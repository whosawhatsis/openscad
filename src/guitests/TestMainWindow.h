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
  void checkIsolatedPreviewUsesGuiColorScheme();
  void checkIsolatedRenderTagsSchemeColors();
  void checkUntouchedCustomizerDoesNotOverrideEditedText();
  void checkIsolatedRenderUsesCommandLineDefinitions();
  void checkF5DuringAnInFlightPreviewShowsTheEditedModel();
  void checkIdenticalPreviewRequestIsNotRepeated();
  void checkIsolatedWindowsPreviewConcurrently();
  //! Auto-reload previews by its own continuation, which must reach the worker too.
  void checkIsolatedAutoReloadPreviewUsesWorker();
  //! And what the worker renders must be what the Customizer says, not the document's defaults.
  void checkIsolatedRenderUsesCustomizerValues();
  //! And a window whose worker cannot start must fall back rather than be left unable to render.
  void checkAWindowWhoseWorkerCannotStartStillRenders();
  //! A worker that dies under its window -- crash, OOM kill, or a cancel escalating to a kill --
  //! must be replaced by the next request rather than leaving the window permanently unable to
  //! compute, and the editor's text must survive the crash untouched.
  void checkCrashedWorkerRespawns();
  //! An isolated window's busy state must be its own: another window's in-process render, which
  //! holds the application-wide lock for its whole synchronous duration, must not block this
  //! window's worker request from starting or finishing.
  void checkIsolatedWindowIsIndependentOfInProcessWindow();
  //! Cancelling an isolated preview must release this window's lock -- not just kill the worker --
  //! or the window is stuck reporting itself busy forever after a Stop.
  void checkCancelReleasesIsolatedWindowLock();
  void checkInProcessPreviewProducesProducts();
  void checkOpenTabPropagateToWindow();
  void checkSaveToShouldUpdateWindowTitle();
};
