#include "io/ipc_endpoint.h"

#include <boost/asio/io_context.hpp>
#include <atomic>
#include <cstdint>
#include <string>

#include "io/ipc_channel.h"

#ifdef _WIN32
#include <boost/asio/windows/stream_handle.hpp>
#include <windows.h>
#else
#include <boost/asio/local/stream_protocol.hpp>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <filesystem>
#endif

namespace {

#ifdef _WIN32

using Stream = boost::asio::windows::stream_handle;

std::string endpointPath(const std::string& name)
{
  return "\\\\.\\pipe\\" + name;
}

#else

using Stream = boost::asio::local::stream_protocol::socket;

std::string endpointPath(const std::string& name)
{
  return (std::filesystem::temp_directory_path() / (name + ".sock")).string();
}

// macOS defaults AF_UNIX socket buffers small enough to cost 3.5x on a large payload; Linux
// autotunes and clamps this to wmem_max, where it is harmless. Measured, not guessed: a 200 MiB
// round trip went from 369 ms to 105 ms on macOS and did not move on Linux.
void tuneBuffers(Stream& socket)
{
  const int size = 1 << 20;
  ::setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDBUF, &size, sizeof size);
  ::setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVBUF, &size, sizeof size);
}

#endif

// Unique per listener and per process, so two windows -- or two OpenSCAD instances -- never publish
// the same endpoint. Crossing two windows' channels would look like a cache bug for a week.
std::string uniqueEndpointName()
{
  static std::atomic<std::uint64_t> counter{0};
#ifdef _WIN32
  const auto pid = static_cast<unsigned long>(::GetCurrentProcessId());
#else
  const auto pid = static_cast<unsigned long>(::getpid());
#endif
  return "openscad-" + std::to_string(pid) + "-" + std::to_string(counter.fetch_add(1));
}

// One connection. The io_context is owned here rather than shared, because a channel outlives the
// call that created it and nothing else needs to drive it.
class Channel : public IpcChannel
{
public:
  Channel() : stream(context) {}

  void write(const std::string& name, const std::string& payload) override
  {
    write_ipc_message(stream, name, payload);
  }

  bool read(IpcMessage& message) override { return read_ipc_message(stream, message); }

  boost::asio::io_context context;
  Stream stream;
};

}  // namespace

struct IpcListener::Private {
  std::string name;
  std::string path;
  boost::asio::io_context context;
#ifdef _WIN32
  HANDLE pipe = INVALID_HANDLE_VALUE;
#else
  boost::asio::local::stream_protocol::acceptor acceptor{context};
#endif
};

std::unique_ptr<IpcChannel> IpcChannel::connect(const std::string& endpoint)
{
  auto channel = std::make_unique<Channel>();
#ifdef _WIN32
  const HANDLE pipe = ::CreateFileA(endpointPath(endpoint).c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) return {};
  channel->stream.assign(pipe);
#else
  boost::system::error_code ec;
  channel->stream.connect(boost::asio::local::stream_protocol::endpoint(endpointPath(endpoint)), ec);
  if (ec) return {};
  tuneBuffers(channel->stream);
#endif
  return channel;
}

IpcListener::IpcListener() : d(std::make_unique<Private>())
{
  d->name = uniqueEndpointName();
  d->path = endpointPath(d->name);
#ifdef _WIN32
  // Duplex and overlapped: Asio's stream_handle drives the pipe through IOCP.
  d->pipe =
    ::CreateNamedPipeA(d->path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 1 << 16, 1 << 16, 0, nullptr);
#else
  // A stale socket file from a previous run would make bind() fail; the name is unique per process
  // so this only ever removes our own leftovers.
  std::remove(d->path.c_str());
  boost::system::error_code ec;
  d->acceptor.open();
  d->acceptor.bind(boost::asio::local::stream_protocol::endpoint(d->path), ec);
  d->acceptor.listen(1, ec);
#endif
}

IpcListener::~IpcListener()
{
#ifdef _WIN32
  if (d->pipe != INVALID_HANDLE_VALUE) ::CloseHandle(d->pipe);
#else
  boost::system::error_code ec;
  d->acceptor.close(ec);
  // The endpoint is a filesystem path on POSIX. Leaking one per window per session would litter
  // the temporary directory.
  std::remove(d->path.c_str());
#endif
}

const std::string& IpcListener::endpoint() const
{
  return d->name;
}

std::unique_ptr<IpcChannel> IpcListener::accept()
{
  auto channel = std::make_unique<Channel>();
#ifdef _WIN32
  if (d->pipe == INVALID_HANDLE_VALUE) return {};
  OVERLAPPED overlapped{};
  overlapped.hEvent = ::CreateEventA(nullptr, TRUE, FALSE, nullptr);
  const bool connected = ::ConnectNamedPipe(d->pipe, &overlapped) ||
                         (::GetLastError() == ERROR_IO_PENDING &&
                          ::WaitForSingleObject(overlapped.hEvent, INFINITE) == WAIT_OBJECT_0) ||
                         ::GetLastError() == ERROR_PIPE_CONNECTED;
  ::CloseHandle(overlapped.hEvent);
  if (!connected) return {};
  channel->stream.assign(d->pipe);
  // The channel owns the handle from here; the listener must not close it twice.
  d->pipe = INVALID_HANDLE_VALUE;
#else
  boost::system::error_code ec;
  // Accept onto the channel's own io_context. The peer-socket overload would bind the new socket
  // to the acceptor's context instead, which the channel outlives.
  channel->stream = d->acceptor.accept(channel->context, ec);
  if (ec) return {};
  tuneBuffers(channel->stream);
#endif
  return channel;
}
