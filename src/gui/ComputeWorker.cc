#include "gui/ComputeWorker.h"

#include <QByteArray>
#include <QMetaObject>
#include <QProcess>
#include <QThread>
#include <QProcessEnvironment>
#include <cstdio>
#include <exception>
#include <functional>
#include <map>
#include <utility>

#include "geometry/Geometry.h"
#include "glview/CsgInfo.h"
#include "io/ipc_channel.h"
#include "io/ipc_geometry.h"
#include "json/json.hpp"
#include "utils/printutils.h"
#include "io/ipc_endpoint.h"

namespace {

//! Names the payload the worker returns geometry under. The request asks for this name and the
//! reply carries it back, so the two only have to agree here.
constexpr auto kRenderOutputName = "render.osig";
//! And the product list a preview returns; its leaves are named after it.
constexpr auto kPreviewOutputName = "preview.json";

}  // namespace

struct ComputeWorker::Private {
  QString program;
  QStringList arguments;
  QString channelVariable;
  QProcess process;
  std::unique_ptr<IpcChannelPair> channel;
  //! One request at a time per worker, run off the GUI thread.
  QThread *renderThread = nullptr;
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
  if (d->renderThread) {
    // The thread is blocked in read() until the child goes away, so the child has to go first.
    if (isRunning()) cancel();
    d->renderThread->quit();
    d->renderThread->wait();
  }
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

void ComputeWorker::startRequest(
  const std::string& request,
  std::function<void(std::map<std::string, std::string>&&, const QString&)> deliver)
{
  d->renderThread = QThread::create([this, request, deliver = std::move(deliver)] {
    const QByteArray payload(request.data(), static_cast<int>(request.size()));
    if (!send("request", payload)) {
      QMetaObject::invokeMethod(this, [this, deliver] { deliver({}, tr("The worker went away.")); });
      return;
    }

    // Payloads arrive before the answer that ends the request, so they are collected as they come.
    std::map<std::string, std::string> payloads;
    IpcMessage message;
    while (receive(message)) {
      if (message.name != "done") {
        payloads.emplace(std::move(message.name), std::move(message.payload));
        continue;
      }
      QString error;
      try {
        const auto answer = nlohmann::json::parse(message.payload);
        if (!answer.value("ok", false)) {
          error = QString::fromStdString(answer.value("error", std::string{"The request failed."}));
        }
      } catch (const std::exception& e) {
        error = QString::fromLatin1(e.what());
      }
      QMetaObject::invokeMethod(this, [this, deliver, payloads = std::move(payloads), error]() mutable {
        deliver(std::move(payloads), error);
      });
      return;
    }
    // The channel ended without an answer, which is what a crashed worker looks like from here.
    QMetaObject::invokeMethod(this, [this, deliver] { deliver({}, tr("The compute worker stopped.")); });
  });
  connect(d->renderThread, &QThread::finished, d->renderThread, &QObject::deleteLater);
  d->renderThread->start();
}

void ComputeWorker::requestFinished()
{
  if (!d->renderThread) return;
  d->renderThread->quit();
  d->renderThread->wait();
  d->renderThread = nullptr;
}

void ComputeWorker::startRender(const QString& scadPath, const QString& parameterFile,
                                const QString& setName, const QString& sourcePath)
{
  if (!d->channel) {
    emit renderFailed(tr("The compute worker is not running."));
    return;
  }
  if (d->renderThread) {
    emit renderFailed(tr("The compute worker is already busy."));
    return;
  }

  nlohmann::json request;
  request["command"] = "render";
  request["input"] = scadPath.toStdString();
  request["output"] = kRenderOutputName;
  if (!parameterFile.isEmpty()) request["parameterFile"] = parameterFile.toStdString();
  if (!setName.isEmpty()) request["setName"] = setName.toStdString();
  if (!sourcePath.isEmpty()) request["sourcePath"] = sourcePath.toStdString();

  startRequest(
    request.dump(), [this](std::map<std::string, std::string>&& payloads, const QString& error) {
      requestFinished();
      if (!error.isEmpty()) {
        emit renderFailed(error);
        return;
      }
      const auto found = payloads.find(kRenderOutputName);
      if (found == payloads.end()) {
        emit renderFailed(tr("The worker returned no geometry."));
        return;
      }
      const auto geometry =
        import_ipc_geometry_buffer(found->second.data(), found->second.size(), kRenderOutputName);
      if (!geometry) emit renderFailed(tr("The worker returned geometry that could not be read."));
      else emit renderDone(geometry);
    });
}

void ComputeWorker::startPreview(const QString& scadPath, const QString& parameterFile,
                                 const QString& setName, const QString& sourcePath,
                                 const std::size_t normalizationLimit)
{
  if (!d->channel) {
    emit previewFailed(tr("The compute worker is not running."));
    return;
  }
  if (d->renderThread) {
    emit previewFailed(tr("The compute worker is already busy."));
    return;
  }

  nlohmann::json request;
  request["command"] = "preview";
  request["input"] = scadPath.toStdString();
  request["output"] = kPreviewOutputName;
  request["normalizationLimit"] = normalizationLimit;
  if (!parameterFile.isEmpty()) request["parameterFile"] = parameterFile.toStdString();
  if (!setName.isEmpty()) request["setName"] = setName.toStdString();
  if (!sourcePath.isEmpty()) request["sourcePath"] = sourcePath.toStdString();

  startRequest(request.dump(),
               [this](std::map<std::string, std::string>&& payloads, const QString& error) {
                 requestFinished();
                 if (!error.isEmpty()) {
                   emit previewFailed(error);
                   return;
                 }
                 const auto found = payloads.find(kPreviewOutputName);
                 if (found == payloads.end()) {
                   emit previewFailed(tr("The worker returned no product list."));
                   return;
                 }
                 auto products = std::make_shared<CsgInfo>();
                 // Every leaf the list names has to have arrived, or the preview would be missing
                 // geometry without saying so.
                 if (!import_csg_products(*products, found->second, payloads)) {
                   emit previewFailed(tr("The worker returned a preview that could not be read."));
                   return;
                 }
                 emit previewDone(products);
               });
}
