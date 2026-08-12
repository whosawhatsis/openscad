#pragma once

#include <QObject>
#include <QSet>
#include <QStringList>

class WindowManager : public QObject
{
  Q_OBJECT

public:
  WindowManager() = default;

  void add(class MainWindow *mainwin);
  void remove(class MainWindow *mainwin);
  const QSet<MainWindow *>& getWindows() const;
  bool openWindow(const QStringList& filenames) const;

  static QStringList childArguments(const QStringList& filenames);

private:
  QSet<MainWindow *> windows;
};
