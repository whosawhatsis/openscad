#include "TestMainWindow.h"

#include <QScopeGuard>
#include <QString>
#include <QStringList>
#include <QTest>

#include "Feature.h"
#include "core/SourceFile.h"
#include "openscad.h"
#include "gui/ScintillaEditor.h"
#include "platform/PlatformUtils.h"

void TestMainWindow::checkOpenTabPropagateToWindow()
{
  restoreWindowInitialState();

  QString filename =
    QString::fromStdString(PlatformUtils::resourceBasePath()) + "/tests/basic-ux/empty.scad";

  // When we open a new file,
  window->tabManager->open(filename);

  // The window title must also have the name of open file
  QCOMPARE(window->windowTitle(), QFileInfo(filename).fileName());

  filename = QString::fromStdString(PlatformUtils::resourceBasePath()) + "/tests/basic-ux/empty2.scad";

  // When we open a new file,
  window->tabManager->open(filename);

  // The window title must also have the name of open file
  QCOMPARE(window->windowTitle(), QFileInfo(filename).fileName());
}

void TestMainWindow::checkEditorEnhancementsFeatureFlag()
{
  QCOMPARE(Feature::ExperimentalEditorEnhancements.get_name(), std::string("editor-enhancements"));
  QVERIFY(!Feature::ExperimentalEditorEnhancements.is_enabled());
}

void TestMainWindow::checkKeywordCompletionRemainsAvailable()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);
  QVERIFY(!Feature::ExperimentalEditorEnhancements.is_enabled());
  editor->setupAutoComplete();

  // Language keywords come from Builtins::keywordList, not from the module/function
  // registries, and must keep completing with the experimental feature disabled.
  editor->setPlainText("els");
  editor->setCursorPosition(0, 3);
  editor->qsci->autoCompleteFromAPIs();
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QCOMPARE(editor->toPlainText(), QString("else"));
}

void TestMainWindow::checkCallableCompletionAddsStructure()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);
  Feature::enable_feature("editor-enhancements");
  // A failed QCOMPARE returns from the slot, so the reset cannot live at the end of the body.
  const auto featureGuard = qScopeGuard([] { Feature::enable_feature("editor-enhancements", false); });
  editor->setupAutoComplete();

  editor->setPlainText("cub");
  editor->setCursorPosition(0, 3);
  editor->qsci->autoCompleteFromAPIs();
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QCOMPARE(editor->toPlainText(), QString("cube();"));

  editor->setPlainText("translat");
  editor->setCursorPosition(0, 8);
  editor->qsci->autoCompleteFromAPIs();
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QCOMPARE(editor->toPlainText(), QString("translate()"));
}

// Must run immediately after checkCallableCompletionAddsStructure: that test enables an
// experimental feature globally, and an early return from a failed QCOMPARE must not leave it
// enabled for everything that follows.
void TestMainWindow::checkUserModuleCompletionAddsStructure()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);
  Feature::enable_feature("editor-enhancements");
  const auto featureGuard = qScopeGuard([] { Feature::enable_feature("editor-enhancements", false); });
  editor->setupAutoComplete();

  // A user module that uses its children completes without a terminating semicolon;
  // one that does not gets the semicolon, exactly as for builtins.
  // The declarations parse; the buffer additionally holds a half-typed name, which
  // does not. That is the normal editing state - completion runs against the last
  // successful parse, not the malformed buffer.
  const QString declarations = "module wrapper() { children(); }\nmodule widget() { cube(); }\n";
  editor->setPlainText(declarations + "wrap");

  // Parse the buffer directly. The production path runs behind an autocompleteMode
  // setting and three preference toggles, and MainWindow does not parse an unsaved
  // buffer at all - neither of which this test is about.
  SourceFile *parsed = nullptr;
  QVERIFY(parse(parsed, declarations.toStdString(), "<test>", "<test>", 0));
  QVERIFY(parsed != nullptr);
  editor->correctUserVarNamesForCompletionFromSourceFile(parsed, true, true, true);

  editor->setCursorPosition(2, 4);
  editor->qsci->autoCompleteFromAPIs();
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QVERIFY2(editor->toPlainText().endsWith("wrapper()"),
           qPrintable("got: " + QString(editor->toPlainText()).replace("\n", "\\n")));
  QVERIFY(!editor->toPlainText().endsWith("wrapper();"));

  // While the source is malformed there is no parsed file to harvest. The last
  // reliable symbol set must survive, rather than completion going dead mid-edit.
  editor->correctUserVarNamesForCompletionFromSourceFile(nullptr, true, true, true);
  editor->setPlainText(declarations + "wrap");
  editor->setCursorPosition(2, 4);
  editor->qsci->autoCompleteFromAPIs();
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QVERIFY2(
    editor->toPlainText().endsWith("wrapper()"),
    qPrintable("after malformed parse, got: " + QString(editor->toPlainText()).replace("\n", "\\n")));
}

void TestMainWindow::checkEditorEnhancementsFlagNotLeaked()
{
  QVERIFY(!Feature::ExperimentalEditorEnhancements.is_enabled());
}

void TestMainWindow::checkReturnInsideBracesUsesKandRIndentation()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);
  editor->qsci->setAutoIndent(true);
  editor->qsci->setIndentationWidth(2);
  editor->qsci->setIndentationsUseTabs(false);
  editor->setPlainText("{}");
  editor->setCursorPosition(0, 1);

  editor->qsci->setFocus();
  QTest::keyClick(editor->qsci, Qt::Key_Return);

  QCOMPARE(editor->toPlainText(), QString("{\n}"));

  editor->setPlainText("{\n}");
  editor->setCursorPosition(0, 1);
  QTest::keyClick(editor->qsci, Qt::Key_Return);

  QCOMPARE(editor->toPlainText(), QString("{\n  \n}"));
}
void TestMainWindow::checkSaveToShouldUpdateWindowTitle()
{
  restoreWindowInitialState();

  QString filename =
    QString::fromStdString(PlatformUtils::resourceBasePath()) + "/tests/basic-ux/empty.scad";

  // When we open a new file,
  window->tabManager->open(filename);

  window->tabManager->saveAs(window->activeEditor, "test-tmp.scad");

  // The window title must also have the name of open file
  QCOMPARE(window->windowTitle(), "test-tmp.scad");
}
