#include <QCoreApplication>
#include <QEvent>
#include <QTest>
#include <iostream>

#include "TestAgentLighting.h"
#include "TestExportImage.h"
#include "TestMainWindow.h"
#include "TestModuleCache.h"
#include "TestTabManager.h"

template <typename TestClass>
int runTests(MainWindow *window)
{
  TestClass tc;
  tc.setWindow(window);
  return QTest::qExec(&tc);
  return 0;
}

int runAllTest(MainWindow *window)
{
  int totalTestFailures = 0;
  std::cout << "******************************* RUN UX TESTS ********************************"
            << std::endl;
  totalTestFailures += runTests<TestTabManager>(window);
  totalTestFailures += runTests<TestMainWindow>(window);
  const bool isolated = MainWindow::isProcessIsolation();
  MainWindow::setProcessIsolation(false);
  auto *legacyWindow = new MainWindow({});
  totalTestFailures += runTests<TestModuleCache>(legacyWindow);
  legacyWindow->close();
  QCoreApplication::sendPostedEvents(legacyWindow, QEvent::DeferredDelete);
  // Restore the mode the suite was invoked in, then run the image-export tests under it.
  // The integrated branch forced isolation on at this point, from before row 29 made the
  // suite runnable in both modes; keeping that would make a legacy-mode run silently
  // isolated from here on.
  MainWindow::setProcessIsolation(isolated);
  totalTestFailures += runTests<TestExportImage>(window);
  totalTestFailures += runTests<TestAgentLighting>(window);
  std::cout << "********************************** RESULTS *********************************"
            << std::endl;
  std::cout << "Failures: " << totalTestFailures << std::endl;
  return totalTestFailures;
}
