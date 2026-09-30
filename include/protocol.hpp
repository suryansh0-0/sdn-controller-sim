#pragma once
// Control-plane wire protocol.
// TCP is a byte stream, so every message is length-prefixed:
//   [4-byte big-endian length][payload]
// The payload is a space-separated text message, e.g. "PACKET_IN 1 1 3 42 15".
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace proto {

inline bool read_all(int fd, void* buf, size_t n) {
    char* p = static_cast<char*>(buf);
    while (n > 0) {
        ssize_t r = ::recv(fd, p, n, 0);
        if (r <= 0) return false;  // error or peer closed
        p += r;
        n -= static_cast<size_t>(r);
    }
    return true;
}

inline bool write_all(int fd, const void* buf, size_t n) {
    const char* p = static_cast<const char*>(buf);
    while (n > 0) {
        ssize_t w = ::send(fd, p, n, 0);
        if (w <= 0) return false;
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

inline bool send_msg(int fd, const std::string& payload) {
    uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    std::string buf(4, '\0');
    std::memcpy(&buf[0], &len, 4);
    buf += payload;
    return write_all(fd, buf.data(), buf.size());
}

inline bool recv_msg(int fd, std::string& out) {
    uint32_t len = 0;
    if (!read_all(fd, &len, 4)) return false;
    len = ntohl(len);
    if (len > (1u << 20)) return false;  // sanity limit: 1 MiB
    out.assign(len, '\0');
    return len == 0 || read_all(fd, &out[0], len);
}

inline std::vector<std::string> split(const std::string& s) {
    std::istringstream is(s);
    std::vector<std::string> v;
    std::string tok;
    while (is >> tok) v.push_back(tok);
    return v;
}

}  // namespace proto
