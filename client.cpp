#include "utils.h"

#include <array>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int send_req(int fd, const std::vector<std::string> &cmd) {
  u32 len = sizeof(u32);
  for (const auto &s : cmd)
    len += sizeof(u32) + s.size();

  if (len > kMaxMsg)
    return -1;

  std::vector<u8> wbuf(sizeof(u32) + len);
  std::memcpy(&wbuf[0], &len, sizeof(u32));

  const u32 ncmd = cmd.size();
  std::memcpy(&wbuf[sizeof(u32)], &ncmd, sizeof(u32));

  u32 offset = 2 * sizeof(u32);
  for (const auto &s : cmd) {
    const u32 slen = s.size();
    std::memcpy(&wbuf[offset], &slen, sizeof(u32));
    std::memcpy(&wbuf[offset + sizeof(u32)], s.data(), s.size());
    offset += sizeof(u32) + slen;
  }
  return write_all(fd, wbuf.data(), wbuf.size());
}

static int read_res(int fd) {
  std::vector<u8> rbuf(sizeof(u32));
  errno = 0;
  auto err = read_full(fd, &rbuf[0], sizeof(u32));
  if (err) {
    std::cerr << (errno == 0 ? "EOF" : std::strerror(errno)) << '\n';
    return err;
  }

  u32 len = 0;
  std::memcpy(&len, &rbuf[0], sizeof(u32));
  if (len > kMaxMsg) {
    std::cerr << "The message is too long: " << len << '\n';
    return -1;
  }
  if (len < 4) {
    std::cerr << "Bad response" << '\n';
    return -1;
  }

  rbuf.resize(sizeof(u32) + len);
  err = read_full(fd, &rbuf[sizeof(u32)], len);
  if (err) {
    std::cerr << (errno == 0 ? "EOF" : std::strerror(errno)) << '\n';
    return err;
  }
  u32 rescode = 0;
  std::memcpy(&rescode, &rbuf[sizeof(u32)], sizeof(u32));

  std::cout << "server says: [" << rescode << "] ";
  std::cout.write(reinterpret_cast<const char *>(&rbuf[2 * sizeof(u32)]),
                  len - 4);
  std::cout << '\n';
  return 0;
}

int main(int argc, char **argv) {
  const auto fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    unix_error("socket");

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0)
    unix_error("connect");

  std::vector<std::string> cmd;
  for (int i = 1; i < argc; ++i)
    cmd.push_back(argv[i]);

  auto err = send_req(fd, cmd);
  if (err)
    goto L_DONE;

  err = read_res(fd);
  if (err)
    goto L_DONE;

L_DONE:
  close(fd);

  return 0;
}