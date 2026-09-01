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
  //! Show Edges remains an independent overlay on ordinary appearance modes.
  void checkShadedComposesWithEdges();
  //! Entering Shaded before or after F6 must give the same picture. The mesh
  //! renderer built its vertex state once and never rebuilt it when the shader
  //! changed, so rendering first and switching after left the material
  //! attribute unbound - metals lost their environment and reflections never
  //! appeared, in the GUI only, which is why every CLI test passed.
  void checkShadedIsIndependentOfRenderOrder();
  //! Depth shading is one of these modes, not an independent toggle layered on
  //! top of them. While it was a separate bool, selecting it alongside another
  //! mode left the menu item checked and the renderer ignoring it.
  void checkDepthIsOneOfTheModes();
};
