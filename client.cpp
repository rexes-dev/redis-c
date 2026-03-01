#include "utils.h"

#include <array>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int send_req(int fd, const u8 *text, u32 len) {
  if (len > kMaxMsg)
    return -1;

  std::vector<u8> wbuf(sizeof(u32) + len);
  std::memcpy(&wbuf[0], &len, sizeof(u32));
  std::memcpy(&wbuf[sizeof(u32)], text, len);
  return write_all(fd, wbuf.data(), sizeof(u32) + len);
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

  rbuf.resize(sizeof(u32) + len);
  err = read_full(fd, &rbuf[sizeof(u32)], len);
  if (err) {
    std::cerr << (errno == 0 ? "EOF" : std::strerror(errno)) << '\n';
    return err;
  }
  std::cout << "len:" << len << " data:";
  std::cout.write(reinterpret_cast<const char *>(&rbuf[sizeof(u32)]), len);
  std::cout << '\n';
  return 0;
}

int main() {
  const auto fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    unix_error("socket");

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0)
    unix_error("connect");

  const std::vector<std::string> query_list = {
      "hello1",
      "hello2",
      "hello3",
      // a large message requires multiple event loop iterations
      std::string(kMaxMsg, 'z'),
      "hello5",
  };
  for (const auto &s : query_list) {
    const auto err =
        send_req(fd, reinterpret_cast<const u8 *>(s.data()), s.size());
    if (err)
      goto L_DONE;
  }

  for (const auto &_ : query_list) {
    const auto err = read_res(fd);
    if (err)
      goto L_DONE;
  }

L_DONE:
  close(fd);

  return 0;
}