#include "TestModuleCache.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QScopeGuard>
#include <QString>
#include <QStringList>
#include <QTest>
#include <memory>

#include "core/node.h"
#include "platform/PlatformUtils.h"

void touchFile(const QString& filename)
{
  auto timeStamp = QDateTime::currentDateTime();

  QFileInfo fileInfo(filename);
  QFile file(filename);
  file.open(QIODevice::WriteOnly);
  if (file.isOpen()) {
    file.setFileTime(timeStamp, QFileDevice::FileModificationTime);
    file.setFileTime(timeStamp, QFileDevice::FileAccessTime);
  }
}

// Reloads and waits for it to finish. With process isolation on (it persists in QSettings, which
// this binary shares with the application) the preview completes on the event loop, not inside
// actionReloadRenderPreview(); a test that reads the result straight away sees nothing, and leaves
// the window locked so the next reload is silently dropped.
bool reloadAndWait(MainWindow *window)
{
  bool done = false;
  const auto connection =
    QObject::connect(window, &MainWindow::compilationDone, [&done](SourceFile *) { done = true; });
  window->actionReloadRenderPreview();
  QElapsedTimer timer;
  timer.start();
  while (!done && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  QObject::disconnect(connection);
  return done;
}

void TestModuleCache::testBasicCache()
{
  restoreWindowInitialState();

  QString filename = QString::fromStdString("test-tmp.scad");
  SourceFile *previousFile{nullptr};
  SourceFile *currentFile{nullptr};
  const auto connection = connect(window, &MainWindow::compilationDone,
                                  [&currentFile](SourceFile *file) { currentFile = file; });
  const auto disconnect = qScopeGuard([connection] { QObject::disconnect(connection); });

  window->designActionAutoReload->setChecked(false);  // Disable auto-reload  & preview
  window->tabManager->open(filename);                 // Open use.scad
  QVERIFY(reloadAndWait(window));                     // F5

  QVERIFY2(currentFile != nullptr, "The file 'test-tmp.scad' should be loaded.");
  previousFile = currentFile;  // save the loaded Source from the

  QVERIFY(reloadAndWait(window));
  QVERIFY2(previousFile == currentFile,
           "The file should be the same as the file cache should have done its work.");
  sleep(1);

  touchFile(filename);
  QVERIFY(reloadAndWait(window));
  QVERIFY2(
    previousFile != currentFile,
    "The file should *not* be the same as the file cache should have detected the timestamp change.");
}

std::vector<std::string>& findNode(std::shared_ptr<AbstractNode> node, std::vector<std::string>& path)
{
  path.push_back(node->verbose_name());
  for (auto child : node->getChildren()) return findNode(child, path);
  return path;
}

void TestModuleCache::testMCAD()
{
  restoreWindowInitialState();

  QString filename =
    QString::fromStdString(PlatformUtils::resourceBasePath()) + "/tests/modulecache-tests/use-mcad.scad";
  window->tabManager->open(filename);  // Open use-mcad.scad
  QVERIFY(reloadAndWait(window));      // F5

  auto node = window->instantiateRootFromSource(window->rootFile.get());
  QVERIFY2(node->verbose_name().empty(), "Root node name must be empty");
  QVERIFY2(node->getChildren().size() != 0, "There must have at least a node");
  QCOMPARE(QString::fromStdString(node->getChildren()[0]->verbose_name()),
           QString::fromStdString("module roundedBox"));
}
