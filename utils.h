#pragma once

#include <cstring>
#include <stdexcept>
#include <string>

inline void unix_error(const char *msg) {
  throw std::runtime_error(std::string(msg) + ": " + std::strerror(errno));
}
