// Framing for the compute worker's payload channel (row 59).
//
// These tests are deliberately transport-free. The channel itself is a Boost.Asio stream --
// a path-based AF_UNIX socket on POSIX, a named pipe via asio::windows::stream_handle on Windows,
// because AF_UNIX there compiles but fails at runtime under Asio's IOCP backend with
// WSAEOPNOTSUPP. Rather than open a socket in a unit test, the framing is templated on the stream
// type and exercised here over an in-memory stream that satisfies the same Asio concepts. That
// keeps the test identical on all three platforms and keeps the transport out of it.

#include "io/ipc_channel.h"

#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>
#include <catch2/catch_all.hpp>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>

namespace {

// The smallest thing satisfying Asio's SyncReadStream and SyncWriteStream: bytes written land in
// a buffer, bytes read come back out of it. `short_reads` makes read_some hand back one byte at a
// time, which is what proves the reader assembles a message across partial reads rather than
// assuming one read per message.
class MemoryStream
{
public:
  explicit MemoryStream(bool short_reads = false) : short_reads_(short_reads) {}

  template <typename ConstBufferSequence>
  std::size_t write_some(const ConstBufferSequence& buffers, boost::system::error_code& ec)
  {
    ec = {};
    std::size_t written = 0;
    for (auto it = boost::asio::buffer_sequence_begin(buffers);
         it != boost::asio::buffer_sequence_end(buffers); ++it) {
      buffer_.append(static_cast<const char *>(it->data()), it->size());
      written += it->size();
    }
    return written;
  }

  template <typename ConstBufferSequence>
  std::size_t write_some(const ConstBufferSequence& buffers)
  {
    boost::system::error_code ec;
    return write_some(buffers, ec);
  }

  template <typename MutableBufferSequence>
  std::size_t read_some(const MutableBufferSequence& buffers, boost::system::error_code& ec)
  {
    ec = {};
    std::size_t read = 0;
    for (auto it = boost::asio::buffer_sequence_begin(buffers);
         it != boost::asio::buffer_sequence_end(buffers); ++it) {
      const std::size_t want = short_reads_ ? std::min<std::size_t>(1, it->size()) : it->size();
      const std::size_t have = std::min(want, buffer_.size() - read_pos_);
      if (have == 0) {
        if (read == 0) ec = boost::asio::error::eof;
        return read;
      }
      std::memcpy(it->data(), buffer_.data() + read_pos_, have);
      read_pos_ += have;
      read += have;
      if (short_reads_) return read;
    }
    return read;
  }

  template <typename MutableBufferSequence>
  std::size_t read_some(const MutableBufferSequence& buffers)
  {
    boost::system::error_code ec;
    return read_some(buffers, ec);
  }

  std::string& buffer() { return buffer_; }
  void corruptLengthPrefix(std::uint64_t value)
  {
    REQUIRE(buffer_.size() >= 16);
    std::memcpy(buffer_.data() + 8, &value, sizeof value);
  }

private:
  std::string buffer_;
  std::size_t read_pos_ = 0;
  bool short_reads_ = false;
};

// The bytes that broke the previous carrier. Payloads shared a line-oriented stdout, so an
// embedded newline split a message in half; these are the exact bytes that has to survive now.
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

TEST_CASE("IPC framing round-trips arbitrary binary payloads", "[io][IPC]")
{
  SECTION("a payload full of newlines and nulls survives byte-exact")
  {
    MemoryStream stream;
    const std::string payload = hostilePayload(4096);
    write_ipc_message(stream, "leaf/0.osig", payload);

    IpcMessage message;
    REQUIRE(read_ipc_message(stream, message));
    CHECK(message.name == "leaf/0.osig");
    CHECK(message.payload == payload);
  }

  SECTION("an empty payload is a message, not an end of stream")
  {
    MemoryStream stream;
    write_ipc_message(stream, "products.json", "");

    IpcMessage message;
    REQUIRE(read_ipc_message(stream, message));
    CHECK(message.name == "products.json");
    CHECK(message.payload.empty());
  }

  SECTION("several messages come back in order")
  {
    MemoryStream stream;
    write_ipc_message(stream, "a", hostilePayload(1));
    write_ipc_message(stream, "b", hostilePayload(65536));
    write_ipc_message(stream, "c", hostilePayload(0));

    IpcMessage message;
    REQUIRE(read_ipc_message(stream, message));
    CHECK(message.name == "a");
    REQUIRE(read_ipc_message(stream, message));
    CHECK(message.name == "b");
    CHECK(message.payload.size() == 65536);
    REQUIRE(read_ipc_message(stream, message));
    CHECK(message.name == "c");
    CHECK_FALSE(read_ipc_message(stream, message));
  }

  SECTION("a message split across single-byte reads is still assembled")
  {
    MemoryStream stream(/*short_reads=*/true);
    const std::string payload = hostilePayload(300);
    write_ipc_message(stream, "leaf/split.osig", payload);

    IpcMessage message;
    REQUIRE(read_ipc_message(stream, message));
    CHECK(message.payload == payload);
  }
}

TEST_CASE("IPC framing rejects a corrupt length prefix instead of trusting it", "[io][IPC]")
{
  // The shape a worker killed mid-write leaves behind. Without a ceiling this reaches resize()
  // as an allocation the size of the claimed length.
  SECTION("an oversize payload length is refused")
  {
    MemoryStream stream;
    write_ipc_message(stream, "leaf/0.osig", hostilePayload(64));
    stream.corruptLengthPrefix(kIpcMaxMessageSize + 1);

    IpcMessage message;
    CHECK_FALSE(read_ipc_message(stream, message));
  }

  SECTION("a truncated stream is refused rather than returning a short payload")
  {
    MemoryStream stream;
    write_ipc_message(stream, "leaf/0.osig", hostilePayload(4096));
    stream.buffer().resize(stream.buffer().size() / 2);

    IpcMessage message;
    CHECK_FALSE(read_ipc_message(stream, message));
  }
}

TEST_CASE("Payload names are canonical across the process boundary", "[io][IPC]")
{
  // The two ends reach a name by different routes that spell separators differently on Windows:
  // geometry names arrive via fs::path::generic_string(), metadata sidecars via plain
  // concatenation of whatever the caller sent. Folding both to '/' is what stops them drifting.
  CHECK(ipc_payload_name("dir\\leaf.osig") == "dir/leaf.osig");
  CHECK(ipc_payload_name("dir/leaf.osig") == "dir/leaf.osig");
  CHECK(ipc_payload_name("a\\b/c\\d") == "a/b/c/d");
  CHECK(ipc_payload_name("") == "");

  SECTION("a name written one way is found when looked up the other way")
  {
    MemoryStream stream;
    write_ipc_message(stream, ipc_payload_name("out\\leaf.osig"), "x");

    IpcMessage message;
    REQUIRE(read_ipc_message(stream, message));
    CHECK(message.name == ipc_payload_name("out/leaf.osig"));
  }
}
