// Tests for the net library: bit packing (round trips, exact layout,
// reading past the end), quantization precision (floats, vectors,
// smallest-three quaternions), and a real connection between a server Host
// and a client Host on this machine (UDP over 127.0.0.1).
//
// No window, no GPU. The loopback tests use port 0 (the OS picks a free
// one), so they never collide with anything else running.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/net/BitStream.h"
#include "playground/net/Host.h"
#include "playground/net/Quantize.h"

#include <bit>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

using namespace playground;
using net::BitReader;
using net::BitWriter;

namespace {

std::vector<std::byte> bytesOf(std::initializer_list<u8> values) {
  std::vector<std::byte> out;
  for (const u8 v : values) out.push_back(std::byte{v});
  return out;
}

}  // namespace

// --- BitWriter / BitReader -------------------------------------------------------------

TEST_CASE("bits are packed least significant first, with no gaps, the same on every CPU") {
  BitWriter out;
  out.writeBits(0b101, 3);  // bits 0-2 of byte 0
  out.writeBits(0xFF, 8);   // bits 3-7 of byte 0, then bits 0-2 of byte 1
  CHECK(out.bitCount() == 11);
  CHECK(std::vector<std::byte>(out.bytes().begin(), out.bytes().end()) == bytesOf({0xFD, 0x07}));

  BitWriter word;
  word.writeU32(0x12345678u);  // little-endian by construction, whatever this CPU is
  CHECK(std::vector<std::byte>(word.bytes().begin(), word.bytes().end()) == bytesOf({0x78, 0x56, 0x34, 0x12}));
}

TEST_CASE("every width from 1 to 64 bits round-trips, in any order and alignment") {
  std::mt19937_64 rng(42);
  BitWriter out;
  std::vector<std::pair<u64, u32>> written;
  usize bits = 0;
  for (int round = 0; round < 3; ++round) {
    for (u32 count = 1; count <= 64; ++count) {
      const u64 mask = count == 64 ? ~u64{0} : (u64{1} << count) - 1;
      const u64 value = rng() & mask;
      out.writeBits(value, count);
      written.emplace_back(value, count);
      bits += count;
    }
  }
  CHECK(out.bitCount() == bits);
  CHECK(out.byteCount() == (bits + 7) / 8);
  const std::vector<std::byte> data = out.take();
  CHECK(out.bitCount() == 0);  // take() starts it afresh
  BitReader in(data);
  for (const auto& [value, count] : written) CHECK(in.readBits(count) == value);
  CHECK(in.ok());
  CHECK(in.bitsLeft() < 8);  // only the padding of the last byte
}

TEST_CASE("typed values round-trip exactly: signed, floats bit for bit, varints, strings, bytes") {
  BitWriter out;
  out.writeBool(true);
  out.writeI32(-123456789);
  out.writeI32(std::numeric_limits<i32>::min());
  for (const f32 f : {0.0f, -0.0f, 1.5f, -3.25e-30f, std::numeric_limits<f32>::infinity(),
                      std::numeric_limits<f32>::quiet_NaN()}) {
    out.writeF32(f);
  }
  for (const u32 v : {0u, 127u, 128u, 16383u, 16384u, std::numeric_limits<u32>::max()}) out.writeVarU32(v);
  out.writeString("");
  out.writeString("hello, Erebus");
  out.writeBytes(bytesOf({1, 2, 255}));
  out.writeBits(0b1, 1);
  out.alignToByte();
  out.writeU16(0xBEEF);

  const std::vector<std::byte> data = out.take();
  BitReader in(data);
  CHECK(in.readBool());
  CHECK(in.readI32() == -123456789);
  CHECK(in.readI32() == std::numeric_limits<i32>::min());
  for (const f32 f : {0.0f, -0.0f, 1.5f, -3.25e-30f, std::numeric_limits<f32>::infinity(),
                      std::numeric_limits<f32>::quiet_NaN()}) {
    CHECK(std::bit_cast<u32>(in.readF32()) == std::bit_cast<u32>(f));  // even -0 and NaN, exactly
  }
  for (const u32 v : {0u, 127u, 128u, 16383u, 16384u, std::numeric_limits<u32>::max()}) CHECK(in.readVarU32() == v);
  CHECK(in.readString().empty());
  CHECK(in.readString() == "hello, Erebus");
  std::vector<std::byte> three(3);
  in.readBytes(three);
  CHECK(three == bytesOf({1, 2, 255}));
  CHECK(in.readBits(1) == 1);
  in.alignToByte();
  CHECK(in.readU16() == 0xBEEF);
  CHECK(in.ok());
}

