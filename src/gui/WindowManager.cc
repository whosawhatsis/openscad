#include "gui/WindowManager.h"

#include <QCoreApplication>
#include <QProcess>
#include <QSet>
#include <utility>

#include "gui/MainWindow.h"
#include "utils/printutils.h"

WindowManager::WindowManager(Launcher launcher) : launcher(std::move(launcher))
{
}

void WindowManager::add(MainWindow *mainwin)
{
  this->windows.insert(mainwin);
}

void WindowManager::remove(MainWindow *mainwin)
{
  this->windows.remove(mainwin);
}

const QSet<MainWindow *>& WindowManager::getWindows() const
{
  return this->windows;
}

bool WindowManager::openWindow(const QStringList& filenames) const
{
  const auto executable = QCoreApplication::applicationFilePath();
  const auto arguments = QStringList{"--new-window-process"} + filenames;
  const bool started =
    launcher ? launcher(executable, arguments) : QProcess::startDetached(executable, arguments);
  if (started) {
    return true;
  }
  LOG(message_group::Error, "Could not start a new OpenSCAD window process.");
  return false;
}

void WindowManager::setLauncher(Launcher launcher)
{
  this->launcher = std::move(launcher);
}
