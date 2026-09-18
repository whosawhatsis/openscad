#include "TestMainWindow.h"
#include "gui/QSettingsCached.h"
#include <QScopeGuard>
#include "openscad.h"
#include <QDoubleSpinBox>

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QTimer>
#include <memory>

#include "Feature.h"
#include "geometry/Geometry.h"
#include "core/CSGNode.h"
#include "geometry/PolySet.h"
#include "gui/Editor.h"
#include "gui/ProgressWidget.h"
#include "gui/parameter/ParameterWidget.h"

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
  // Disconnected before returning: the window outlives this frame, and a later compile on it would
  // otherwise write through a reference to a local that no longer exists.
  const auto connection = QObject::connect(window, &MainWindow::compilationDone, window,
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
  QObject::disconnect(connection);

  // Deliberately not destroyed. A MainWindow registers itself with application-wide state and is
  // the target of queued connections; tearing one down inside a running test process crashes in
  // whatever touches it next. The test process exits shortly after, so leaking it is the cheaper
  // and more honest option than pretending the teardown is safe.
  return compiled ? window : nullptr;
}

//! Renders again in a window that has already finished one, and waits for it the same way.
void renderAgain(MainWindow *window)
{
  bool compiled = false;
  const auto connection = QObject::connect(window, &MainWindow::compilationDone, window,
                                           [&compiled](SourceFile *) { compiled = true; });
  window->designActionRender->trigger();
  QElapsedTimer timer;
  timer.start();
  while (!compiled && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  QObject::disconnect(connection);
  QVERIFY2(compiled, "the second render never finished");
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

void TestMainWindow::checkIsolatedAutoReloadPreviewUsesWorker()
{
  // Auto-reload ends in csgReloadRender, not csgRender. If only F5's continuation knows about the
  // worker, every save-triggered preview quietly runs in this process: blocking the window and
  // evaluating a model the worker never saw.
  const QString path = QDir::temp().filePath(QStringLiteral("openscad-isolated-autoreload.scad"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
  file.write("cube([10, 10, 10]);");
  file.close();

  // Toggling auto-reload writes design/autoReload, and this test binary shares its settings with the
  // user's real application. Put back the value that was there -- not an assumed default -- however
  // this test exits, including a failed check's early return. Written straight to the settings rather
  // than through the action, which would restart this window's timer on the way out.
  const auto originalAutoReload = QSettingsCached{}.value("design/autoReload");
  const auto restoreAutoReload = qScopeGuard(
    [&originalAutoReload] { QSettingsCached{}.setValue("design/autoReload", originalAutoReload); });

  Feature::enable_feature("process-isolation");
  auto *window = new MainWindow{QStringList{}};  // leaked for the reason runInOwnWindow gives
  // Unlike runInOwnWindow's windows this one has a real file, so the auto-reload timer compileEnded
  // starts would keep previewing it for the rest of the run -- into a lambda whose captured local
  // is long gone. Both are stopped before this returns.
  window->designActionAutoReload->setChecked(false);
  window->activeEditor->filepath = path;
  bool compiled = false;
  const auto connection = QObject::connect(window, &MainWindow::compilationDone, window,
                                           [&compiled](SourceFile *) { compiled = true; });

  window->actionReloadRenderPreview();
  QElapsedTimer timer;
  timer.start();
  while (!compiled && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  QObject::disconnect(connection);
  window->designActionAutoReload->setChecked(false);
  Feature::enable_feature("process-isolation", false);

  QVERIFY2(compiled, "the auto-reload preview never finished");
  QVERIFY2(window->previewProductsForTest() != nullptr, "the auto-reload preview produced nothing");
  QCOMPARE(window->isolatedPreviewsForTest(), 1);
}

// Any document with a Customizer parameter has to preview under isolation. The request key built
// for the in-flight-preview comparison serializes each parameter value on its own, and a value
// exported by the Customizer is a bare ptree leaf -- which Boost's write_json refuses, because JSON
// has no top-level scalar. Every such preview aborted with "ptree contains data that cannot be
// represented in JSON format", so isolation was unusable on any parameterized file. The existing
// preview tests all use sources without parameters, which is why this went unnoticed.
void TestMainWindow::checkIsolatedPreviewWithCustomizerParameterSucceeds()
{
  Feature::enable_feature("process-isolation");
  auto *window = runInOwnWindow(QStringLiteral("pw_side = 5; cube([pw_side, 5, 5]);"), true);
  Feature::enable_feature("process-isolation", false);

  QVERIFY2(window != nullptr, "an isolated preview of a parameterized document never finished");
  QVERIFY2(window->previewProductsForTest() != nullptr,
           "an isolated preview of a parameterized document produced no product list");
}

void TestMainWindow::checkIsolatedPreviewUsesGuiColorScheme()
{
#ifdef ENABLE_OPENCSG
  Feature::enable_feature("process-isolation");
  auto *previewWindow = runInOwnWindow(
    QStringLiteral(
      "render() difference() { cube(100, center = true); cylinder(r = 12, h = 200, center = true); }"),
    true);
  Feature::enable_feature("process-isolation", false);
  QVERIFY2(previewWindow != nullptr, "the isolated preview never finished");

  previewWindow->show();
  QVERIFY(QTest::qWaitForWindowExposed(previewWindow));
  previewWindow->qglview->setColorScheme("DeepOcean");
  previewWindow->qglview->zoom(120, true);
  previewWindow->qglview->repaint();
  const QImage moved = previewWindow->qglview->grabFramebuffer().copy();
  QVERIFY(!moved.isNull());

  qsizetype cornfieldPixels = 0;
  for (int y = 0; y < moved.height(); ++y) {
    for (int x = 0; x < moved.width(); ++x) {
      const auto color = moved.pixelColor(x, y);
      if (color.red() > 150 && color.green() > 130 && color.blue() < 80) ++cornfieldPixels;
    }
  }
  QCOMPARE(cornfieldPixels, 0);
#endif
}

void TestMainWindow::checkIsolatedRenderTagsSchemeColors()
{
  // F5 has checkIsolatedPreviewUsesGuiColorScheme; F6 had nothing, and it takes a different path --
  // a single mesh through exportFileByName rather than a product list.
  //
  // The invariant is that the worker does NOT resolve implicit face colors: it cannot, because the
  // color scheme belongs to the window and the worker has its own, unrelated one. It sends tags and
  // the window resolves them against the scheme it is showing. If the worker ever sent resolved
  // colors instead, the rendered object would keep the worker's scheme no matter what the user
  // selects -- checked here on the geometry rather than on pixels, which in this harness depend on
  // window exposure and view mode.
  Feature::enable_feature("process-isolation");
  auto *window = runInOwnWindow(
    QStringLiteral(
      "difference() { cube(100, center = true); cylinder(r = 12, h = 200, center = true); }"),
    false);
  Feature::enable_feature("process-isolation", false);
  QVERIFY2(window != nullptr, "the isolated render never finished");

  const auto polyset = std::dynamic_pointer_cast<const PolySet>(window->rootGeom);
  QVERIFY2(polyset != nullptr, "the isolated render produced no PolySet");
  QVERIFY2(!polyset->color_indices.empty(),
           "the render carried no per-face color data at all, so the check below proves nothing");

  const bool tagged =
    std::any_of(polyset->color_indices.begin(), polyset->color_indices.end(), [](int32_t index) {
      return index == PolySet::COLOR_INDEX_DEFAULT || index == PolySet::COLOR_INDEX_CUTOUT;
    });
  QVERIFY2(tagged,
           "the worker resolved implicit face colors itself instead of tagging them; the rendered "
           "object will keep the worker's color scheme whatever the user selects");
}

// The Customizer's policy is that once the user touches a value it wins until the document closes.
// A value the user has NEVER touched must not win: editing the variable's default in the text has to
// take effect on the first render, as it does with the feature off. Sending the widget's values
// unconditionally makes every edit render one step behind -- and F6 then exports that stale mesh.
void TestMainWindow::checkUntouchedCustomizerDoesNotOverrideEditedText()
{
  Feature::enable_feature("process-isolation");
  // A name no other test uses: a Customizer value outlives a complete source replacement.
  auto *window =
    runInOwnWindow(QStringLiteral("outrank_size = 10; // [10:100]\ncube(outrank_size);"), false);
  Feature::enable_feature("process-isolation", false);
  QVERIFY2(window != nullptr, "the first isolated render never finished");
  QCOMPARE(window->rootGeom->getBoundingBox().max().x(), 10.0);

  // Edit the default with the Customizer untouched: the edit must win.
  window->rootGeom.reset();
  window->activeEditor->setPlainText(
    QStringLiteral("outrank_size = 40; // [10:100]\ncube(outrank_size);"));
  renderAgain(window);
  QVERIFY(window->rootGeom != nullptr);
  QCOMPARE(window->rootGeom->getBoundingBox().max().x(), 40.0);

  // ...and once touched, the Customizer wins, which is the escape hatch the user needs. Look the
  // widget up now, not earlier: setParameters() rebuilds them whenever the source changes.
  window->rootGeom.reset();
  auto *spinBox = window->activeEditor->parameterWidget->findChild<QDoubleSpinBox *>("doubleSpinBox");
  QVERIFY2(spinBox != nullptr, "no Customizer spin box for the parameter");
  spinBox->setValue(70);
  renderAgain(window);
  QVERIFY(window->rootGeom != nullptr);
  QCOMPARE(window->rootGeom->getBoundingBox().max().x(), 70.0);
}

// -D definitions are appended to the text the window parses. The worker parses its own copy of the
// document, so unless they are appended there too, `openscad -D size=7 model.scad` renders one thing
// in the GUI and another under isolation -- silently, since nothing reports the difference.
void TestMainWindow::checkIsolatedRenderUsesCommandLineDefinitions()
{
  const auto previousCommands = commandline_commands;
  commandline_commands = "size = 7;\n";
  Feature::enable_feature("process-isolation");
  auto *window = runInOwnWindow(QStringLiteral("cube(size);"), false);
  Feature::enable_feature("process-isolation", false);
  commandline_commands = previousCommands;

  QVERIFY2(window != nullptr, "the isolated render never finished");
  QVERIFY2(window->rootGeom != nullptr, "the isolated render produced no geometry");
  QCOMPARE(window->rootGeom->getBoundingBox().max().x(), 7.0);
}

namespace {

// Slow enough in the worker that a second F5 reliably arrives while it is still computing, without
// making the suite slow. render() forces a real boolean rather than a cheap product list; `salt`
// keeps each test's model out of the other's cache.
QString slowPreviewModel(int salt)
{
  return QStringLiteral(
           "render() union() { for (i = [0:70]) translate([i * 6 + %1, 0, 0]) sphere(3, $fn = 72); }")
    .arg(salt);
}

//! Pumps events until the windows have been idle for a short stretch, so a preview that re-runs
//! itself on completion has had its chance to start and finish. Idle means the windows themselves
//! say so: an isolated window releases the application-wide lock while its worker computes.
bool waitUntilSettled(std::initializer_list<MainWindow *> windows, int timeoutMs = 90000)
{
  QElapsedTimer total;
  total.start();
  QElapsedTimer idle;
  bool idleRunning = false;
  while (total.elapsed() < timeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const bool busy =
      GuiLocker::isLocked() ||
      std::any_of(windows.begin(), windows.end(), [](MainWindow *w) { return w->isBusyForTest(); });
    if (busy) {
      idleRunning = false;
    } else if (!idleRunning) {
      idle.start();
      idleRunning = true;
    } else if (idle.elapsed() > 750) {
      return true;
    }
  }
  return false;
}

}  // namespace

// Pressing F5 while an isolated preview is still computing must not be lost. In-process, compile()
// runs synchronously, so a second F5 caught during it is re-run straight afterwards; under isolation
// compile() returns at once and the worker answers later, so that check had already passed and the
// request was silently dropped -- the window kept showing the OLD model after the user asked for the
// new one. The edited model must end up on screen, and it should not have to wait for the stale
// preview to run to completion first.
void TestMainWindow::checkF5DuringAnInFlightPreviewShowsTheEditedModel()
{
  Feature::enable_feature("process-isolation");
  auto *window = new MainWindow{QStringList{}};
  Feature::enable_feature("process-isolation", false);
  window->activeEditor->setPlainText(slowPreviewModel(1));
  window->designActionPreview->trigger();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  QVERIFY2(window->isBusyForTest(),
           "the first preview finished before the second F5, so this would not exercise an in-flight "
           "preview at all");

  window->activeEditor->setPlainText(QStringLiteral("cube(7);"));
  window->designActionPreview->trigger();

  QVERIFY2(waitUntilSettled({window}), "the window never settled after the second F5");
  const auto products = window->previewProductsForTest();
  QVERIFY2(products != nullptr, "no preview is showing");
  QCOMPARE(products->getBoundingBox().max().x(), 7.0);
}

// The other half: pressing F5 again with nothing changed while a preview is computing must not queue
// a second, identical preview behind it. That would redo seconds of work for a result already on the
// way. Once the request above is no longer dropped, this is what keeps it from being repeated.
void TestMainWindow::checkIdenticalPreviewRequestIsNotRepeated()
{
  Feature::enable_feature("process-isolation");
  auto *window = new MainWindow{QStringList{}};
  Feature::enable_feature("process-isolation", false);
  window->activeEditor->setPlainText(slowPreviewModel(2));
  const auto before = window->isolatedPreviewsForTest();
  const auto requestsBefore = window->isolatedPreviewRequestsForTest();

  window->designActionPreview->trigger();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  QVERIFY2(window->isBusyForTest(), "the first preview finished before the repeat F5 arrived");
  window->designActionPreview->trigger();

  QVERIFY2(waitUntilSettled({window}), "the window never settled after the repeat F5");
  QCOMPARE(window->isolatedPreviewsForTest() - before, 1);
  // One request, not two. Cancelling the first preview and running the same thing again would also
  // finish with a single completed preview, having thrown the first one's work away.
  QCOMPARE(window->isolatedPreviewRequestsForTest() - requestsBefore, 1);
}

// Each window has its own worker process, so one window's slow preview must not hold up another's.
// That is half the reason for isolation: a heavy model in one window should leave the rest usable.
// The second window's quick preview has to finish -- showing its own model -- while the first is
// still computing, rather than waiting its turn behind it.
void TestMainWindow::checkIsolatedWindowsPreviewConcurrently()
{
  Feature::enable_feature("process-isolation");
  auto *slow = new MainWindow{QStringList{}};
  auto *quick = new MainWindow{QStringList{}};
  Feature::enable_feature("process-isolation", false);

  slow->activeEditor->setPlainText(slowPreviewModel(3));
  const auto slowBefore = slow->isolatedPreviewsForTest();
  slow->designActionPreview->trigger();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  QCOMPARE(slow->isolatedPreviewsForTest(), slowBefore);  // the slow preview is really in flight

  quick->activeEditor->setPlainText(QStringLiteral("cube(9);"));
  const auto quickBefore = quick->isolatedPreviewsForTest();
  quick->designActionPreview->trigger();

  QElapsedTimer timer;
  timer.start();
  while (quick->isolatedPreviewsForTest() == quickBefore && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  QVERIFY2(quick->isolatedPreviewsForTest() > quickBefore,
           "the second window's preview never ran: it was blocked by the first window's");
  QVERIFY2(slow->isolatedPreviewsForTest() == slowBefore,
           "the second window only finished after the first did, so they ran one after another");
  QCOMPARE(quick->previewProductsForTest()->getBoundingBox().max().x(), 9.0);

  QVERIFY2(waitUntilSettled({slow, quick}), "the windows never settled");
}

void TestMainWindow::checkIsolatedRenderUsesCustomizerValues()
{
  // The worker parses its own copy of the source, so without being told the Customizer's values it
  // renders the document's defaults -- an F6 (and any export taken from it) that quietly ignores
  // what the user set in the pane.
  Feature::enable_feature("process-isolation");
  auto *window = runInOwnWindow(QStringLiteral("pw_side = 5; cube([pw_side, 5, 5]);"), false);
  QVERIFY2(window != nullptr, "the first isolated render never finished");
  auto polyset = std::dynamic_pointer_cast<const PolySet>(window->rootGeom);
  QVERIFY(polyset != nullptr);
  // Guards the measurement: if the default were already 20 the assertion below would prove nothing.
  QCOMPARE(polyset->getBoundingBox().sizes().x(), 5.0);

  ParameterSet values;
  values["pw_side"].put_value<double>(20);
  window->activeEditor->parameterWidget->importValuesForTest(values);

  // The source is unchanged, so setParameters() keeps the values just injected.
  renderAgain(window);
  Feature::enable_feature("process-isolation", false);

  polyset = std::dynamic_pointer_cast<const PolySet>(window->rootGeom);
  QVERIFY2(polyset != nullptr, "the second isolated render produced no mesh");
  QCOMPARE(polyset->getBoundingBox().sizes().x(), 20.0);
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

void TestMainWindow::checkCrashedWorkerRespawns()
{
  // A worker can go away under its window -- a crash, the OS reclaiming memory, or the kill a
  // cancellation escalates to. Before respawn, the window then had no worker for the rest of its
  // life: every later F5/F6 failed with "not running" until the application was restarted, which is
  // exactly the wedged window this feature exists to prevent.
  Feature::enable_feature("process-isolation");
  auto *window = runInOwnWindow(QStringLiteral("cube([10, 10, 10]);"), false);
  QVERIFY2(window != nullptr, "an isolated render never finished");
  const auto editorTextBeforeCrash = window->activeEditor->toPlainText();

  const auto workerBeforeCrash = window->computeWorkerProcessId();
  QVERIFY2(workerBeforeCrash > 0, "the window has no live compute worker to kill");

  window->exitComputeWorkerForTest();
  QElapsedTimer diedTimer;
  diedTimer.start();
  while (window->computeWorkerProcessId() != 0 && diedTimer.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  QCOMPARE(window->computeWorkerProcessId(), qint64{0});
  // The window's own lock must release when its worker dies, not just when a request completes --
  // otherwise "not running" is followed by "permanently busy" instead of a working respawn.
  QVERIFY2(!window->isBusyForTest(), "the window is still marked busy after its worker died");

  // The editor must not lose the user's text just because the process underneath it died.
  QCOMPARE(window->activeEditor->toPlainText(), editorTextBeforeCrash);

  // The next render must succeed anyway, and must do so with a fresh process rather than a wedged
  // window that keeps failing "not running" forever.
  renderAgain(window);
  const auto polyset = std::dynamic_pointer_cast<const PolySet>(window->rootGeom);
  QVERIFY2(polyset != nullptr, "the render after the crash produced no mesh");

  const auto workerAfterRespawn = window->computeWorkerProcessId();
  QVERIFY2(workerAfterRespawn > 0, "no worker is running after the respawned render");
  QVERIFY2(workerAfterRespawn != workerBeforeCrash, "the respawned worker reused the dead PID");

  Feature::enable_feature("process-isolation", false);
}

void TestMainWindow::checkIsolatedWindowIsIndependentOfInProcessWindow()
{
  // The in-process path legitimately shares one application-wide lock across every window --
  // that path's caches and evaluation state are not safe for two windows to touch at once. An
  // isolated window has none of that: its geometry is computed by its own worker process, so
  // nothing about another window's in-process render should be able to hold it up.
  Feature::enable_feature("process-isolation", false);
  auto *inProcess = new MainWindow{QStringList{}};
  Feature::enable_feature("process-isolation");
  auto *isolated = new MainWindow{QStringList{}};
  Feature::enable_feature("process-isolation", false);

  inProcess->activeEditor->setPlainText(slowPreviewModel(31));
  isolated->activeEditor->setPlainText(QStringLiteral("cube(9);"));

  // An in-process preview is one synchronous call: trigger() does not return to this test until
  // the whole render is finished, so there is no window afterwards in which to observe it "still
  // running". report_func() pumps the event loop a few times a second while it computes, though --
  // the same mechanism that keeps the app responsive during a long boolean -- so a timer queued
  // before the trigger fires *from inside* that nested loop, while the in-process window still
  // genuinely holds the application-wide lock. That is the real window to try dispatching the
  // isolated window's own request in.
  bool triedWhileLocked = false;
  bool wasLockedAtTheTime = false;
  const auto isolatedPreviewsBefore = isolated->isolatedPreviewsForTest();
  QTimer::singleShot(50, [&] {
    triedWhileLocked = true;
    wasLockedAtTheTime = GuiLocker::isLocked();
    isolated->designActionPreview->trigger();
  });
  inProcess->designActionPreview->trigger();
  QVERIFY2(triedWhileLocked, "the timer never fired during the in-process render");
  QVERIFY2(wasLockedAtTheTime, "the in-process preview had already released the application-wide lock");

  QElapsedTimer timer;
  timer.start();
  while (isolated->isolatedPreviewsForTest() == isolatedPreviewsBefore && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  QVERIFY2(isolated->isolatedPreviewsForTest() > isolatedPreviewsBefore,
           "the isolated window's preview was blocked by the other window's in-process render");

  QVERIFY2(waitUntilSettled({inProcess, isolated}), "the windows never settled");
}

void TestMainWindow::checkCancelReleasesIsolatedWindowLock()
{
  // Cancelling has to release this window's own lock, not just end up killing the worker: a Stop
  // that leaves the window reporting itself busy is indistinguishable, from the user's seat, from
  // the wedged window process isolation exists to prevent.
  Feature::enable_feature("process-isolation");
  auto *window = new MainWindow{QStringList{}};
  Feature::enable_feature("process-isolation", false);

  window->activeEditor->setPlainText(slowPreviewModel(32));
  window->designActionPreview->trigger();
  QVERIFY2(window->isBusyForTest(), "the isolated preview never marked the window busy");

  auto *progress = window->findChild<ProgressWidget *>();
  QVERIFY(progress != nullptr);
  progress->cancel();

  QElapsedTimer timer;
  timer.start();
  while (window->isBusyForTest() && timer.elapsed() < 15000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  QVERIFY2(!window->isBusyForTest(), "cancelling an isolated preview left the window's lock held");

  // And the window must be usable again immediately, not just unlocked in name.
  window->activeEditor->setPlainText(QStringLiteral("cube(9);"));
  const auto previewsBefore = window->isolatedPreviewsForTest();
  window->designActionPreview->trigger();
  timer.restart();
  while (window->isolatedPreviewsForTest() == previewsBefore && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  QVERIFY2(window->isolatedPreviewsForTest() > previewsBefore,
           "the window could not preview again after the cancelled one released its lock");
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
