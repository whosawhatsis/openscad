#include "io/ipc_endpoint.h"

#include <boost/asio/io_context.hpp>
#include <cstdint>
#include <sstream>
#include <exception>
#include <memory>
#include <string>

#include "io/ipc_channel.h"

#ifdef _WIN32
#include <boost/asio/windows/stream_handle.hpp>
#include <windows.h>
#else
#include <boost/asio/local/stream_protocol.hpp>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32

using Stream = boost::asio::windows::stream_handle;
using RawEnd = HANDLE;
const RawEnd kNoEnd = INVALID_HANDLE_VALUE;

void closeEnd(RawEnd end)
{
  if (end != kNoEnd) ::CloseHandle(end);
}

std::string endToArgument(RawEnd end)
{
  return std::to_string(reinterpret_cast<std::uintptr_t>(end));
}

#else

using Stream = boost::asio::local::stream_protocol::socket;
using RawEnd = int;
constexpr RawEnd kNoEnd = -1;

void closeEnd(RawEnd end)
{
  if (end != kNoEnd) ::close(end);
}

std::string endToArgument(RawEnd end)
{
  return std::to_string(end);
}

// macOS defaults AF_UNIX socket buffers small enough to cost 3.5x on a large payload; Linux
// autotunes and clamps this to wmem_max, where it is harmless. Measured, not guessed: a 200 MiB
// round trip went from 369 ms to 105 ms on macOS and did not move on Linux.
void tuneBuffers(RawEnd end)
{
  const int size = 1 << 20;
  ::setsockopt(end, SOL_SOCKET, SO_SNDBUF, &size, sizeof size);
  ::setsockopt(end, SOL_SOCKET, SO_RCVBUF, &size, sizeof size);
#ifdef SO_NOSIGPIPE
  // Writing to a worker that has already died must return an error, not raise SIGPIPE and take the
  // GUI down with it -- which would turn the one failure this feature exists to contain into a
  // worse one. Asio sets this itself, but only on sockets it creates or accepts; these come from
  // socketpair() and are adopted with assign(), so neither path runs. Linux needs nothing here:
  // Asio passes MSG_NOSIGNAL on every send where the platform has it, and macOS does not.
  const int on = 1;
  ::setsockopt(end, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
}

#endif

// One end. The io_context is owned here rather than shared, because a channel outlives the call
// that created it and nothing else needs to drive it.
class Channel : public IpcChannel
{
public:
  Channel() : stream(context) {}

  bool adopt(RawEnd end)
  {
    boost::system::error_code ec;
#ifdef _WIN32
    stream.assign(end, ec);
#else
    stream.assign(boost::asio::local::stream_protocol(), end, ec);
#endif
    return !ec;
  }

  void write(const std::string& name, const std::string& payload) override
  {
    write_ipc_message(stream, name, payload);
  }

  bool read(IpcMessage& message) override { return read_ipc_message(stream, message); }

  boost::asio::io_context context;
  Stream stream;
};

}  // namespace

struct IpcChannelPair::Private {
  std::unique_ptr<Channel> parentEnd;
  RawEnd childEnd = kNoEnd;
};

std::unique_ptr<IpcChannel> ipc_channel_from_argument(const std::string& argument)
{
  if (argument.empty()) return {};
  std::uintptr_t value = 0;
  try {
    std::size_t consumed = 0;
    value = static_cast<std::uintptr_t>(std::stoull(argument, &consumed));
    if (consumed != argument.size()) return {};
  } catch (const std::exception&) {
    // Not a number at all, or out of range. Either way the parent did not hand us this.
    return {};
  }

#ifdef _WIN32
  const RawEnd end = reinterpret_cast<RawEnd>(value);
#else
  const RawEnd end = static_cast<RawEnd>(value);
#endif
  if (end == kNoEnd) return {};
  auto channel = std::make_unique<Channel>();
  if (!channel->adopt(end)) return {};
  return channel;
}

IpcChannelPair::IpcChannelPair() : d(std::make_unique<Private>())
{
#ifdef _WIN32
  // A named pipe connected to itself. ConnectNamedPipe returns ERROR_PIPE_CONNECTED immediately
  // because the other end already exists, so nothing waits and nothing can race. The name is a
  // construction detail: the child is handed a handle, never the name.
  static std::uint64_t counter = 0;
  const std::string name = "\\\\.\\pipe\\openscad-ipc-" + std::to_string(::GetCurrentProcessId()) + "-" +
                           std::to_string(counter++);
  const RawEnd server =
    ::CreateNamedPipeA(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 1 << 16, 1 << 16, 0, nullptr);
  if (server == kNoEnd) return;
  // Inheritable: this is the end the child receives.
  SECURITY_ATTRIBUTES inheritable{sizeof inheritable, nullptr, TRUE};
  const RawEnd client = ::CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, &inheritable,
                                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
  if (client == kNoEnd) {
    closeEnd(server);
    return;
  }
  OVERLAPPED overlapped{};
  overlapped.hEvent = ::CreateEventA(nullptr, TRUE, FALSE, nullptr);
  ::ConnectNamedPipe(server, &overlapped);
  ::CloseHandle(overlapped.hEvent);
#else
  int ends[2];
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, ends) != 0) return;
  tuneBuffers(ends[0]);
  tuneBuffers(ends[1]);
  const RawEnd server = ends[0];
  const RawEnd client = ends[1];
#endif

  auto parentEnd = std::make_unique<Channel>();
  if (!parentEnd->adopt(server)) {
    closeEnd(server);
    closeEnd(client);
    return;
  }
  d->parentEnd = std::move(parentEnd);
  d->childEnd = client;
}

IpcChannelPair::~IpcChannelPair()
{
  releaseChildEnd();
}

bool IpcChannelPair::valid() const
{
  return d->parentEnd != nullptr;
}

IpcChannel& IpcChannelPair::parent()
{
  return *d->parentEnd;
}

std::string IpcChannelPair::childArgument() const
{
  if (!d->parentEnd || d->childEnd == kNoEnd) return {};
  return endToArgument(d->childEnd);
}

void IpcChannelPair::releaseChildEnd()
{
  closeEnd(d->childEnd);
  d->childEnd = kNoEnd;
}

namespace {

// One request at a time in one process; see the header for why this is process state.
IpcChannel *sinkChannel = nullptr;
std::string sinkName;
std::ostringstream sinkPayload;

void sendPending()
{
  if (!sinkChannel || sinkName.empty()) return;
  sinkChannel->write(sinkName, sinkPayload.str());
  sinkName.clear();
  sinkPayload.str({});
  sinkPayload.clear();
}

}  // namespace

namespace ipc_payload_sink {

bool collecting()
{
  return sinkChannel != nullptr;
}

void begin(IpcChannel& channel)
{
  sinkChannel = &channel;
  sinkName.clear();
  sinkPayload.str({});
  sinkPayload.clear();
}

void end()
{
  sendPending();
  sinkChannel = nullptr;
}

std::ostream& open(const std::string& name)
{
  // Sending the previous payload here rather than at the end of the request is what lets the
  // receiving side start work while the worker is still going.
  sendPending();
  sinkName = ipc_payload_name(name);
  return sinkPayload;
}

void flush_pending()
{
  sendPending();
}

}  // namespace ipc_payload_sink
