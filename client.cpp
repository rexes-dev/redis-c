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

static int print_response(const u8 *data, u32 size) {
  if (size < 1) {
    std::cerr << "Bad response" << '\n';
    return -1;
  }
  switch (data[0]) {
  case TAG_NIL: {
    std::cout << "(nil)\n";
    return 1;
  }
  case TAG_ERR: {
    if (size < sizeof(u8) + 2 * sizeof(u32)) {
      std::cerr << "Bad response" << '\n';
      return -1;
    }

    u32 code = 0;
    u32 len = 0;
    std::memcpy(&code, &data[sizeof(u8)], sizeof(u32));
    std::memcpy(&len, &data[sizeof(u8) + sizeof(u32)], sizeof(u32));

    if (size < sizeof(u8) + 2 * sizeof(u32) + len) {
      std::cerr << "Bad response" << '\n';
      return -1;
    }
    std::cout << "(err) " << code;
    std::cout.write(
        reinterpret_cast<const char *>(&data[sizeof(u8) + 2 * sizeof(u32)]),
        len);
    std::cout << '\n';
    return sizeof(u8) + 2 * sizeof(u32) + len;
  }
  case TAG_STR: {
    if (size < sizeof(u8) + sizeof(u32)) {
      std::cerr << "Bad response" << '\n';
      return -1;
    }

    u32 len = 0;
    std::memcpy(&len, &data[sizeof(u8)], sizeof(u32));
    if (size < sizeof(u8) + sizeof(u32) + len) {
      std::cerr << "Bad response" << '\n';
      return -1;
    }

    std::cout << "(str) ";
    std::cout.write(
        reinterpret_cast<const char *>(&data[sizeof(u8) + sizeof(u32)]), len);
    std::cout << '\n';

    return sizeof(u8) + sizeof(u32) + len;
  }
  case TAG_INT: {
    if (size < sizeof(u8) + sizeof(i64)) {
      std::cerr << "Bad response" << '\n';
      return -1;
    }

    i64 val = 0;
    std::memcpy(&val, &data[sizeof(u8)], sizeof(i64));
    std::cout << "(int) " << val << '\n';

    return sizeof(u8) + sizeof(i64);
  }
  case TAG_DBL: {
    if (size < sizeof(u8) + sizeof(double)) {
      std::cerr << "Bad response" << '\n';
      return -1;
    }

    double val = 0;
    std::memcpy(&val, &data[sizeof(u8)], sizeof(double));
    std::cout << "(dbl) " << val << '\n';

    return sizeof(u8) + sizeof(double);
  }
  case TAG_ARR: {
    if (size < sizeof(u8) + sizeof(u32)) {
      std::cerr << "Bad response" << '\n';
      return -1;
    }

    u32 len = 0;
    std::memcpy(&len, &data[sizeof(u8)], sizeof(u32));
    std::cout << "(arr) len=" << len << '\n';
    u32 arr_bytes = sizeof(u8) + sizeof(u32);
    for (u32 i = 0; i < len; ++i) {
      const auto err = print_response(&data[arr_bytes], size - arr_bytes);
      if (err < 0)
        return err;
      arr_bytes += err;
    }
    std::cout << "(arr) end\n";
    return arr_bytes;
  }
  default: {
    std::cerr << "Bad response" << '\n';
    return -1;
  }
  }
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

  err = print_response(&rbuf[sizeof(u32)], len);
  if (err > 0 && err != len) {
    std::cerr << "Bad response" << '\n';
    err = -1;
  }

  return err;
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