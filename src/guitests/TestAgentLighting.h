#pragma once

#include "UXTest.h"

class TestAgentLighting : public UXTest
{
  Q_OBJECT;
private slots:
  //! Every menu action must reach GLView, or the mode is CLI-only in practice.
  void checkMenuActionsSetTheMode();
  //! The actions are alternatives, not composable toggles.
  void checkModesAreMutuallyExclusive();
  //! The mode has to change what is actually drawn, not just a member.
  void checkModesChangeTheRender();
};
