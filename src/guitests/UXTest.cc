#include "UXTest.h"

#include <QString>
#include <QSignalBlocker>

#include "platform/PlatformUtils.h"

QString UXTest::fixturePath(const QString& relative)
{
  return QString::fromStdString(PlatformUtils::resourceBasePath()) + "/tests/data/" + relative;
}

void UXTest::setWindow(MainWindow *window_)
{
  window = window_;
}

void UXTest::restoreWindowInitialState()
{
  window->rootGeom.reset();
  window->previewRenderer.reset();
  window->thrownTogetherRenderer.reset();

  QString filename = fixturePath("basic-ux/default.scad");
  window->tabManager->open(filename);

  while (window->tabCount > 1) {
    window->tabManager->closeCurrentTab();
  }

  const QSignalBlocker blocker(window->designActionAutoReload);
  window->designActionAutoReload->setChecked(true);  // Enable auto-reload & preview for this test only.
}
