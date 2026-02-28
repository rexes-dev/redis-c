#include "utils.h"

#include <array>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int query(int fd, const char *text) {
  auto len = static_cast<u32>(strlen(text));
  if (len > kMaxMsg)
    return -1;

  std::array<char, sizeof(u32) + kMaxMsg> wbuf;
  std::memcpy(&wbuf[0], &len, sizeof(u32));
  std::memcpy(&wbuf[sizeof(u32)], text, len);

  auto err = write_all(fd, wbuf.data(), sizeof(u32) + len);
  if (err)
    return err;

  std::array<char, sizeof(u32) + kMaxMsg> rbuf;
  errno = 0;

  err = read_full(fd, &rbuf[0], sizeof(u32));
  if (err) {
    std::cerr << (errno == 0 ? "EOF" : std::strerror(errno)) << '\n';
    return err;
  }

  std::memcpy(&len, &rbuf[0], sizeof(u32));
  if (len > kMaxMsg) {
    std::cerr << "The message is too long: " << len << '\n';
    return -1;
  }
  err = read_full(fd, &rbuf[sizeof(u32)], len);
  if (err) {
    std::cerr << (errno == 0 ? "EOF" : std::strerror(errno)) << '\n';
    return err;
  }

  std::cout << "server says: ";
  std::cout.write(&rbuf[sizeof(u32)], len);
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

  // std::string msg = "hello";
  // write(fd, msg.data(), msg.size());

  // std::array<char, 64> rbuf{};
  // const auto n = read(fd, rbuf.data(), rbuf.size() - 1);
  // if (n < 0)
  //   unix_error("read");

  // std::cout << "server says: " << rbuf.data() << '\n';

  auto err = query(fd, "hello1");
  if (err)
    goto L_DONE;

  err = query(fd, "hello2");
  if (err)
    goto L_DONE;

L_DONE:
  close(fd);

  return 0;
}