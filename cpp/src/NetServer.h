#pragma once

#include <cstdint>
#include <string>

namespace net {

// Runs the online server on this UDP port until stopped (or for `seconds`,
// when that is above zero — the test suite uses it). No window, no GL:
// `erebus_native --server [port]` on any machine your friends can reach.
int runServer(uint16_t port, double seconds = 0.0);

// Many servers for one game: one main server (social: tags, friends, squads,
// and which copy of a place you go to) and any number of instance servers,
// each running some of the copies (positions, Cybers, scores — the busy
// traffic). An instance server is started with `join` = the main server's
// address; `publicAddr` is how players reach it (needed when it sits behind
// a router or on another machine); `key` must match the main server's
// (EREBUS_SHARD_KEY) or it is not let in. New copies of a place go to the
// least busy instance server; if one goes quiet, its players are moved.
struct ServerOptions {
  uint16_t port = 27960;
  double seconds = 0.0;
  std::string join;        // empty: a main (or only) server
  std::string publicAddr;  // host[:port] players should use to reach this one
  std::string key;
};
int runServer(const ServerOptions& options);

}  // namespace net
