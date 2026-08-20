#pragma once

#include "UXTest.h"

class TestAnalysisView : public UXTest
{
  Q_OBJECT;
private slots:
  //! Every menu action must reach GLView, or the mode is CLI-only in practice.
  void checkMenuActionsSetTheMode();
  //! The actions are alternatives, not composable toggles.
  void checkModesAreMutuallyExclusive();
  //! The mode has to change what is actually drawn, not just a member.
  void checkModesChangeTheRender();
  //! Depth shading is one of these modes, not an independent toggle layered on
  //! top of them. While it was a separate bool, selecting it alongside another
  //! mode left the menu item checked and the renderer ignoring it.
  void checkDepthIsOneOfTheModes();
};
