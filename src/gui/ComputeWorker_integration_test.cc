// The real binary, driven as a compute worker.
//
// tests/test_compute_worker.py covers this ground far more thoroughly, but it cannot run on
// Windows: handing a descriptor to a child needs POSIX pass_fds, and the Windows equivalent is an
// inherited HANDLE, which Python's subprocess does not expose. Without something here, the worker
// request path -- render, preview, the product list -- would have no coverage at all on the one
// platform where the channel is a named pipe rather than a socket, and where the descriptor
// handoff is least like the others.
//
// So this is deliberately thin and deliberately cross-platform: it drives the real binary through
// ComputeWorker, exactly as a window will.

#include "gui/ComputeWorker.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "io/ipc_message.h"
#include "json/json.hpp"

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

void ensureApplication()
{
  if (QCoreApplication::instance()) return;
  static int argc = 1;
  static char name[] = "OpenSCADUnitTests";
  static char *argv[] = {name, nullptr};
  static QCoreApplication app(argc, argv);
}

std::unique_ptr<ComputeWorker> startRealWorker()
{
  ensureApplication();
  const QString program = QStringLiteral(OPENSCAD_BINARY_PATH);
  // Deliberately a failure and not a skip: a skip counts as a pass, and this is the only cover the
  // worker path has on Windows.
  REQUIRE_FALSE(program.isEmpty());
  INFO("worker binary: " << program.toStdString()
                         << " -- build the OpenSCAD target, not just OpenSCADUnitTests");
  REQUIRE(fs::exists(fs::u8path(program.toStdString())));

  auto worker = std::make_unique<ComputeWorker>(program, QStringList{QStringLiteral("--compute-worker")},
                                                QStringLiteral("OPENSCAD_IPC_CHANNEL"));
  REQUIRE(worker->start());
  return worker;
}

// A model written where the worker can read it. Kept trivial: what is under test is the channel
// and the request path, not the geometry.
std::string writeModel(const std::string& text, const std::string& stem)
{
  const auto path = fs::temp_directory_path() / (stem + ".scad");
  std::ofstream out(path);
  out << text;
  out.close();
  return path.generic_string();
}

json exchange(ComputeWorker& worker, const json& request, std::map<std::string, std::string>& payloads)
{
  const auto text = request.dump();
  REQUIRE(worker.send("request", QByteArray(text.data(), static_cast<int>(text.size()))));
  for (;;) {
    IpcMessage message;
    REQUIRE(worker.receive(message));
    if (message.name == "done") return json::parse(message.payload);
    payloads.emplace(message.name, message.payload);
  }
}

}  // namespace

TEST_CASE("The real binary serves a render over its channel", "[gui][ComputeWorkerIntegration]")
{
  auto worker = startRealWorker();
  std::map<std::string, std::string> payloads;
  const auto answer = exchange(*worker,
                               {{"command", "render"},
                                {"requestId", 1},
                                {"input", writeModel("cube([10, 10, 10]);", "openscad-worker-render")},
                                {"output", "result.osig"}},
                               payloads);

  CHECK(answer.value("ok", false));
  REQUIRE(payloads.count("result.osig") == 1);
  // The internal geometry format's magic. Its contents have their own unit tests; what matters
  // here is that the bytes crossed a real process boundary intact.
  CHECK(payloads["result.osig"].rfind("OSIG", 0) == 0);
}

TEST_CASE("The real binary serves a preview over its channel", "[gui][ComputeWorkerIntegration]")
{
  auto worker = startRealWorker();
  std::map<std::string, std::string> payloads;
  const auto answer = exchange(*worker,
                               {{"command", "preview"},
                                {"requestId", 2},
                                {"input", writeModel("cube([10, 10, 10]);", "openscad-worker-preview")},
                                {"output", "preview.json"}},
                               payloads);

  CHECK(answer.value("ok", false));
  REQUIRE(payloads.count("preview.json") == 1);

  const auto products = json::parse(payloads["preview.json"]);
  REQUIRE(products.contains("products"));
  REQUIRE_FALSE(products["products"].empty());

  // Every leaf the product list names must have arrived, or the parent cannot composite it.
  for (const auto& product : products["products"]) {
    for (const auto& leaf : product.value("intersections", json::array())) {
      const auto name = leaf.at("geometry").get<std::string>();
      INFO("product references " << name);
      REQUIRE(payloads.count(name) == 1);
      CHECK(payloads[name].rfind("OSIG", 0) == 0);
    }
  }
}

TEST_CASE("The real binary reports a bad request and stays up", "[gui][ComputeWorkerIntegration]")
{
  auto worker = startRealWorker();
  std::map<std::string, std::string> payloads;
  CHECK_FALSE(
    exchange(*worker, {{"command", "not-a-command"}, {"requestId", 3}}, payloads).value("ok", true));
  CHECK(worker->isRunning());

  const auto answer = exchange(*worker,
                               {{"command", "render"},
                                {"requestId", 4},
                                {"input", writeModel("sphere(5);", "openscad-worker-after-error")},
                                {"output", "after.osig"}},
                               payloads);
  CHECK(answer.value("ok", false));
  CHECK(answer.value("requestId", 0) == 4);
}

// ---------------------------------------------------------------------------------------------
// The asynchronous shape MainWindow needs.
//
// CGALWorker hands its result back through a `done` signal from a thread of its own, because the
// GUI thread cannot block while geometry is evaluated. ComputeWorker's channel is blocking, so it
// needs the same treatment before a window can use it -- otherwise isolating the computation would
// freeze the window it was meant to protect, which is worse than not isolating it.

#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include "geometry/Geometry.h"
#include "geometry/PolySet.h"

namespace {

//! Spins the event loop until `predicate` holds or the wait times out. Returns whether it held.
template <typename Predicate>
bool waitFor(const Predicate& predicate, int milliseconds = 30000)
{
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < milliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  return predicate();
}

}  // namespace

TEST_CASE("A render can be asked for without blocking the caller", "[gui][ComputeWorkerIntegration]")
{
  auto worker = startRealWorker();

  std::shared_ptr<const Geometry> result;
  bool finished = false;
  QObject::connect(worker.get(), &ComputeWorker::renderDone, worker.get(),
                   [&](const std::shared_ptr<const Geometry>& geometry) {
                     result = geometry;
                     finished = true;
                   });

  const auto model = writeModel("cube([10, 10, 10]);", "openscad-worker-async");
  worker->startRender(QString::fromStdString(model), {}, {});

  // The point of the exercise: control comes straight back, and the answer arrives later through
  // the event loop rather than by blocking here.
  CHECK_FALSE(finished);

  REQUIRE(waitFor([&] { return finished; }));
  REQUIRE(result);
  const auto polyset = std::dynamic_pointer_cast<const PolySet>(result);
  REQUIRE(polyset);
  CHECK(polyset->vertices.size() == 8);
}

TEST_CASE("A render that fails reports rather than hanging", "[gui][ComputeWorkerIntegration]")
{
  // A window waiting forever on a signal that will never come is the failure mode this has to
  // avoid: the user would see a progress bar and nothing else, with no way to tell it had died.
  auto worker = startRealWorker();

  bool failed = false;
  QString message;
  QObject::connect(worker.get(), &ComputeWorker::renderFailed, worker.get(), [&](const QString& reason) {
    message = reason;
    failed = true;
  });

  worker->startRender(QString::fromStdString(writeModel("nonsense (((", "openscad-worker-bad")), {}, {});
  REQUIRE(waitFor([&] { return failed; }));
  CHECK_FALSE(message.isEmpty());
}
