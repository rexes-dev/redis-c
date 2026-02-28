#pragma once

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

using u32 = uint32_t;

constexpr auto kMaxMsg = 4096;

static void unix_error(const char *msg) {
  throw std::runtime_error(std::string(msg) + ": " + std::strerror(errno));
}

static int read_full(int fd, char *buf, size_t n) {
  while (n > 0) {
    const auto rv = read(fd, buf, n);
    if (rv == -1 && errno == EINTR)
      continue;
    if (rv <= 0)
      return -1;
    n -= rv;
    buf += rv;
  }
  return 0;
}

static int write_all(int fd, const char *buf, size_t n) {
  while (n > 0) {
    const auto rv = write(fd, buf, n);
    if (rv <= 0)
      return -1;
    n -= rv;
    buf += rv;
  }
  return 0;
}
