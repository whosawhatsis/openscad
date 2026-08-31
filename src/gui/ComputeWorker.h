#pragma once

#include <QObject>
#include <memory>
#include <QString>
#include <QStringList>
#include <memory>

class QByteArray;
struct IpcMessage;

/*!
   A window's private compute worker: a child process that evaluates geometry, connected to this
   one by a pre-connected channel (see io/ipc_endpoint.h).

   The point of the separation is containment. A model that exhausts memory or trips an assertion
   kills the child, not the window, and the parent finds out because its end of the channel
   finishes rather than blocking. Anything here that could turn that into a hang is a defect in the
   feature's whole reason for existing.

   Reads and writes block. The GUI drives this from a worker thread; a blocking core is simpler to
   reason about than a half-finished asynchronous one, and it matches the channel underneath.
 */
class ComputeWorker : public QObject
{
  Q_OBJECT;

public:
  /*!
     `program` and `arguments` are what to spawn. `channelEnvironmentVariable` names the environment
     variable through which the child is told its end of the channel, keeping it off the command
     line where it would be visible to anything listing processes.
   */
  ComputeWorker(QString program, QStringList arguments, QString channelEnvironmentVariable);
  ~ComputeWorker() override;

  //! Creates the channel, spawns the child and hands it its end. False if it could not be started.
  bool start();

  [[nodiscard]] bool isRunning() const;
  //! 0 when no child is running.
  [[nodiscard]] qint64 processId() const;

  //! False if there is no worker to send to, or if the channel is already finished.
  bool send(const QString& name, const QByteArray& payload);
  //! False at the end of the channel, which includes the abrupt end a crashed worker leaves.
  bool receive(IpcMessage& message);

  //! Kills the child. It is not asked politely: cancellation has to work while it is busy.
  void cancel();

  bool waitForFinished();
  //! False for a crash or a kill, true only for a child that exited zero of its own accord.
  [[nodiscard]] bool exitedCleanly() const;

  /*!
     Asks for a render and returns immediately. The answer arrives as `renderDone` or
     `renderFailed`, both emitted on the caller's thread.

     Blocking here is not an option: the GUI thread cannot wait on a channel, and isolating the
     computation only to freeze the window it was meant to protect would be worse than not
     isolating it at all. This mirrors what CGALWorker does for the in-process path.
   */
  void startRender(const QString& scadPath, const QString& parameterFile, const QString& setName);

signals:
  //! The geometry a render produced. Never null: a failure comes through renderFailed instead.
  void renderDone(std::shared_ptr<const class Geometry>);
  //! Always emitted if renderDone is not, so a window is never left waiting on a signal that will
  //! never come.
  void renderFailed(QString reason);

private:
  //! Runs on the caller's thread; turns the worker's answer into one signal or the other.
  void finishRender(const std::shared_ptr<const class Geometry>& geometry, const QString& error);

  struct Private;
  std::unique_ptr<Private> d;
};
