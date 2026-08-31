#include "TestAnalysisView.h"

#include <QAction>
#include <QImage>
#include <QString>
#include <QTest>

#include "platform/PlatformUtils.h"

namespace {

QImage grabViewport(MainWindow *window)
{
  // grabFramebuffer() returns a null image until the widget has actually been
  // exposed and has a valid GL context. Without waiting for that, the first grab
  // comes back null and every "differs from the shaded view" check below passes
  // vacuously - which is exactly what happened the first time this test ran.
  window->show();
  QTest::qWaitForWindowExposed(window);
  // repaint() rather than update(): update() only schedules, so a grab can land
  // on a frame that is still being composed.
  window->qglview->repaint();
  QTest::qWait(150);
  // .copy() is not redundant: the returned image's buffer is owned by the widget
  // and is reused by the next grab, so a retained QImage silently becomes a
  // dangling reference - which showed up first as nonsense metadata and then as
  // a segfault when its bits were read.
  return window->qglview->grabFramebuffer().copy();
}

/*!
   Compare what was drawn, not how it is labeled.

   QCOMPARE on QImage checks devicePixelRatio first, and two grabs of the same
   viewport can disagree on it (the first grab of a not-yet-exposed widget
   carries a different ratio from later ones) while holding identical pixels.
   That is metadata about the display, not about the render this test is here to
   check.
 */
bool sameRender(const QImage& a, const QImage& b)
{
  if (a.isNull() || b.isNull() || a.size() != b.size()) return false;
  // Sampled through QImage::pixel() rather than memcmp over constBits(): the
  // raw-buffer route segfaulted here, and a grid of samples is enough to tell
  // one render mode from another - these images differ across whole faces, not
  // in single pixels.
  for (int y = 0; y < a.height(); y += 8) {
    for (int x = 0; x < a.width(); x += 8) {
      if (a.pixel(x, y) != b.pixel(x, y)) return false;
    }
  }
  return true;
}

}  // namespace

void TestAnalysisView::checkMenuActionsSetTheMode()
{
  restoreWindowInitialState();

  QCOMPARE(QString(window->menuAnalysisView->title()).remove('&'), QString("Shading"));
  QVERIFY(window->viewActionAnalysisViewCanny->isVisible());
  QVERIFY(window->viewActionAnalysisViewWireframe->isVisible());

  // Triggering the action, rather than calling the handler directly: the failure
  // this guards against is a name mismatch that leaves Qt's auto-connection
  // silently unbound, which calling the handler would not catch.
  window->viewActionAnalysisViewNormal->trigger();
  QCOMPARE(window->qglview->analysisMode(), AnalysisMode::Normal);

  window->viewActionAnalysisViewCoordinate->trigger();
  QCOMPARE(window->qglview->analysisMode(), AnalysisMode::Coordinate);

  window->viewActionAnalysisViewFlat->trigger();
  QCOMPARE(window->qglview->analysisMode(), AnalysisMode::Flat);

  window->viewActionAnalysisViewCanny->trigger();
  QCOMPARE(window->qglview->analysisMode(), AnalysisMode::Canny);

  window->viewActionAnalysisViewWireframe->trigger();
  QCOMPARE(window->qglview->analysisMode(), AnalysisMode::Wireframe);

  window->viewActionAnalysisViewShaded->trigger();
  QCOMPARE(window->qglview->analysisMode(), AnalysisMode::Shaded);

  window->viewActionAnalysisViewChromatic->trigger();
  QCOMPARE(window->qglview->analysisMode(), AnalysisMode::Chromatic);

  window->viewActionAnalysisViewDefault->trigger();
  QCOMPARE(window->qglview->analysisMode(), AnalysisMode::Default);
}

void TestAnalysisView::checkModesAreMutuallyExclusive()
{
  restoreWindowInitialState();

  window->viewActionAnalysisViewNormal->trigger();
  QVERIFY(window->viewActionAnalysisViewNormal->isChecked());
  QVERIFY(!window->viewActionAnalysisViewDefault->isChecked());

  window->viewActionAnalysisViewChromatic->trigger();
  QVERIFY(window->viewActionAnalysisViewChromatic->isChecked());
  QVERIFY(!window->viewActionAnalysisViewNormal->isChecked());
}

