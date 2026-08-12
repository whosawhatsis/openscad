#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QStringList>

#include "gui/WindowManager.h"

TEST_CASE("opening a window launches a guarded child process", "[window_process]")
{
  int argc = 1;
  char name[] = "openscad-test";
  char *argv[] = {name, nullptr};
  QCoreApplication app(argc, argv);

  bool launched = false;
  QStringList arguments;
  WindowManager manager([&](const QString&, const QStringList& args) {
    launched = true;
    arguments = args;
    return true;
  });

  CHECK(manager.openWindow({"model.scad"}));
  CHECK(launched);
  CHECK(arguments == QStringList{"--new-window-process", "model.scad"});
}
