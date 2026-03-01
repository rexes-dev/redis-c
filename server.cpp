#include "utils.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

static std::map<std::string, std::string> g_data;

static Conn *handle_accept(int listenfd) {
  // accept
  sockaddr_in client_addr{};
  socklen_t addrlen = sizeof(client_addr);

  const auto connfd =
      accept(listenfd, reinterpret_cast<sockaddr *>(&client_addr), &addrlen);
  if (connfd < 0) {
    std::cerr << std::string("accept: ") + std::strerror(errno) << '\n';
    return nullptr;
  }

  // set the new connfd to non-blocking mode
  fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK);

  // create struct Conn
  Conn *conn = new Conn();
  conn->fd = connfd;
  conn->want_read = true; // read the 1st req
  return conn;
}

static int parse_req(const u8 *data, size_t size,
                     std::vector<std::string> &out) {
  u32 nstr = 0;
  if (size < sizeof(u32))
    return -1;

  std::memcpy(&nstr, data, sizeof(u32));
  size -= sizeof(u32);
  data += sizeof(u32);

  for (u32 i = 0; i < nstr; ++i) {
    u32 len = 0;
    if (size < sizeof(u32))
      return -1;

    std::memcpy(&len, data, sizeof(u32));
    size -= sizeof(u32);
    data += sizeof(u32);
    out.push_back("");

    if (size < len)
      return -1;
    out.back().assign(data, data + len);
    size -= len;
    data += len;
  }

  if (size > 0)
    return -1;

  return 0;
}

static void do_request(std::vector<std::string> &cmd, Response &res) {
  if (cmd.size() == 2 && cmd[0] == "get") {
    auto it = g_data.find(cmd[1]);
    if (it == g_data.end()) {
      res.status = RES_NX; // not found
      return;
    }
    const auto &val = it->second;
    res.data.assign(val.begin(), val.end());
  } else if (cmd.size() == 3 && cmd[0] == "set") {
    g_data[cmd[1]].swap(cmd[2]);
  } else if (cmd.size() == 2 && cmd[0] == "del") {
    g_data.erase(cmd[1]);
  } else {
    res.status = RES_ERR; // unrecognized command
  }
}

static void make_response(const Response &res, std::vector<u8> &out) {
  const u32 res_len = sizeof(u32) + res.data.size();
  const u32 out_len = sizeof(u32) + res_len;
  out.resize(out_len);
  std::memcpy(&out[0], &res_len, sizeof(u32));
  std::memcpy(&out[sizeof(u32)], &res.status, sizeof(u32));
  std::memcpy(&out[2 * sizeof(u32)], &res.data[0], res.data.size());
}

static bool try_one_request(Conn *conn) {
  // 3. Try to parse the accumulated buf
  if (conn->incoming.size() < sizeof(u32))
    return false;

  u32 len = 0;
  memcpy(&len, conn->incoming.data(), sizeof(u32));
  if (len > kMaxMsg) {
    std::cerr << "The message is too long: " << len << '\n';
    conn->want_close = true;
    return false;
  }

  // message body
  if (sizeof(u32) + len > conn->incoming.size())
    return false;

  const auto *req = &conn->incoming[sizeof(u32)];

  std::vector<std::string> cmd;
  if (parse_req(req, len, cmd) < 0) {
    conn->want_close = true;
    return false;
  }
  Response res;
  do_request(cmd, res);
  make_response(res, conn->outgoing);

  conn->incoming.erase(conn->incoming.begin(),
                       conn->incoming.begin() + sizeof(u32) + len);
  return true;
}

static void handle_write(Conn *conn) {
  const auto rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());

  // probably kernel buf is full
  if (rv < 0 && errno == EAGAIN)
    return;

  if (rv < 0) {
    conn->want_close = true;
    return;
  }

  if (conn->outgoing.size() == 0) {
    conn->want_read = true;
    conn->want_write = false;
  }

  conn->outgoing.erase(conn->outgoing.begin(), conn->outgoing.begin() + rv);
}

static void handle_read(Conn *conn) {
  // 1. Do a non-blocking read
  std::array<u8, 64 * 1024> buf;
  const auto rv = read(conn->fd, buf.data(), buf.size());
  if (rv <= 0) { // handle IO error (rv < 0) or EOF (rv == 0)
    conn->want_close = true;
    return;
  }

  // 2. Add new data to the `Conn::incoming` buffer
  conn->incoming.insert(conn->incoming.end(), buf.data(), buf.data() + rv);

  while (try_one_request(conn))
    ;

  if (conn->outgoing.size() > 0) {
    conn->want_read = false;
    conn->want_write = true;

    // No need to wait until next event loop
    return handle_write(conn);
  }
}

int main() {
  /*
    AF_INET - IPv4
    SOCK_STREAM - TCP
    0 - useless for our purpose
  */
  const auto listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0)
    unix_error("socket");

  // See https://stackoverflow.com/a/3233022
  const int opt_val = 1;
  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt_val,
                 sizeof(opt_val)) != 0)
    unix_error("setsockopt");

  // sockaddr (Generic)
  // sockaddr_in (IPv4-specific)
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);     // port
  addr.sin_addr.s_addr = htonl(0); // wildcard IP 0.0.0.0
  if (bind(listenfd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) !=
      0)
    unix_error("bind");

  if (listen(listenfd, SOMAXCONN) != 0)
    unix_error("listen");

  // a map of all client connections, keyed by fd;
  std::vector<Conn *> fd2conn;

  // the event loop
  std::vector<pollfd> poll_args;
  while (true) {
    // prepare the arguments of the poll()
    poll_args.clear();

    // put the listening sockets in the first position
    // accept() is treated as read()
    pollfd pfd{listenfd, POLLIN, 0};
    poll_args.push_back(pfd);

    // the rest are connection sockets
    for (auto *conn : fd2conn) {
      if (!conn)
        continue;

      // POLLERR indicates a socket error that
      // we always want to be notified about
      pollfd pfd{conn->fd, POLLERR, 0};

      // poll() flags from the app's intent
      if (conn->want_read)
        pfd.events |= POLLIN;
      if (conn->want_write)
        pfd.events |= POLLOUT;
      poll_args.push_back(pfd);
    }

    // wait for readiness
    const auto err = poll(poll_args.data(), poll_args.size(), -1);
    if (err < 0) {
      if (errno == EINTR)
        continue; // not an error

      unix_error("poll");
    }

    // handle the listening socket
    if (poll_args[0].revents) {
      if (Conn *conn = handle_accept(listenfd)) {
        // put into the map
        if (fd2conn.size() <= static_cast<size_t>(conn->fd))
          fd2conn.resize(conn->fd + 1);
        fd2conn[conn->fd] = conn;
      }
    }

    // handle connection sockets
    for (size_t i = 1; i < poll_args.size(); ++i) {
      const auto poll_arg = poll_args[i];
      const auto ready = poll_arg.revents;
      auto *conn = fd2conn[poll_arg.fd];
      if (ready & POLLIN)
        handle_read(conn);
      if (ready & POLLOUT)
        handle_write(conn);

      // close the socket from socket error or app logic
      if ((ready & POLLERR) || conn->want_close) {
        close(conn->fd);
        fd2conn[conn->fd] = nullptr;
        delete conn;
      }
    }
  }

  return 0;
}