TEST_CASE("small numbers take fewer bits as varints") {
  BitWriter small;
  small.writeVarU32(100);
  CHECK(small.bitCount() == 8);
  BitWriter large;
  large.writeVarU32(std::numeric_limits<u32>::max());
  CHECK(large.bitCount() == 40);
}

TEST_CASE("reading past the end returns zeros and fails, and stays failed") {
  const std::vector<std::byte> twoBytes = bytesOf({0xAB, 0xCD});
  BitReader in(twoBytes);
  CHECK(in.readU8() == 0xAB);
  CHECK(in.ok());
  CHECK(in.readU16() == 0);  // only 8 bits left
  CHECK_FALSE(in.ok());
  CHECK(in.readU8() == 0);   // sticky: even what would have fitted
  CHECK_FALSE(in.ok());
  CHECK(in.bitsLeft() == 0);

  BitReader tooWide(twoBytes);
  CHECK(tooWide.readBits(65) == 0);
  CHECK_FALSE(tooWide.ok());

  BitReader empty(std::span<const std::byte>{});
  CHECK_FALSE(empty.readBool());
  CHECK_FALSE(empty.ok());
}

TEST_CASE("hostile lengths and malformed varints are refused, not trusted") {
  SUBCASE("a string longer than the data") {
    BitWriter out;
    out.writeVarU32(1000);  // claims 1000 bytes follow
    out.writeU8('x');
    const std::vector<std::byte> data = out.take();
    BitReader in(data);
    CHECK(in.readString().empty());
    CHECK_FALSE(in.ok());
  }
  SUBCASE("a string longer than the reader allows") {
    BitWriter out;
    out.writeString("0123456789");
    const std::vector<std::byte> data = out.take();
    BitReader in(data);
    CHECK(in.readString(4).empty());
    CHECK_FALSE(in.ok());
  }
  SUBCASE("a varint that never ends") {
    const std::vector<std::byte> data = bytesOf({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    BitReader in(data);
    CHECK(in.readVarU32() == 0);
    CHECK_FALSE(in.ok());
  }
  SUBCASE("bytes past the end leave the output zeroed") {
    const std::vector<std::byte> data = bytesOf({7});
    BitReader in(data);
    std::vector<std::byte> out(4, std::byte{0x55});
    in.readBytes(out);
    CHECK(out == std::vector<std::byte>(4, std::byte{0}));
    CHECK_FALSE(in.ok());
  }
  SUBCASE("a message cut short by one byte is detected") {
    BitWriter out;
    out.writeU32(7);
    out.writeVarU32(300);
    out.writeF32(2.5f);
    std::vector<std::byte> data = out.take();
    data.pop_back();
    BitReader in(data);
    (void)in.readU32();
    (void)in.readVarU32();
    (void)in.readF32();
    CHECK_FALSE(in.ok());
  }
}

// --- Quantization ------------------------------------------------------------------------

TEST_CASE("bounded floats come back within half a step; ends are exact; outside values are clamped") {
  std::mt19937 rng(7);
  for (const u32 bits : {4u, 8u, 12u, 16u, 20u, 24u}) {
    // Half a step, plus one float step near 1000: past about 20 bits over
    // this range, f32 itself is coarser than the quantization.
    const f32 floatStep = std::nextafter(1000.0f, 2000.0f) - 1000.0f;
    const f32 maxError = net::quantizationError(-1000.0f, 1000.0f, bits) + floatStep;
    std::uniform_real_distribution<f32> any(-1000.0f, 1000.0f);
    f32 worst = 0.0f;
    for (int i = 0; i < 2000; ++i) {
      const f32 v = any(rng);
      const f32 back = net::dequantize(net::quantize(v, -1000.0f, 1000.0f, bits), -1000.0f, 1000.0f, bits);
      worst = std::max(worst, std::fabs(back - v));
    }
    INFO(bits, " bits: worst error ", worst, ", bound ", maxError);
    CHECK(worst <= maxError * 1.0001f + 1e-6f);
  }
  CHECK(net::quantizationError(-1000.0f, 1000.0f, 16) == doctest::Approx(2000.0f / 65535.0f / 2.0f));
  CHECK(net::dequantize(net::quantize(-1000.0f, -1000.0f, 1000.0f, 16), -1000.0f, 1000.0f, 16) == -1000.0f);
  CHECK(net::dequantize(net::quantize(1000.0f, -1000.0f, 1000.0f, 16), -1000.0f, 1000.0f, 16) == 1000.0f);
  CHECK(net::quantize(5000.0f, -1000.0f, 1000.0f, 16) == 65535u);  // clamped to max
  CHECK(net::quantize(-5000.0f, -1000.0f, 1000.0f, 16) == 0u);
  CHECK(net::quantize(std::numeric_limits<f32>::quiet_NaN(), -1.0f, 1.0f, 8) == 0u);
  CHECK(net::quantize(0.7f, 0.0f, 1.0f, 1) == 1u);  // 1 bit: two values
  CHECK(net::quantize(0.3f, 0.0f, 1.0f, 1) == 0u);
}

TEST_CASE("quantized vectors pack into exactly the bits asked for and come back within the bound") {
  BitWriter out;
  const glm::vec3 position{123.456f, -0.5f, 999.0f};
  const glm::vec2 flat{-321.0f, 42.42f};
  net::writeVec3(out, position, -1000.0f, 1000.0f, 20);
  net::writeVec2(out, flat, -1000.0f, 1000.0f, 16);
  CHECK(out.bitCount() == 3 * 20 + 2 * 16);
  const std::vector<std::byte> data = out.take();
  BitReader in(data);
  const glm::vec3 p = net::readVec3(in, -1000.0f, 1000.0f, 20);
  const glm::vec2 f = net::readVec2(in, -1000.0f, 1000.0f, 16);
  CHECK(in.ok());
  for (int i = 0; i < 3; ++i) CHECK(std::fabs(p[i] - position[i]) <= net::quantizationError(-1000.0f, 1000.0f, 20));
  for (int i = 0; i < 2; ++i) CHECK(std::fabs(f[i] - flat[i]) <= net::quantizationError(-1000.0f, 1000.0f, 16));
}

TEST_CASE("smallest-three quaternions: 32 bits per rotation, within a fraction of a degree") {
  std::mt19937 rng(2026);
  std::normal_distribution<f32> gauss(0.0f, 1.0f);
  f32 worstDegrees = 0.0f;
  for (int i = 0; i < 1000; ++i) {
    // A uniformly random rotation: a normalised 4D Gaussian.
    glm::quat q(gauss(rng), gauss(rng), gauss(rng), gauss(rng));
    q = glm::normalize(q);
    BitWriter out;
    net::writeQuaternion(out, q, 10);
    CHECK(out.bitCount() == 32);
    const std::vector<std::byte> data = out.take();
    BitReader in(data);
    const glm::quat back = net::readQuaternion(in, 10);
    CHECK(in.ok());
    // The angle between the two rotations (q and -q are the same rotation).
    const f32 dot = std::min(1.0f, std::fabs(glm::dot(q, back)));
    worstDegrees = std::max(worstDegrees, glm::degrees(2.0f * std::acos(dot)));
  }
  INFO("worst error: ", worstDegrees, " degrees");
  CHECK(worstDegrees < 0.25f);

  // Identity and a sign-flipped, unnormalised input.
  BitWriter out;
  net::writeQuaternion(out, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
  net::writeQuaternion(out, glm::quat(-2.0f, 0.0f, 0.0f, 0.0f));  // = identity, twice as long, negated
  const std::vector<std::byte> data = out.take();
  BitReader in(data);
  for (int i = 0; i < 2; ++i) {
    const glm::quat q = net::readQuaternion(in);
    CHECK(std::fabs(q.w) == doctest::Approx(1.0f).epsilon(1e-4));
  }
}

// --- Loopback: a real server and client -------------------------------------------------

namespace {

using net::EventType;

// Services both hosts until `done` returns true, or 3 seconds pass.
bool pump(net::Host& a, net::Host& b, const std::function<void(net::Host&, const net::Event&)>& onEvent,
          const std::function<bool()>& done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!done()) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    for (net::Host* host : {&a, &b}) {
      for (const net::Event& e : host->service(2)) onEvent(*host, e);
    }
  }
  return true;
}

}  // namespace

TEST_CASE("loopback: a client connects to a server; reliable and unreliable packets arrive on their channels") {
  auto server = net::Host::listen(0);  // any free port
  REQUIRE_MESSAGE(server.has_value(), (server ? "" : server.error().message));
  CHECK(server->port() != 0);
  auto client = net::Host::client();
  REQUIRE(client.has_value());
  auto toServer = client->connect("127.0.0.1", server->port());
  REQUIRE_MESSAGE(toServer.has_value(), (toServer ? "" : toServer.error().message));

  net::Peer toClient;
  bool clientConnected = false;
  std::vector<net::Packet> serverGot;
  std::vector<net::Packet> clientGot;
  const auto onEvent = [&](net::Host& host, const net::Event& e) {
    const bool isServer = &host == &*server;
    if (e.type == EventType::Connected) {
      if (isServer) toClient = e.peer;
      else clientConnected = true;
    } else if (e.type == EventType::Received) {
      (isServer ? serverGot : clientGot).push_back(e.packet);
    }
  };
  REQUIRE(pump(*server, *client, onEvent, [&] { return toClient.valid() && clientConnected; }));
  CHECK(server->connectedPeers() == 1);
  const auto info = server->info(toClient);
  REQUIRE(info.has_value());
  CHECK(info->connected);
  CHECK(info->address.starts_with("127.0.0.1:"));

  // Client -> server, reliable: a small message, bit-packed.
  BitWriter hello;
  hello.writeBits(3, 4);  // a message kind
  hello.writeString("player one");
  net::writeVec3(hello, {1.0f, 2.0f, 3.0f}, -100.0f, 100.0f, 16);
  REQUIRE(client->send(*toServer, net::Packet(net::Channel::Reliable, hello.take())).has_value());
  // Server -> client, unreliable: a snapshot.
  BitWriter snapshot;
  snapshot.writeU32(120);  // a tick
  net::writeQuaternion(snapshot, glm::angleAxis(0.5f, glm::vec3(0.0f, 1.0f, 0.0f)));
  REQUIRE(server->send(toClient, net::Packet(net::Channel::Unreliable, snapshot.take())).has_value());

  REQUIRE(pump(*server, *client, onEvent, [&] { return !serverGot.empty() && !clientGot.empty(); }));
  CHECK(serverGot.front().channel() == net::Channel::Reliable);
  BitReader in(serverGot.front().bytes());
  CHECK(in.readBits(4) == 3);
  CHECK(in.readString() == "player one");
  const glm::vec3 v = net::readVec3(in, -100.0f, 100.0f, 16);
  CHECK(in.ok());
  CHECK(v.z == doctest::Approx(3.0f).epsilon(0.001));
  CHECK(clientGot.front().channel() == net::Channel::Unreliable);
  BitReader snap(clientGot.front().bytes());
  CHECK(snap.readU32() == 120);
  const glm::quat q = net::readQuaternion(snap);
  CHECK(snap.ok());
  CHECK(glm::angle(q) == doctest::Approx(0.5f).epsilon(0.01));

  // A polite disconnect: the server hears about it; the old Peer goes stale.
  client->disconnect(*toServer);
  bool serverSawGoodbye = false;
  REQUIRE(pump(*server, *client,
               [&](net::Host& host, const net::Event& e) {
                 if (&host == &*server && e.type == EventType::Disconnected && e.peer == toClient) {
                   serverSawGoodbye = true;
                 }
               },
               [&] { return serverSawGoodbye; }));
  CHECK(server->connectedPeers() == 0);
  CHECK_FALSE(server->info(toClient).has_value());
  const auto stale = server->send(toClient, net::Packet(net::Channel::Reliable, {}));
  REQUIRE_FALSE(stale.has_value());
  CHECK(stale.error().code == Errc::InvalidArgument);
}

TEST_CASE("loopback: a hundred reliable messages arrive, all of them, in order") {
  auto server = net::Host::listen(0);
  REQUIRE(server.has_value());
  auto client = net::Host::client();
  REQUIRE(client.has_value());
  auto toServer = client->connect("127.0.0.1", server->port());
  REQUIRE(toServer.has_value());
  bool connected = false;
  std::vector<u32> received;
  const auto onEvent = [&](net::Host& host, const net::Event& e) {
    if (&host == &*client && e.type == EventType::Connected) connected = true;
    if (&host == &*server && e.type == EventType::Received) received.push_back(BitReader(e.packet.bytes()).readU32());
  };
  REQUIRE(pump(*server, *client, onEvent, [&] { return connected; }));
  for (u32 i = 0; i < 100; ++i) {
    BitWriter out;
    out.writeU32(i);
    REQUIRE(client->send(*toServer, net::Packet(net::Channel::Reliable, out.take())).has_value());
  }
  REQUIRE(pump(*server, *client, onEvent, [&] { return received.size() == 100; }));
  for (u32 i = 0; i < 100; ++i) CHECK(received[i] == i);
}

TEST_CASE("a port already in use is refused with a message naming it") {
  auto first = net::Host::listen(0);
  REQUIRE(first.has_value());
  const auto second = net::Host::listen(first->port());
  REQUIRE_FALSE(second.has_value());
  CHECK(second.error().code == Errc::IoFailure);
  CHECK(second.error().message.find(std::to_string(first->port())) != std::string::npos);
}

// --- Simulated network conditions (Host::simulate) ---------------------------------------

namespace {

struct ConnectedPair {
  net::Host server;
  net::Host client;
  net::Peer toServer;
  net::Peer toClient;
};

// A server and a client, connected (with no simulated conditions yet).
ConnectedPair connectPair() {
  auto server = net::Host::listen(0);
  REQUIRE(server.has_value());
  auto client = net::Host::client();
  REQUIRE(client.has_value());
  auto toServer = client->connect("127.0.0.1", server->port());
  REQUIRE(toServer.has_value());
  net::Peer toClient;
  bool connected = false;
  REQUIRE(pump(*server, *client,
               [&](net::Host& host, const net::Event& e) {
                 if (e.type != EventType::Connected) return;
                 if (&host == &*server) toClient = e.peer;
                 else connected = true;
               },
               [&] { return toClient.valid() && connected; }));
  return {std::move(*server), std::move(*client), *toServer, toClient};
}

net::Packet numbered(net::Channel channel, u32 i) {
  BitWriter out;
  out.writeU32(i);
  return net::Packet(channel, out.take());
}

}  // namespace

TEST_CASE("simulated latency: nothing arrives before the delay, everything after it") {
  ConnectedPair pair = connectPair();
  pair.client.simulate({.latencyMs = 60});
  CHECK(pair.client.simulated().active());
  const auto sent = std::chrono::steady_clock::now();
  REQUIRE(pair.client.send(pair.toServer, numbered(net::Channel::Unreliable, 7)).has_value());
  REQUIRE(pair.client.send(pair.toServer, numbered(net::Channel::Reliable, 8)).has_value());
  pair.client.flush();  // too early: still held back
  std::vector<std::chrono::steady_clock::time_point> arrivals;
  REQUIRE(pump(pair.server, pair.client,
               [&](net::Host& host, const net::Event& e) {
                 if (&host == &pair.server && e.type == EventType::Received) arrivals.push_back(std::chrono::steady_clock::now());
               },
               [&] { return arrivals.size() == 2; }));
  for (const auto& at : arrivals) {
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(at - sent).count() >= 60);
  }
  CHECK(pair.client.simulationStats().delayed == 2);
}

