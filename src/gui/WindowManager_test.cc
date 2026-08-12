#include <catch2/catch_test_macros.hpp>

#include <QStringList>

#include "gui/WindowManager.h"

TEST_CASE("window subprocess arguments are guarded", "[window_process]")
{
  CHECK(WindowManager::childArguments({}) == QStringList{"--new-window-process"});
  CHECK(WindowManager::childArguments({"model.scad"}) ==
        QStringList{"--new-window-process", "model.scad"});
}
