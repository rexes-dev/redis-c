#include "utils.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static void do_something(int connfd) {
  std::array<char, 64> rbuf{};
  const auto n = read(connfd, rbuf.data(), rbuf.size() - 1);
  if (n < 0)
    unix_error("read");
  std::cout << "client says: " << rbuf.data() << '\n';

  std::string wbuf = "world";
  // For now, we ignore the return value
  write(connfd, wbuf.data(), wbuf.size());
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

  while (true) {
    sockaddr_in client_addr{};
    socklen_t addrlen = sizeof(client_addr);
    int connfd =
        accept(listenfd, reinterpret_cast<sockaddr *>(&client_addr), &addrlen);
    if (connfd < 0) {
      std::cerr << std::string("accept: ") + std::strerror(errno) << std::endl;
      continue;
    }

    do_something(connfd);

    close(connfd);
  }

  return 0;
}