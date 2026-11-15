#pragma once

#include <cstdint>

namespace net {

// Runs the online server on this UDP port until stopped (or for `seconds`,
// when that is above zero — the test suite uses it). No window, no GL:
// `erebus_native --server [port]` on any machine your friends can reach.
int runServer(uint16_t port, double seconds = 0.0);

}  // namespace net
