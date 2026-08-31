// The GUI side of process isolation: a window's private compute worker (row 59).
//
// What is under test here is the process lifecycle, not geometry. The channel and the codec have
// their own tests; this covers spawning a child, handing it its end of the pair, getting messages
// back, and -- the part that matters most -- what happens when the child dies. A worker that
// crashes has to surface as a finished channel, because the whole point of the feature is that a
// model which kills the evaluator must not take the window with it. If that path hangs instead,
// process isolation has made things worse rather than better.
//
// The child is this same test binary, re-executed with a hidden Catch2 tag. Tags beginning with a
// dot are excluded from an unfiltered run, so `OpenSCADUnitTests` never serves as a worker by
// accident -- only when ComputeWorker names it. That avoids building a second executable purely to
// have something to spawn, and it means the child is a real process with a real exec, which is the
// only way the descriptor handoff is genuinely exercised.

#include "gui/ComputeWorker.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <catch2/catch_all.hpp>
#include <cstdlib>
#include <memory>

#include "io/ipc_channel.h"
#include "io/ipc_endpoint.h"

namespace {

// ComputeWorker passes the child its channel argument; the stub reads it from the environment so
// the Catch2 command line stays a plain tag.
constexpr auto kChannelEnvironmentVariable = "OPENSCAD_TEST_IPC_CHANNEL";
constexpr auto kStubTag = "[.compute-worker-stub]";

// QProcess needs an application object. The unit-test binary has no main of its own, so tests that
// spawn create one on demand and share it.
void ensureApplication()
{
  if (QCoreApplication::instance()) return;
  static int argc = 1;
  static char name[] = "OpenSCADUnitTests";
  static char *argv[] = {name, nullptr};
  static QCoreApplication app(argc, argv);
}

std::unique_ptr<ComputeWorker> makeStubWorker()
{
  ensureApplication();
  return std::make_unique<ComputeWorker>(QCoreApplication::applicationFilePath(),
                                         QStringList{QString::fromLatin1(kStubTag)},
                                         QString::fromLatin1(kChannelEnvironmentVariable));
}

}  // namespace

// Not a test. This is the worker process, and it only runs when named by tag.
TEST_CASE("compute worker stub", kStubTag)
{
  const char *argument = std::getenv(kChannelEnvironmentVariable);
  REQUIRE(argument != nullptr);
  const auto channel = ipc_channel_from_argument(argument);
  REQUIRE(channel);

  IpcMessage message;
  while (channel->read(message)) {
    if (message.name == "exit") return;
    if (message.name == "die") std::abort();  // stands in for a worker that crashes mid-request
    channel->write(message.name + ".echo", message.payload);
  }
}

TEST_CASE("A compute worker starts and answers over its channel", "[gui][ComputeWorker]")
{
  auto worker = makeStubWorker();
  REQUIRE(worker->start());
  CHECK(worker->isRunning());
  CHECK(worker->processId() != 0);

  // Deliberately full of the bytes that broke the previous carrier.
  const QByteArray payload(
    "\n\0\r\nA\0\xff"
    "Z",
    8);
  REQUIRE(worker->send("leaf/0.osig", payload));

  IpcMessage reply;
  REQUIRE(worker->receive(reply));
  CHECK(reply.name == "leaf/0.osig.echo");
  CHECK(QByteArray::fromStdString(reply.payload) == payload);

  REQUIRE(worker->send("exit", {}));
  CHECK(worker->waitForFinished());
  CHECK_FALSE(worker->isRunning());
}

TEST_CASE("Each compute worker is a separate process", "[gui][ComputeWorker]")
{
  // Every window owns a private worker; two windows sharing one would serialize behind each other,
  // which is most of what this feature exists to stop.
  auto first = makeStubWorker();
  auto second = makeStubWorker();
  REQUIRE(first->start());
  REQUIRE(second->start());
  CHECK(first->processId() != second->processId());

  REQUIRE(first->send("a", {}));
  REQUIRE(second->send("b", {}));
  IpcMessage onFirst;
  IpcMessage onSecond;
  REQUIRE(first->receive(onFirst));
  REQUIRE(second->receive(onSecond));
  CHECK(onFirst.name == "a.echo");
  CHECK(onSecond.name == "b.echo");

  REQUIRE(first->send("exit", {}));
  REQUIRE(second->send("exit", {}));
  CHECK(first->waitForFinished());
  CHECK(second->waitForFinished());
}

TEST_CASE("A crashed worker ends the channel instead of hanging", "[gui][ComputeWorker]")
{
  // The failure this feature exists to contain. receive() must return false rather than block
  // forever, or a model that kills the evaluator freezes the window -- which is worse than the
  // in-process behaviour it replaced.
  auto worker = makeStubWorker();
  REQUIRE(worker->start());
  REQUIRE(worker->send("die", {}));

  IpcMessage nothing;
  CHECK_FALSE(worker->receive(nothing));
  CHECK(worker->waitForFinished());
  CHECK_FALSE(worker->isRunning());
  // A crash is not a clean exit, and the caller has to be able to tell the difference to report it.
  CHECK_FALSE(worker->exitedCleanly());
}

TEST_CASE("A cancelled worker is killed and reports as stopped", "[gui][ComputeWorker]")
{
  // Cancellation has to work while the worker is mid-request and not answering, which is exactly
  // when a user reaches for it.
  auto worker = makeStubWorker();
  REQUIRE(worker->start());
  const qint64 pid = worker->processId();
  CHECK(pid != 0);

  worker->cancel();
  CHECK(worker->waitForFinished());
  CHECK_FALSE(worker->isRunning());
  CHECK_FALSE(worker->exitedCleanly());

  IpcMessage nothing;
  CHECK_FALSE(worker->receive(nothing));
}

TEST_CASE("A worker that cannot be started fails rather than pretending", "[gui][ComputeWorker]")
{
  ensureApplication();
  ComputeWorker worker(QStringLiteral("/nonexistent/openscad-compute-worker"), {},
                       QString::fromLatin1(kChannelEnvironmentVariable));
  CHECK_FALSE(worker.start());
  CHECK_FALSE(worker.isRunning());
  CHECK(worker.processId() == 0);
}

TEST_CASE("Sending to a worker that was never started is refused", "[gui][ComputeWorker]")
{
  ensureApplication();
  ComputeWorker worker(QCoreApplication::applicationFilePath(), {},
                       QString::fromLatin1(kChannelEnvironmentVariable));
  CHECK_FALSE(worker.isRunning());
  CHECK_FALSE(worker.send("leaf/0.osig", QByteArray("x")));
  IpcMessage nothing;
  CHECK_FALSE(worker.receive(nothing));
}
