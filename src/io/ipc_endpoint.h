#pragma once

#include <memory>
#include <string>

struct IpcMessage;

// The connected channel a window and its private compute worker talk over.
//
// io/ipc_channel.h is the framing and knows nothing about transports; this is the other half.
// The platform difference lives here and nowhere else: POSIX uses a path-based AF_UNIX socket,
// Windows a named pipe. AF_UNIX is not an option on Windows even though Boost defines
// BOOST_ASIO_HAS_LOCAL_SOCKETS there from 1.80 onward -- it compiles and then fails at runtime
// under Asio's IOCP backend with WSAEOPNOTSUPP, because Windows AF_UNIX does not support the
// overlapped sockets Asio creates. Do not gate on that macro; the fallback is not conditional.
//
// Asio rather than Qt because the worker end compiles in HEADLESS builds where Qt is not linked,
// and because a CLI-side parent needs the same channel.

/*!
   One connection. Reads and writes block, which is what the worker wants on its own thread and
   what the parent wants on the thread it dedicates to a request.
 */
class IpcChannel
{
public:
  virtual ~IpcChannel() = default;

  //! Connects to an endpoint by the name its listener published. Null if nothing is listening --
  //! a worker whose parent died between spawn and connect has to be told, not left blocking.
  static std::unique_ptr<IpcChannel> connect(const std::string& endpoint);

  virtual void write(const std::string& name, const std::string& payload) = 0;
  //! False at the end of the channel, including the abrupt end a crashed peer leaves behind.
  virtual bool read(IpcMessage& message) = 0;
};

/*!
   Publishes an endpoint and accepts the one connection made to it. Every window owns a private
   worker, so every listener gets a name of its own; two windows sharing one would cross their
   geometry. The endpoint is released when the listener is destroyed.
 */
class IpcListener
{
public:
  IpcListener();
  ~IpcListener();
  IpcListener(const IpcListener&) = delete;
  IpcListener& operator=(const IpcListener&) = delete;

  //! The name to hand the worker on its command line.
  [[nodiscard]] const std::string& endpoint() const;

  //! Waits for the worker to connect. Null if it never does.
  std::unique_ptr<IpcChannel> accept();

private:
  struct Private;
  std::unique_ptr<Private> d;
};
