#pragma once

#include <memory>
#include <string>

struct IpcMessage;

// The connected channel a window and its private compute worker talk over.
//
// io/ipc_channel.h is the framing and knows nothing about transports; this is the other half.
// The platform difference lives here and nowhere else:
//
//   POSIX   socketpair(AF_UNIX, SOCK_STREAM) -- a pre-connected pair, one end inherited by the
//           child across fork/exec, identified to it by descriptor number.
//   Windows no socketpair in Winsock, so a named pipe connected to itself: CreateNamedPipe then
//           CreateFile to that same name, yielding two connected OVERLAPPED handles. The name is
//           a construction detail the child never sees -- its handle is created inheritable and
//           identified to it by handle value, exactly like a descriptor.
//
// Deliberately no listener, no published endpoint name and no accept(). It is less machinery --
// nothing to name uniquely, nothing to clean up, and no window between spawning the child and it
// connecting -- and it avoids Asio's accept(), which is unusable for AF_UNIX on macOS: it fails
// with EINVAL there while succeeding for TCP in the identical pattern, on Boost 1.74 and 1.90
// alike, while a raw ::accept() on Asio's own descriptor works.
//
// Asio rather than Qt because the worker end compiles in HEADLESS builds where Qt is not linked,
// and because a CLI-side parent needs the same channel.

/*!
   One end of the channel. Reads and writes block, which is what the worker wants on its own thread
   and what the parent wants on the thread it dedicates to a request.
 */
class IpcChannel
{
public:
  virtual ~IpcChannel() = default;

  virtual void write(const std::string& name, const std::string& payload) = 0;
  //! False at the end of the channel, including the abrupt end a crashed peer leaves behind.
  virtual bool read(IpcMessage& message) = 0;
};

/*!
   Adopts the far end of a pair from the string its parent passed on the command line. Null if the
   string names nothing usable -- a worker handed a descriptor its parent never opened has to be
   told, not left blocking on a channel that will never carry anything.
 */
std::unique_ptr<IpcChannel> ipc_channel_from_argument(const std::string& argument);

/*!
   A connected pair: the end this process keeps, and the end its child will adopt. Already connected
   when constructed, so there is no failure mode between spawning the child and it arriving.
 */
class IpcChannelPair
{
public:
  IpcChannelPair();
  ~IpcChannelPair();
  IpcChannelPair(const IpcChannelPair&) = delete;
  IpcChannelPair& operator=(const IpcChannelPair&) = delete;

  //! False if the pair could not be created at all; nothing else is usable then.
  [[nodiscard]] bool valid() const;

  //! This process's end.
  IpcChannel& parent();

  //! What to hand the child on its command line. Empty if the pair is not valid.
  [[nodiscard]] std::string childArgument() const;

  /*!
     Drops this process's copy of the child's end. Call it once the child has been spawned and has
     inherited that end: until then the descriptor must stay open, and afterwards it must not, or
     the parent never sees end-of-stream when the child dies -- its own copy would hold the channel
     open forever, which is a hang rather than a diagnostic.
   */
  void releaseChildEnd();

private:
  struct Private;
  std::unique_ptr<Private> d;
};
