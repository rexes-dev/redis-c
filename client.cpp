#include "utils.h"

#include <array>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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

  std::string msg = "hello";
  write(fd, msg.data(), msg.size());

  std::array<char, 64> rbuf{};
  const auto n = read(fd, rbuf.data(), rbuf.size() - 1);
  if (n < 0)
    unix_error("read");

  std::cout << "server says: " << rbuf.data() << '\n';
  close(fd);

  return 0;
}