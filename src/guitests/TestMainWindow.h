#pragma once

#include "UXTest.h"

class TestMainWindow : public UXTest
{
  Q_OBJECT;
private slots:
  void checkOpenTabPropagateToWindow();
  void checkReturnInsideBracesUsesKandRIndentation();
  void checkSaveToShouldUpdateWindowTitle();
};
