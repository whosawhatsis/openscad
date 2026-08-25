#pragma once

#include <QObject>
#include <QString>

#include "gui/MainWindow.h"

// Tests that assert on the compute worker itself — its process id, its respawn
// behaviour, or that dispatch returned before any parsing happened — are only
// meaningful with process isolation on. The suite runs isolated by default and
// legacy when OPENSCAD_GUI_TEST_LEGACY is set; these skip in the latter.
#define SKIP_WITHOUT_PROCESS_ISOLATION()                                        \
  do {                                                                          \
    if (!MainWindow::isProcessIsolation()) QSKIP("requires process isolation"); \
  } while (false)

// The inverse, for tests that assert on the in-process path users get with the
// feature off.
#define SKIP_WITH_PROCESS_ISOLATION()                                                \
  do {                                                                               \
    if (MainWindow::isProcessIsolation()) QSKIP("asserts on the non-isolated path"); \
  } while (false)

class UXTest : public QObject
{
  Q_OBJECT;

public:
  void setWindow(MainWindow *window);

protected:
  // Test fixtures live under tests/data/ in the source tree, and are copied into
  // Contents/Resources/tests/data/ of the macOS bundle. Both layouts are reached
  // through resourceBasePath(), so every test must go through here.
  static QString fixturePath(const QString& relative);

  void restoreWindowInitialState();

  MainWindow *window;
};
