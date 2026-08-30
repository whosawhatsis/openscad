// The connected channel a window and its private compute worker talk over (row 59).
//
// io/ipc_channel.h is the framing and knows nothing about transports. This is the other half: a
// pre-connected pair of ends, one of which the parent keeps and one of which it hands to the child
// it spawns. The child's end is identified by a plain string -- a file descriptor number on POSIX,
// a handle value on Windows -- which is what goes on its command line.
//
// There is deliberately no listener, no endpoint name and no accept(). Two reasons. It is less
// machinery: nothing to name uniquely, nothing to clean up, and no window between spawning the
// child and it connecting. And Asio's accept() is unusable for AF_UNIX on macOS -- it fails with
// EINVAL while succeeding for TCP in the identical pattern, on Boost 1.74 and 1.90 alike, while a
// raw ::accept() on Asio's own descriptor works. A pre-connected pair never goes near that path.
//
// The peer here is a thread rather than a child process: this is a unit test, and what needs
// proving is that the far end of the pair can be adopted from its string and exchange framed bytes.
// Whether it was forked is the process layer's problem, not the channel's.
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

TEST_CASE("A channel pair is connected the moment it is made", "[io][IPC][IPC-Endpoint]")
{
  IpcChannelPair pair;
  REQUIRE(pair.valid());
  REQUIRE_FALSE(pair.childArgument().empty());

  const std::string payload = hostilePayload(1 << 16);
  bool childAdopted = false;
  std::thread child([argument = pair.childArgument(), &payload, &childAdopted] {
    const auto channel = ipc_channel_from_argument(argument);
    childAdopted = channel != nullptr;
    if (channel) channel->write("leaf/0.osig", payload);
  });

  IpcMessage message;
  REQUIRE(pair.parent().read(message));
  CHECK(message.name == "leaf/0.osig");
  CHECK(message.payload == payload);

  child.join();
  CHECK(childAdopted);
}

TEST_CASE("The channel carries messages in both directions", "[io][IPC][IPC-Endpoint]")
{
  // The worker sends payloads and the parent sends requests over the same connection, so this is
  // not a one-way pipe.
  IpcChannelPair pair;
  REQUIRE(pair.valid());

  bool childEchoed = false;
  std::thread child([argument = pair.childArgument(), &childEchoed] {
    const auto channel = ipc_channel_from_argument(argument);
    if (!channel) return;
    IpcMessage request;
    if (!channel->read(request)) return;
    channel->write(request.name + ".done", request.payload);
    childEchoed = true;
  });

  pair.parent().write("render", hostilePayload(4096));
  IpcMessage reply;
  REQUIRE(pair.parent().read(reply));
  CHECK(reply.name == "render.done");
  CHECK(reply.payload == hostilePayload(4096));

  child.join();
  CHECK(childEchoed);
}

TEST_CASE("Several payloads cross one channel in order", "[io][IPC][IPC-Endpoint]")
{
  // A single preview sends products.json plus one payload per distinct leaf, and products.json
  // refers to the leaves by name -- so both the order and the names have to survive.
  IpcChannelPair pair;
  REQUIRE(pair.valid());

  bool childAdopted = false;
  std::thread child([argument = pair.childArgument(), &childAdopted] {
    const auto channel = ipc_channel_from_argument(argument);
    childAdopted = channel != nullptr;
    if (!channel) return;
    for (int i = 0; i < 8; ++i) {
      channel->write("leaf/" + std::to_string(i) + ".osig", hostilePayload(i * 1024));
    }
    channel->write("products.json", "{}");
  });

  for (int i = 0; i < 8; ++i) {
    IpcMessage message;
    REQUIRE(pair.parent().read(message));
    CHECK(message.name == "leaf/" + std::to_string(i) + ".osig");
    CHECK(message.payload.size() == static_cast<std::size_t>(i) * 1024);
  }
  IpcMessage products;
  REQUIRE(pair.parent().read(products));
  CHECK(products.name == "products.json");

  child.join();
  CHECK(childAdopted);
}

TEST_CASE("Two pairs are independent", "[io][IPC][IPC-Endpoint]")
{
  // Every window owns a private worker. Two windows sharing a channel would cross their geometry,
  // which is the kind of bug that looks like a cache problem for a week.
  IpcChannelPair first;
  IpcChannelPair second;
  REQUIRE(first.valid());
  REQUIRE(second.valid());
  CHECK(first.childArgument() != second.childArgument());

  bool bothAdopted = false;
  std::thread children([a = first.childArgument(), b = second.childArgument(), &bothAdopted] {
    const auto one = ipc_channel_from_argument(a);
    const auto two = ipc_channel_from_argument(b);
    bothAdopted = one && two;
    if (!bothAdopted) return;
    two->write("to-second", "");
    one->write("to-first", "");
  });

  IpcMessage onFirst;
  IpcMessage onSecond;
  REQUIRE(first.parent().read(onFirst));
  REQUIRE(second.parent().read(onSecond));
  CHECK(onFirst.name == "to-first");
  CHECK(onSecond.name == "to-second");

  children.join();
  CHECK(bothAdopted);
}

TEST_CASE("An argument that names nothing is refused", "[io][IPC][IPC-Endpoint]")
{
  // A worker handed a descriptor its parent never opened must be told, not left blocking on a
  // channel that will never carry anything.
  CHECK(ipc_channel_from_argument("") == nullptr);
  CHECK(ipc_channel_from_argument("not-a-number") == nullptr);
  CHECK(ipc_channel_from_argument("-1") == nullptr);
}

TEST_CASE("A closed peer ends the channel rather than hanging", "[io][IPC][IPC-Endpoint]")
{
  // What a crashed worker looks like from the parent: the far end closes mid-conversation. read()
  // has to report that, so a crash surfaces as a diagnostic instead of a frozen window.
  IpcChannelPair pair;
  REQUIRE(pair.valid());

  bool childAdopted = false;
  std::thread child([argument = pair.childArgument(), &childAdopted] {
    const auto channel = ipc_channel_from_argument(argument);
    childAdopted = channel != nullptr;
    if (channel) channel->write("only", "one");
    // and then goes away without saying goodbye
  });

  IpcMessage message;
  REQUIRE(pair.parent().read(message));
  CHECK(message.name == "only");

  child.join();
  CHECK(childAdopted);
  // The parent's own copy of the child end has to be gone as well, or the channel never reports
  // end-of-stream: the descriptor would still be open in this process.
  pair.releaseChildEnd();
  IpcMessage nothing;
  CHECK_FALSE(pair.parent().read(nothing));
}