void TestAnalysisView::checkModesChangeTheRender()
{
  restoreWindowInitialState();

  const QString filename =
    QString::fromStdString(PlatformUtils::resourceBasePath()) + "/tests/basic-ux/empty.scad";
  window->tabManager->open(filename);

  window->viewActionAnalysisViewDefault->trigger();
  grabViewport(window);  // discard: the first paint after exposure is not settled
  const QImage shaded = grabViewport(window);
  QVERIFY(!shaded.isNull());
  QVERIFY(shaded.width() > 0 && shaded.height() > 0);

  // Everything below reads a difference between two grabs as evidence about a
  // mode. That inference is only valid if two grabs of the *same* mode agree, so
  // establish that first - otherwise a flaky viewport would masquerade as a
  // state leak, and did during development.
  QVERIFY2(sameRender(grabViewport(window), shaded),
           "two consecutive grabs of the same view differ; this test cannot "
           "distinguish a mode change from viewport noise");

  struct ModeCase {
    QAction *action;
    const char *name;
  };
  const ModeCase cases[] = {
    {window->viewActionAnalysisViewShaded, "Shaded"},
    {window->viewActionAnalysisViewNormal, "Normal"},
    {window->viewActionAnalysisViewCoordinate, "Coordinate"},
    {window->viewActionAnalysisViewFlat, "Flat"},
    {window->viewActionAnalysisViewChromatic, "Chromatic"},
  };

  // Each mode is checked round trip on its own, so a state leak names the mode
  // that leaked instead of failing at the end of a chain.
  for (const auto& c : cases) {
    c.action->trigger();
    QVERIFY2(!sameRender(grabViewport(window), shaded),
             qPrintable(QString("%1 mode did not change the render").arg(c.name)));

    window->viewActionAnalysisViewDefault->trigger();
    QVERIFY2(sameRender(grabViewport(window), shaded),
             qPrintable(QString("returning to Default after %1 did not restore the view - "
                                "that mode leaks GL state")
                          .arg(c.name)));
  }
}

void TestAnalysisView::checkDepthIsOneOfTheModes()
{
  restoreWindowInitialState();

  // Depth belongs to the same exclusive group as the rest.
  window->viewActionAnalysisViewDepth->trigger();
  QVERIFY(window->viewActionAnalysisViewDepth->isChecked());
  QCOMPARE(window->qglview->analysisMode(), AnalysisMode::Depth);

  // Selecting another mode must deselect it - the failure this replaces was a
  // checked "Shade by Depth" that the renderer silently discarded because an
  // agent lighting mode was also active.
  window->viewActionAnalysisViewNormal->trigger();
  QVERIFY(!window->viewActionAnalysisViewDepth->isChecked());
  QCOMPARE(window->qglview->analysisMode(), AnalysisMode::Normal);

  window->viewActionAnalysisViewDefault->trigger();
  QCOMPARE(window->qglview->analysisMode(), AnalysisMode::Default);
}

void TestAnalysisView::checkShadedComposesWithEdges()
{
  restoreWindowInitialState();

  const QString filename =
    QString::fromStdString(PlatformUtils::resourceBasePath()) + "/tests/basic-ux/empty.scad";
  window->tabManager->open(filename);
  window->viewActionAnalysisViewShaded->trigger();
  window->viewActionShowEdges->setChecked(false);
  grabViewport(window);
  const QImage shaded = grabViewport(window);

  window->viewActionShowEdges->setChecked(true);
  QVERIFY2(!sameRender(grabViewport(window), shaded),
           "Show Edges does not composite over shaded rendering");

  window->viewActionShowEdges->setChecked(false);
  QVERIFY2(sameRender(grabViewport(window), shaded),
           "disabling Show Edges does not restore plain shaded rendering");
}
