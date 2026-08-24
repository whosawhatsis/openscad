#pragma once

#include "UXTest.h"

class TestMainWindow : public UXTest
{
  Q_OBJECT;
private slots:
  void checkOpenTabPropagateToWindow();
  void checkEditorEnhancementsFeatureFlag();
  void checkKeywordCompletionRemainsAvailable();
  void checkCallableCompletionAddsStructure();
  void checkUserModuleCompletionAddsStructure();
  void checkCompletionReusesExistingPunctuation();
  void checkEditorEnhancementsFlagNotLeaked();
  void checkReturnInsideBracesUsesKandRIndentation();
  void checkSaveToShouldUpdateWindowTitle();
};
