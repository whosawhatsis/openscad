#include "TestMainWindow.h"

#include <QString>
#include <QStringList>
#include <QTest>

#include "gui/OpenSCADApp.h"
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

void TestMainWindow::checkNewWindowLaunchesChildProcess()
{
  QStringList arguments;
  scadApp->windowManager.setLauncher([&](const QString&, const QStringList& args) {
    arguments = args;
    return true;
  });

  const bool invoked = QMetaObject::invokeMethod(window, "on_fileActionNewWindow_triggered");
  scadApp->windowManager.setLauncher({});

  QVERIFY(invoked);
  QCOMPARE(arguments, QStringList{"--new-window-process"});
}
