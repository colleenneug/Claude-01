#include "Net.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace net {

bool startup() {
#ifdef _WIN32
  static bool done = false;
  if (done) return true;
  WSADATA w;
  done = WSAStartup(MAKEWORD(2, 2), &w) == 0;
  return done;
#else
  return true;
#endif
}

std::string Address::str() const {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u", (ip >> 24) & 255u, (ip >> 16) & 255u, (ip >> 8) & 255u,
                ip & 255u, (unsigned)port);
  return buf;
}

bool resolve(const std::string& hostPort, uint16_t defaultPort, Address& out) {
  if (!startup()) return false;
  std::string host = hostPort;
  uint16_t port = defaultPort;
  const size_t colon = hostPort.rfind(':');
  if (colon != std::string::npos) {
    host = hostPort.substr(0, colon);
    try {
      port = (uint16_t)std::stoi(hostPort.substr(colon + 1));
    } catch (...) {
      return false;
    }
  }
  if (host.empty()) host = "127.0.0.1";
  addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
  const sockaddr_in* sin = (const sockaddr_in*)res->ai_addr;
  out.ip = ntohl(sin->sin_addr.s_addr);
  out.port = port;
  freeaddrinfo(res);
  return true;
}

Socket::~Socket() { close(); }

bool Socket::open(uint16_t port) {
  if (!startup()) return false;
  close();
  long long fd = (long long)::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
  if ((SOCKET)fd == INVALID_SOCKET) return false;
#else
  if (fd < 0) return false;
#endif
  sockaddr_in a;
  std::memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(port);
  if (::bind((decltype(::socket(0, 0, 0)))fd, (const sockaddr*)&a, sizeof(a)) != 0) {
#ifdef _WIN32
    closesocket((SOCKET)fd);
#else
    ::close((int)fd);
#endif
    return false;
  }
#ifdef _WIN32
  u_long nb = 1;
  ioctlsocket((SOCKET)fd, FIONBIO, &nb);
  // Windows reports an ICMP "port unreachable" from an earlier send (a
  // player who just quit) as an error on the next receive, which would cut
  // the server's receive loop short every time someone leaves. Off.
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
  BOOL reportReset = FALSE;
  DWORD unused = 0;
  WSAIoctl((SOCKET)fd, SIO_UDP_CONNRESET, &reportReset, sizeof(reportReset), nullptr, 0, &unused, nullptr, nullptr);
#else
  fcntl((int)fd, F_SETFL, fcntl((int)fd, F_GETFL, 0) | O_NONBLOCK);
#endif
  sockaddr_in bound;
  socklen_t len = sizeof(bound);
  getsockname((decltype(::socket(0, 0, 0)))fd, (sockaddr*)&bound, &len);
  port_ = ntohs(bound.sin_port);
  fd_ = fd;
  return true;
}

bool Socket::enableBroadcast() {
  if (fd_ < 0) return false;
  int on = 1;
  return setsockopt((decltype(::socket(0, 0, 0)))fd_, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof(on)) == 0;
}

void Socket::close() {
  if (fd_ < 0) return;
#ifdef _WIN32
  closesocket((SOCKET)fd_);
#else
  ::close((int)fd_);
#endif
  fd_ = -1;
}

bool Socket::send(const Address& to, const void* data, size_t len) {
  if (fd_ < 0 || !to.valid()) return false;
  sockaddr_in a;
  std::memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(to.ip);
  a.sin_port = htons(to.port);
  const auto r = ::sendto((decltype(::socket(0, 0, 0)))fd_, (const char*)data, (int)len, 0,
                          (const sockaddr*)&a, sizeof(a));
  return r == (decltype(r))len;
}

int Socket::recv(Address& from, void* buf, size_t cap) {
  if (fd_ < 0) return 0;
  sockaddr_in a;
  socklen_t len = sizeof(a);
  const auto r = ::recvfrom((decltype(::socket(0, 0, 0)))fd_, (char*)buf, (int)cap, 0, (sockaddr*)&a, &len);
  if (r <= 0) return 0;
  from.ip = ntohl(a.sin_addr.s_addr);
  from.port = ntohs(a.sin_port);
  return (int)r;
}

void Writer::f32(float v) {
  uint32_t u;
  std::memcpy(&u, &v, 4);
  u32(u);
}

void Writer::str(const std::string& s) {
  const size_t n = s.size() > 1000 ? 1000 : s.size();
  u16((uint16_t)n);
  b.insert(b.end(), s.begin(), s.begin() + (long)n);
}

float Reader::f32() {
  uint32_t u = u32();
  float v;
  std::memcpy(&v, &u, 4);
  return v;
}

std::string Reader::str() {
  const uint16_t len = u16();
  if (!ok || at + len > n) { ok = false; return std::string(); }
  std::string s((const char*)p + at, len);
  at += len;
  return s;
}

bool discover(uint16_t port, double seconds, Address& out) {
  Socket s;
  if (!s.open(0)) return false;
  s.enableBroadcast();
  // magic "ERB1", version, Discover (see NetProtocol.h), and a zero id.
  const uint8_t ask[14] = {0x45, 0x52, 0x42, 0x31, 1, 21, 0, 0, 0, 0, 0, 0, 0, 0};
  Address everyone;
  everyone.ip = 0xFFFFFFFFu;
  everyone.port = port;
  Address local;
  local.ip = 0x7F000001u;
  local.port = port;
  const double end = now() + seconds;
  double nextAsk = 0.0;
  while (now() < end) {
    if (now() >= nextAsk) {
      s.send(everyone, ask, sizeof(ask));
      s.send(local, ask, sizeof(ask));   // a copy on this machine, if broadcast does not loop back
      nextAsk = now() + 0.2;
    }
    uint8_t buf[256];
    Address from;
    const int n = s.recv(from, buf, sizeof(buf));
    // The answer: magic, version, DiscoverReply (90), then the port.
    if (n >= 8 && buf[0] == 0x45 && buf[1] == 0x52 && buf[2] == 0x42 && buf[3] == 0x31 && buf[5] == 90) {
      out.ip = from.ip;
      out.port = (uint16_t)(buf[6] | (buf[7] << 8));
      return true;
    }
    if (n <= 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

double now() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace net
