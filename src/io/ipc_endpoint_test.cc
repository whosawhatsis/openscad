// The connected channel a window and its private compute worker talk over (row 59).
//
// ipc_channel.h is the framing and knows nothing about transports. This is the other half: opening
// a real endpoint, accepting one connection on it, and connecting to it by name. The name is what
// the parent hands the child on its command line, which is why it has to be a plain string and why
// two windows must never produce the same one.
//
// The platform difference lives here and nowhere else. POSIX uses a path-based AF_UNIX socket;
// Windows uses a named pipe through asio::windows::stream_handle, because AF_UNIX there compiles
// (BOOST_ASIO_HAS_LOCAL_SOCKETS is defined from Boost 1.80) but fails at runtime under Asio's IOCP
// backend with WSAEOPNOTSUPP. These tests are written against the wrapper so they do not care which
// one is underneath.
//
// The peer here is a thread rather than a child process: this is a unit test, and what needs
// proving is that a second party can find the endpoint by name and exchange framed bytes over it.
// Whether the peer was forked is the process layer's problem, not the channel's.
//
// NOTE: Catch2 assertion state is not thread-safe, so the peer thread never uses REQUIRE or CHECK.
// It records what happened in a plain variable and the main thread asserts on it after join().
// Asserting from both threads at once corrupts Catch2 and aborts the run intermittently, which
// reads exactly like a flaky channel and is not.

#include "io/ipc_endpoint.h"

#include <catch2/catch_all.hpp>
#include <memory>
#include <string>
#include <thread>

#include "io/ipc_channel.h"

namespace {

std::string hostilePayload(std::size_t bytes)
{
  static const char pattern[] = {'\n', '\0', '\r', '\n', 'A', '\0', '\xff', 'Z'};
  std::string s;
  s.reserve(bytes);
  while (s.size() < bytes) s.append(pattern, sizeof pattern);
  s.resize(bytes);
  return s;
}

}  // namespace

TEST_CASE("A worker connects to its parent's endpoint by name", "[io][IPC][IPC-Endpoint]")
{
  IpcListener listener;
  REQUIRE_FALSE(listener.endpoint().empty());

  const std::string payload = hostilePayload(1 << 16);
  bool workerConnected = false;
  std::thread worker([endpoint = listener.endpoint(), &payload, &workerConnected] {
    const auto channel = IpcChannel::connect(endpoint);
    workerConnected = channel != nullptr;
    if (channel) channel->write("leaf/0.osig", payload);
  });

  const auto channel = listener.accept();
  REQUIRE(channel);
  IpcMessage message;
  REQUIRE(channel->read(message));
  CHECK(message.name == "leaf/0.osig");
  CHECK(message.payload == payload);

  worker.join();
  CHECK(workerConnected);
}

TEST_CASE("The channel carries messages in both directions", "[io][IPC][IPC-Endpoint]")
{
  // The worker sends payloads and the parent sends requests over the same connection, so this is
  // not a one-way pipe.
  IpcListener listener;
  bool workerEchoed = false;
  std::thread worker([endpoint = listener.endpoint(), &workerEchoed] {
    const auto channel = IpcChannel::connect(endpoint);
    if (!channel) return;
    IpcMessage request;
    if (!channel->read(request)) return;
    channel->write(request.name + ".done", request.payload);
    workerEchoed = true;
  });

  const auto channel = listener.accept();
  REQUIRE(channel);
  channel->write("render", hostilePayload(4096));

  IpcMessage reply;
  REQUIRE(channel->read(reply));
  CHECK(reply.name == "render.done");
  CHECK(reply.payload == hostilePayload(4096));

  worker.join();
  CHECK(workerEchoed);
}

TEST_CASE("Several payloads cross one connection in order", "[io][IPC][IPC-Endpoint]")
{
  // A single preview sends products.json plus one payload per distinct leaf, and products.json
  // refers to the leaves by name -- so both the order and the names have to survive.
  IpcListener listener;
  bool workerConnected = false;
  std::thread worker([endpoint = listener.endpoint(), &workerConnected] {
    const auto channel = IpcChannel::connect(endpoint);
    workerConnected = channel != nullptr;
    if (!channel) return;
    for (int i = 0; i < 8; ++i) {
      channel->write("leaf/" + std::to_string(i) + ".osig", hostilePayload(i * 1024));
    }
    channel->write("products.json", "{}");
  });

  const auto channel = listener.accept();
  REQUIRE(channel);
  for (int i = 0; i < 8; ++i) {
    IpcMessage message;
    REQUIRE(channel->read(message));
    CHECK(message.name == "leaf/" + std::to_string(i) + ".osig");
    CHECK(message.payload.size() == static_cast<std::size_t>(i) * 1024);
  }
  IpcMessage products;
  REQUIRE(channel->read(products));
  CHECK(products.name == "products.json");

  worker.join();
  CHECK(workerConnected);
}

TEST_CASE("Two listeners never share an endpoint name", "[io][IPC][IPC-Endpoint]")
{
  // Every window owns a private worker. Two windows colliding on one endpoint would cross their
  // geometry, which is the kind of bug that looks like a cache problem for a week.
  IpcListener first;
  IpcListener second;
  CHECK(first.endpoint() != second.endpoint());

  bool workerConnected = false;
  std::thread worker([endpoint = second.endpoint(), &workerConnected] {
    const auto channel = IpcChannel::connect(endpoint);
    workerConnected = channel != nullptr;
    if (channel) channel->write("second", "");
  });

  const auto channel = second.accept();
  REQUIRE(channel);
  IpcMessage message;
  REQUIRE(channel->read(message));
  CHECK(message.name == "second");

  worker.join();
  CHECK(workerConnected);
}

TEST_CASE("Connecting to an endpoint that is not there fails", "[io][IPC][IPC-Endpoint]")
{
  // A worker whose parent died between spawn and connect must not block forever holding a process
  // open; it has to be told there is nothing to talk to.
  CHECK(IpcChannel::connect("openscad-ipc-endpoint-that-does-not-exist") == nullptr);
}

TEST_CASE("A listener releases its endpoint when it is destroyed", "[io][IPC][IPC-Endpoint]")
{
  // On POSIX the endpoint is a filesystem path; leaking one per window per session would litter
  // the temporary directory. Reconnecting after the listener is gone must fail.
  std::string endpoint;
  {
    IpcListener listener;
    endpoint = listener.endpoint();
  }
  CHECK(IpcChannel::connect(endpoint) == nullptr);
}

TEST_CASE("A closed peer ends the channel rather than hanging", "[io][IPC][IPC-Endpoint]")
{
  // What a crashed worker looks like from the parent: the connection closes mid-conversation.
  // read() has to report that, so a crash surfaces as a diagnostic instead of a frozen window.
  IpcListener listener;
  bool workerConnected = false;
  std::thread worker([endpoint = listener.endpoint(), &workerConnected] {
    const auto channel = IpcChannel::connect(endpoint);
    workerConnected = channel != nullptr;
    if (channel) channel->write("only", "one");
    // and then goes away without saying goodbye
  });

  const auto channel = listener.accept();
  REQUIRE(channel);
  IpcMessage message;
  REQUIRE(channel->read(message));
  CHECK(message.name == "only");

  worker.join();
  CHECK(workerConnected);
  IpcMessage nothing;
  CHECK_FALSE(channel->read(nothing));
}
