#pragma once

#include "UXTest.h"

class TestMainWindow : public UXTest
{
  Q_OBJECT;
private slots:
  //! A window with process isolation on must still produce geometry -- from its worker process
  //! rather than its own thread, but by the same route and into the same place.
  void checkIsolatedRenderProducesGeometry();
  //! And a preview under isolation must come back as a product list the window can composite.
  void checkIsolatedPreviewProducesProducts();
  //! And what the worker renders must be what the Customizer says, not the document's defaults.
  void checkIsolatedRenderUsesCustomizerValues();
  //! And a window whose worker cannot start must fall back rather than be left unable to render.
  void checkAWindowWhoseWorkerCannotStartStillRenders();
  void checkInProcessPreviewProducesProducts();
  void checkOpenTabPropagateToWindow();
  void checkSaveToShouldUpdateWindowTitle();
};
