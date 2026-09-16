#pragma once

#include <boost/asio/buffer.hpp>
#include <boost/asio/completion_condition.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// Framing for the compute worker's payload channel.
//
// The worker returns geometry over a dedicated channel rather than writing it to files. That
// channel is a Boost.Asio stream -- Asio is already a dependency of this project (see
// src/core/HTTPClient.cc) and is header-only for this use, and unlike Qt it is available in
// HEADLESS builds, which the worker end has to compile in. It is also what lets a CLI-side parent
// use the same worker.
//
// Framing exists because a stream socket has no message boundaries, not because the payloads are
// awkward: they are arbitrary binary and routinely contain '\n' and '\0', but nothing here has to
// tiptoe around that, since the channel carries nothing else. A length prefix plus
// asio::transfer_exactly is the whole of it -- transfer_exactly is what assembles a message across
// partial reads, so there is no incremental reader and no buffering to get wrong.
//
// Lengths are native-width and native byte order. The two ends are always the same executable on
// the same machine, since the worker is launched from the parent's own path. Do not reuse this
// framing for anything that outlives the pair of processes.

// Ceilings exist so a corrupt length prefix -- the shape a worker killed mid-write leaves behind --
// is rejected instead of being handed to resize(). Both are far above anything real: the largest
// payload measured on this project is 191 MiB, and names are filesystem paths.
inline constexpr std::uint64_t kIpcMaxMessageSize = 16ull * 1024 * 1024 * 1024;
inline constexpr std::uint64_t kIpcMaxNameSize = 4096;

struct IpcMessage {
  std::string name;
  std::string payload;
};

// Canonical form of a payload name. Payload names are identifiers, never opened, and the two ends
// reach them by different routes that spell separators differently on Windows: a geometry name
// arrives via fs::path::generic_string() with forward slashes, a metadata sidecar via plain
// concatenation of whatever the caller sent. Folding both to '/' here, and using this on the
// writing and the reading side alike, is what stops them drifting apart.
inline std::string ipc_payload_name(std::string name)
{
  std::replace(name.begin(), name.end(), '\\', '/');
  return name;
}

// A message is named because a single preview sends several: products.json plus one payload per
// distinct leaf PolySet, and products.json refers to the leaves by the path the worker would have
// written. Carrying that path as the name lets the receiving side resolve those references from a
// map instead of the filesystem.
template <typename SyncWriteStream>
void write_ipc_message(SyncWriteStream& stream, const std::string& name, const std::string& payload)
{
  const std::uint64_t nameSize = name.size();
  const std::uint64_t payloadSize = payload.size();
  const std::vector<boost::asio::const_buffer> buffers{
    boost::asio::buffer(&nameSize, sizeof nameSize),
    boost::asio::buffer(&payloadSize, sizeof payloadSize), boost::asio::buffer(name),
    boost::asio::buffer(payload)};
  boost::asio::write(stream, buffers);
}

/*!
   Reads one message. False means no more messages: a clean end of stream, a truncated one, or a
   length prefix that cannot be real. The caller cannot distinguish those and does not need to --
   all three mean the channel is finished, and a worker that died mid-write is reported through its
   exit status rather than through this.
 */
template <typename SyncReadStream>
bool read_ipc_message(SyncReadStream& stream, IpcMessage& message)
{
  std::uint64_t nameSize = 0;
  std::uint64_t payloadSize = 0;
  boost::system::error_code ec;

  boost::asio::read(stream, boost::asio::buffer(&nameSize, sizeof nameSize),
                    boost::asio::transfer_exactly(sizeof nameSize), ec);
  if (ec) return false;
  boost::asio::read(stream, boost::asio::buffer(&payloadSize, sizeof payloadSize),
                    boost::asio::transfer_exactly(sizeof payloadSize), ec);
  if (ec) return false;
  // Before either resize(), so a bogus length is never an allocation.
  if (nameSize > kIpcMaxNameSize || payloadSize > kIpcMaxMessageSize) return false;

  message.name.resize(nameSize);
  message.payload.resize(payloadSize);
  if (nameSize) {
    boost::asio::read(stream, boost::asio::buffer(message.name.data(), nameSize),
                      boost::asio::transfer_exactly(nameSize), ec);
    if (ec) return false;
  }
  if (payloadSize) {
    boost::asio::read(stream, boost::asio::buffer(message.payload.data(), payloadSize),
                      boost::asio::transfer_exactly(payloadSize), ec);
    if (ec) return false;
  }
  return true;
}
