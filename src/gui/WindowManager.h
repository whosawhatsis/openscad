#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>

class WindowManager : public QObject
{
  Q_OBJECT

public:
  using Launcher = std::function<bool(const QString&, const QStringList&)>;

  WindowManager() = default;
  explicit WindowManager(Launcher launcher);

  void add(class MainWindow *mainwin);
  void remove(class MainWindow *mainwin);
  const QSet<MainWindow *>& getWindows() const;
  bool openWindow(const QStringList& filenames) const;
  void setLauncher(Launcher launcher);

private:
  Launcher launcher;
  QSet<MainWindow *> windows;
};
