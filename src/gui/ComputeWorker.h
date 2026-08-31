#pragma once

#include <QObject>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <QString>
#include <QStringList>
#include <memory>

class QByteArray;
class CsgInfo;
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

  /*!
     Asks the child to abandon the request it is running, and kills it if it does not.

     The polite form is worth trying first because the child keeps its geometry caches, which is
     most of what a per-window worker buys; a kill throws them away. It cannot be relied on alone,
     though -- a model can spend minutes inside one boolean without ever reaching the point where
     the request is noticed -- so this escalates to `cancel()` rather than leaving a window unable
     to stop what it started.
   */
  void cancelRequest();

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
  void startRender(const QString& scadPath, const QString& parameterFile, const QString& setName,
                   const QString& sourcePath = {});

  /*!
     Asks for a preview. Answers as `previewDone` or `previewFailed`.

     A preview is not a mesh: it is the CSG product list the window composites, plus the mesh for
     each leaf. `normalizationLimit` is the window's OpenCSG limit, which decides how far the
     product list is normalized -- the worker cannot know it.
   */
  void startPreview(const QString& scadPath, const QString& parameterFile, const QString& setName,
                    const QString& sourcePath, std::size_t normalizationLimit);

signals:
  //! The geometry a render produced. Never null: a failure comes through renderFailed instead.
  void renderDone(std::shared_ptr<const class Geometry>);
  //! Always emitted if renderDone is not, so a window is never left waiting on a signal that will
  //! never come.
  void renderFailed(QString reason);

  //! The product lists a preview produced, with every leaf already resolved from its payload.
  void previewDone(std::shared_ptr<CsgInfo>);
  void previewFailed(QString reason);

private:
  /*!
     Sends a request on a thread of its own and delivers the result back on the caller's thread.

     `deliver` is called exactly once, with the payloads that arrived and the worker's answer, or
     with an error. Exactly once is the contract that keeps a window from waiting forever.
   */
  void startRequest(const std::string& request,
                    std::function<void(std::map<std::string, std::string>&&, const QString&)> deliver);
  //! Lets go of the finished request's thread. Runs on the caller's thread.
  void requestFinished();

  struct Private;
  std::unique_ptr<Private> d;
};
