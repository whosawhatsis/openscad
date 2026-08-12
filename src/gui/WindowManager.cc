#include "gui/WindowManager.h"

#include <QCoreApplication>
#include <QProcess>
#include <QSet>

#include "gui/MainWindow.h"
#include "utils/printutils.h"

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

QStringList WindowManager::childArguments(const QStringList& filenames)
{
  return QStringList{"--new-window-process"} + filenames;
}

bool WindowManager::openWindow(const QStringList& filenames) const
{
  if (QProcess::startDetached(QCoreApplication::applicationFilePath(), childArguments(filenames))) {
    return true;
  }
  LOG(message_group::Error, "Could not start a new OpenSCAD window process.");
  return false;
}
