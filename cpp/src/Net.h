#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The online layer's plumbing: a UDP socket that works the same on Windows
// (Winsock) and everywhere else (BSD sockets), an address, and a byte
// writer/reader for the messages. No library: the game already ships with
// no network dependency, and a few hundred lines of sockets are easier to
// build on every machine than one more package.
//
// The protocol itself is in NetProtocol.h; the server in NetServer.cpp; the
// game's side in NetClient.cpp.
namespace net {

bool startup();   // Winsock wants this once; a no-op elsewhere

struct Address {
  uint32_t ip = 0;     // host byte order
  uint16_t port = 0;
  bool operator==(const Address& o) const { return ip == o.ip && port == o.port; }
  bool operator!=(const Address& o) const { return !(*this == o); }
  bool valid() const { return port != 0; }
  std::string str() const;
};

// "host", "host:port", "1.2.3.4:5678". Looks the host up if it is a name.
bool resolve(const std::string& hostPort, uint16_t defaultPort, Address& out);

class Socket {
public:
  Socket() = default;
  ~Socket();
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  // Port 0 picks any free one (a client); a server names its own.
  bool open(uint16_t port = 0);
  // Lets send() reach 255.255.255.255 — everyone on the local network.
  bool enableBroadcast();
  void close();
  bool isOpen() const { return fd_ >= 0; }
  bool send(const Address& to, const void* data, size_t len);
  // Non-blocking: returns the bytes read, or 0 when nothing is waiting.
  int recv(Address& from, void* buf, size_t cap);
  uint16_t localPort() const { return port_; }

private:
  long long fd_ = -1;
  uint16_t port_ = 0;
};

// Little-endian, fixed-width, strings with a 16-bit length.
struct Writer {
  std::vector<uint8_t> b;
  void u8(uint8_t v) { b.push_back(v); }
  void u16(uint16_t v) { u8((uint8_t)v); u8((uint8_t)(v >> 8)); }
  void u32(uint32_t v) { u16((uint16_t)v); u16((uint16_t)(v >> 16)); }
  void u64(uint64_t v) { u32((uint32_t)v); u32((uint32_t)(v >> 32)); }
  void f32(float v);
  void str(const std::string& s);
};

struct Reader {
  const uint8_t* p = nullptr;
  size_t n = 0, at = 0;
  bool ok = true;
  Reader(const void* data, size_t len) : p((const uint8_t*)data), n(len) {}
  uint8_t u8() {
    if (at + 1 > n) { ok = false; return 0; }
    return p[at++];
  }
  uint16_t u16() { uint16_t a = u8(); return (uint16_t)(a | (uint16_t)u8() << 8); }
  uint32_t u32() { uint32_t a = u16(); return a | (uint32_t)u16() << 16; }
  uint64_t u64() { uint64_t a = u32(); return a | (uint64_t)u32() << 32; }
  float f32();
  std::string str();
};

// Looks for a game server on the local network for up to `seconds`: true
// with its address if one answered.
bool discover(uint16_t port, double seconds, Address& out);

// Wall-clock seconds, for timeouts. Monotonic.
double now();

}  // namespace net
