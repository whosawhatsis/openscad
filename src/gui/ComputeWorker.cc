#include "gui/ComputeWorker.h"

#include <QByteArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <exception>
#include <utility>

#include "io/ipc_channel.h"
#include "utils/printutils.h"
#include "io/ipc_endpoint.h"

struct ComputeWorker::Private {
  QString program;
  QStringList arguments;
  QString channelVariable;
  QProcess process;
  std::unique_ptr<IpcChannelPair> channel;
};

ComputeWorker::ComputeWorker(QString program, QStringList arguments, QString channelEnvironmentVariable)
  : d(std::make_unique<Private>())
{
  d->program = std::move(program);
  d->arguments = std::move(arguments);
  d->channelVariable = std::move(channelEnvironmentVariable);
}

ComputeWorker::~ComputeWorker()
{
  if (isRunning()) {
    cancel();
    d->process.waitForFinished(-1);
  }
}

bool ComputeWorker::start()
{
  if (d->channel) return false;

  auto channel = std::make_unique<IpcChannelPair>();
  if (!channel->valid()) return false;

  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(d->channelVariable, QString::fromStdString(channel->childArgument()));
  d->process.setProcessEnvironment(environment);
  d->process.start(d->program, d->arguments);
  if (!d->process.waitForStarted()) {
    // Worth saying out loud: without this a worker that cannot be started is indistinguishable
    // from one that started and said nothing.
    LOG(message_group::Error, "Could not start compute worker '%1$s': %2$s", d->program.toStdString(),
        d->process.errorString().toStdString());
    return false;
  }

  // The child has it now, so this process must not keep a copy. Holding one leaves the channel
  // open from this end forever, and a worker that dies would then never produce an end of stream
  // -- a hang where there should be a diagnostic. This ordering is the whole of it: after
  // waitForStarted(), before the first read.
  channel->releaseChildEnd();
  d->channel = std::move(channel);
  return true;
}

bool ComputeWorker::isRunning() const
{
  return d->process.state() == QProcess::Running;
}

qint64 ComputeWorker::processId() const
{
  return isRunning() ? d->process.processId() : 0;
}

bool ComputeWorker::send(const QString& name, const QByteArray& payload)
{
  if (!d->channel) return false;
  try {
    d->channel->parent().write(name.toStdString(), std::string(payload.constData(), payload.size()));
  } catch (const std::exception&) {
    // A worker that died between the check and the write. Its exit status is the honest report;
    // this only has to avoid pretending the message went anywhere.
    return false;
  }
  return true;
}

bool ComputeWorker::receive(IpcMessage& message)
{
  if (!d->channel) return false;
  return d->channel->parent().read(message);
}

void ComputeWorker::cancel()
{
  if (isRunning()) d->process.kill();
}

bool ComputeWorker::waitForFinished()
{
  if (d->process.state() == QProcess::NotRunning) return true;
  return d->process.waitForFinished();
}

bool ComputeWorker::exitedCleanly() const
{
  return d->process.exitStatus() == QProcess::NormalExit && d->process.exitCode() == 0;
}
