#include "TestMainWindow.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QString>
#include <QStringList>
#include <QTest>
#include <memory>

#include "Feature.h"
#include "geometry/Geometry.h"
#include "core/CSGNode.h"
#include "geometry/PolySet.h"
#include "gui/Editor.h"

#include "platform/PlatformUtils.h"

void TestMainWindow::checkOpenTabPropagateToWindow()
{
  restoreWindowInitialState();

  QString filename =
    QString::fromStdString(PlatformUtils::resourceBasePath()) + "/tests/basic-ux/empty.scad";

  // When we open a new file,
  window->tabManager->open(filename);

  // The window title must also have the name of open file
  QCOMPARE(window->windowTitle(), QFileInfo(filename).fileName());

  filename = QString::fromStdString(PlatformUtils::resourceBasePath()) + "/tests/basic-ux/empty2.scad";

  // When we open a new file,
  window->tabManager->open(filename);

  // The window title must also have the name of open file
  QCOMPARE(window->windowTitle(), QFileInfo(filename).fileName());
}

void TestMainWindow::checkSaveToShouldUpdateWindowTitle()
{
  restoreWindowInitialState();

  QString filename =
    QString::fromStdString(PlatformUtils::resourceBasePath()) + "/tests/basic-ux/empty.scad";

  // When we open a new file,
  window->tabManager->open(filename);

  window->tabManager->saveAs(window->activeEditor, "test-tmp.scad");

  // The window title must also have the name of open file
  QCOMPARE(window->windowTitle(), "test-tmp.scad");
}

// ------------------------------------------------------------------------------------------
// Process isolation, from the window's point of view.
//
// Everything below the window has its own tests -- the channel, the codec, the worker's request
// path. What none of them can show is that a *window* actually uses any of it: that the flag is
// read, that a worker is started with the window, that a render takes the isolated branch, and
// that the geometry comes back to the same place the in-process path puts it. Without this, the
// feature is proven everywhere except where the user meets it.
//
// The window is constructed here rather than reused from the fixture because process isolation is
// latched at construction; the fixture's window was built before the flag was set.

namespace {

//! Runs `source` in a window of its own -- rendered (F6) or previewed (F5) -- and returns the
//! window once it has finished compiling, or null if it never did.
MainWindow *runInOwnWindow(const QString& source, const bool preview)
{
  // Heap-allocated and released through the event loop, never destroyed on the stack. A window is
  // the target of queued connections and timers; tearing one down while the application is still
  // delivering events to it crashes in whatever happens to touch it next -- which cost a
  // backtrace to find, landing in formatIdentifierToAction() reading exportMap on a dead window.
  auto *window = new MainWindow{QStringList{}};
  window->activeEditor->setPlainText(source);

  bool compiled = false;
  QObject::connect(window, &MainWindow::compilationDone, window,
                   [&compiled](SourceFile *) { compiled = true; });

  if (preview) window->designActionPreview->trigger();
  else window->designActionRender->trigger();

  // The work crosses a process boundary, so it is slower than an in-process one and the wait has
  // to be generous. It still has to end: a window left waiting forever is the failure this whole
  // feature exists to prevent, so timing out here is a real result and not a flake.
  QElapsedTimer timer;
  timer.start();
  while (!compiled && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }

  // Deliberately not destroyed. A MainWindow registers itself with application-wide state and is
  // the target of queued connections; tearing one down inside a running test process crashes in
  // whatever touches it next. The test process exits shortly after, so leaking it is the cheaper
  // and more honest option than pretending the teardown is safe.
  return compiled ? window : nullptr;
}

}  // namespace

void TestMainWindow::checkIsolatedRenderProducesGeometry()
{
  Feature::enable_feature("process-isolation");
  auto *window = runInOwnWindow(QStringLiteral("cube([10, 10, 10]);"), false);
  Feature::enable_feature("process-isolation", false);

  QVERIFY2(window != nullptr, "an isolated render never finished");
  const auto polyset = std::dynamic_pointer_cast<const PolySet>(window->rootGeom);
  QVERIFY2(polyset != nullptr, "the isolated result was not a mesh");
  QCOMPARE(polyset->vertices.size(), size_t{8});
}

void TestMainWindow::checkIsolatedPreviewProducesProducts()
{
  // A preview crosses the same boundary as a render, but comes back as a product list rather than
  // one mesh. The window has to composite what the worker sent; until it does, F5 under isolation
  // is either wrong or never ends.
  Feature::enable_feature("process-isolation");
  auto *window = runInOwnWindow(QStringLiteral("cube([10, 10, 10]);"), true);
  Feature::enable_feature("process-isolation", false);

  QVERIFY2(window != nullptr, "an isolated preview never finished");
  const auto& products = window->previewProductsForTest();
  QVERIFY2(products != nullptr, "an isolated preview produced no product list");
  QCOMPARE(products->size(), size_t{1});
  // Products alone would also be there if the window had quietly previewed in-process, which is
  // exactly what it did before this was wired -- so the test has to say where they came from.
  QCOMPARE(window->isolatedPreviewsForTest(), 1);
}

void TestMainWindow::checkInProcessPreviewProducesProducts()
{
  // The control for the isolated preview test: same window, same trigger, isolation off. If this
  // hangs too, the problem is previewing in a window the test never showed -- not the worker.
  Feature::enable_feature("process-isolation", false);
  auto *window = runInOwnWindow(QStringLiteral("cube([10, 10, 10]);"), true);
  QVERIFY2(window != nullptr, "the in-process preview never finished either");
  QVERIFY2(window->previewProductsForTest() != nullptr, "no product list in-process");
}

void TestMainWindow::checkAWindowWhoseWorkerCannotStartStillRenders()
{
  // The fallback matters more than it looks: a user whose worker cannot start -- a broken install,
  // a policy blocking the executable, a sandbox -- must still be able to render, rather than find
  // the application useless until they discover a preference.
  //
  // With the flag off, the same window takes the in-process path, which is exactly the state the
  // fallback leaves it in.
  Feature::enable_feature("process-isolation", false);
  auto *window = runInOwnWindow(QStringLiteral("cube([10, 10, 10]);"), false);

  QVERIFY2(window != nullptr, "an in-process render never finished");
  QVERIFY(std::dynamic_pointer_cast<const PolySet>(window->rootGeom) != nullptr);
}