TEST_CASE("simulated loss: about the asked share of unreliable packets is lost; reliable ones never, in order") {
  ConnectedPair pair = connectPair();
  pair.client.simulate({.latencyMs = 5, .jitterMs = 5, .lossPercent = 30.0f, .seed = 42});
  constexpr u32 kCount = 1000;
  for (u32 i = 0; i < kCount; ++i) {
    REQUIRE(pair.client.send(pair.toServer, numbered(net::Channel::Unreliable, i)).has_value());
  }
  for (u32 i = 0; i < 100; ++i) {
    REQUIRE(pair.client.send(pair.toServer, numbered(net::Channel::Reliable, i)).has_value());
  }
  u32 unreliable = 0;
  std::vector<u32> reliable;
  const auto onEvent = [&](net::Host& host, const net::Event& e) {
    if (&host != &pair.server || e.type != EventType::Received) return;
    if (e.packet.channel() == net::Channel::Unreliable) ++unreliable;
    else reliable.push_back(BitReader(e.packet.bytes()).readU32());
  };
  REQUIRE(pump(pair.server, pair.client, onEvent, [&] { return reliable.size() == 100; }));
  (void)pump(pair.server, pair.client, onEvent, [&] { return unreliable + pair.client.simulationStats().dropped >= kCount; });

  const u64 dropped = pair.client.simulationStats().dropped;
  INFO("dropped " << dropped << ", arrived " << unreliable);
  CHECK(dropped > 230);  // 30% of 1000 is 300; a fixed seed keeps it stable
  CHECK(dropped < 370);
  CHECK(unreliable + dropped == kCount);
  for (u32 i = 0; i < 100; ++i) CHECK(reliable[i] == i);
  CHECK(pair.client.simulationStats().reliableHeldBack > 0);  // some were "lost" and so came later, still in order
}

TEST_CASE("disconnectAfterSending: the last message arrives before the goodbye, even when delayed") {
  ConnectedPair pair = connectPair();
  pair.server.simulate({.latencyMs = 40});
  REQUIRE(pair.server.send(pair.toClient, numbered(net::Channel::Reliable, 99)).has_value());
  pair.server.disconnectAfterSending(pair.toClient);
  bool gotMessage = false;
  bool messageBeforeGoodbye = false;
  bool goodbye = false;
  REQUIRE(pump(pair.server, pair.client,
               [&](net::Host& host, const net::Event& e) {
                 if (&host != &pair.client) return;
                 if (e.type == EventType::Received) gotMessage = BitReader(e.packet.bytes()).readU32() == 99;
                 if (e.type == EventType::Disconnected) {
                   goodbye = true;
                   messageBeforeGoodbye = gotMessage;
                 }
               },
               [&] { return goodbye; }));
  CHECK(messageBeforeGoodbye);
}
