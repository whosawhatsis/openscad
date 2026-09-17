#include "TestMainWindow.h"
#include "gui/QSettingsCached.h"
#include <QScopeGuard>
#include "openscad.h"
#include <QDoubleSpinBox>

#include <QAction>
#include <QMenu>
#include <QScopeGuard>
#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QImage>
#include <memory>

#include "geometry/Geometry.h"
#include "gui/Editor.h"
#include "gui/parameter/ParameterWidget.h"

#include <functional>

#include "Feature.h"
#include "core/SourceFile.h"
#include "core/SourceFileCache.h"
#include "core/Settings.h"
#include "openscad.h"
#include "gui/ScintillaEditor.h"
#include "core/CSGNode.h"
#include "geometry/PolySet.h"
#include "glview/preview/OpenCSGRenderer.h"
#include "gui/OpenSCADApp.h"
#include "platform/PlatformUtils.h"

void TestMainWindow::checkOpenTabPropagateToWindow()
{
  restoreWindowInitialState();

  QString filename = fixturePath("basic-ux/empty.scad");
  // When we open a new file,
  window->tabManager->open(filename);

  // The window title must also have the name of open file
  QCOMPARE(window->windowTitle(), QFileInfo(filename).fileName());

  filename = fixturePath("basic-ux/empty2.scad");
  // When we open a new file,
  window->tabManager->open(filename);

  // The window title must also have the name of open file
  QCOMPARE(window->windowTitle(), QFileInfo(filename).fileName());
}

void TestMainWindow::checkEditorEnhancementsFeatureFlag()
{
  QCOMPARE(Feature::ExperimentalEditorEnhancements.get_name(), std::string("editor-enhancements"));

  // Whether the feature is on right now is ambient: experimental features persist
  // in QSettings, which this binary shares with the application, so a developer who
  // has enabled it to dogfood would otherwise fail this test. Drive it explicitly
  // and restore what was there.
  const bool wasEnabled = Feature::ExperimentalEditorEnhancements.is_enabled();
  const auto restore =
    qScopeGuard([wasEnabled] { Feature::enable_feature("editor-enhancements", wasEnabled); });

  Feature::enable_feature("editor-enhancements", false);
  QVERIFY(!Feature::ExperimentalEditorEnhancements.is_enabled());
  Feature::enable_feature("editor-enhancements", true);
  QVERIFY(Feature::ExperimentalEditorEnhancements.is_enabled());
}

void TestMainWindow::checkKeywordCompletionRemainsAvailable()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);

  // This covers behaviour with the feature off, so turn it off rather than assume:
  // it persists in QSettings and may be on from dogfooding.
  const bool wasEnabled = Feature::ExperimentalEditorEnhancements.is_enabled();
  const auto restore =
    qScopeGuard([wasEnabled] { Feature::enable_feature("editor-enhancements", wasEnabled); });
  Feature::enable_feature("editor-enhancements", false);

  editor->setupAutoComplete();

  // Language keywords come from Builtins::keywordList, not from the module/function
  // registries, and must keep completing with the experimental feature disabled.
  editor->setPlainText("els");
  editor->setCursorPosition(0, 3);
  editor->triggerCompletion();
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
  editor->triggerCompletion();
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QCOMPARE(editor->toPlainText(), QString("cube();"));

  editor->setPlainText("translat");
  editor->setCursorPosition(0, 8);
  editor->triggerCompletion();
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
  editor->triggerCompletion();
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QVERIFY2(editor->toPlainText().endsWith("wrapper()"),
           qPrintable("got: " + QString(editor->toPlainText()).replace("\n", "\\n")));
  QVERIFY(!editor->toPlainText().endsWith("wrapper();"));

  // While the source is malformed there is no parsed file to harvest. The last
  // reliable symbol set must survive, rather than completion going dead mid-edit.
  editor->correctUserVarNamesForCompletionFromSourceFile(nullptr, true, true, true);
  editor->setPlainText(declarations + "wrap");
  editor->setCursorPosition(2, 4);
  editor->triggerCompletion();
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QVERIFY2(
    editor->toPlainText().endsWith("wrapper()"),
    qPrintable("after malformed parse, got: " + QString(editor->toPlainText()).replace("\n", "\\n")));
}

void TestMainWindow::checkCompletionReusesExistingPunctuation()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);
  Feature::enable_feature("editor-enhancements");
  const auto featureGuard = qScopeGuard([] { Feature::enable_feature("editor-enhancements", false); });
  editor->setupAutoComplete();

  const auto complete = [editor](const QString& text, int col) {
    editor->setPlainText(text);
    editor->setCursorPosition(0, col);
    editor->triggerCompletion();
    QTest::keyClick(editor->qsci, Qt::Key_Tab);
    return editor->toPlainText();
  };

  // Completing over a call that already has arguments must not duplicate the
  // parentheses or append a stray semicolon - the arguments are not ours to touch.
  QCOMPARE(complete("cub(10)", 3), QString("cube(10)"));
  QCOMPARE(complete("translat([1,2,3])", 8), QString("translate([1,2,3])"));

  // Empty parentheses already present are reused, not doubled, and the caret steps
  // inside them so the next keystroke lands where an argument goes.
  QCOMPARE(complete("cub()", 3), QString("cube()"));
  {
    int line = -1, col = -1;
    editor->qsci->getCursorPosition(&line, &col);
    QCOMPARE(col, 5);  // cube(|)
  }

  // With arguments present the caret must not be pushed into them.
  QCOMPARE(complete("cub(10)", 3), QString("cube(10)"));
  {
    int line = -1, col = -1;
    editor->qsci->getCursorPosition(&line, &col);
    QCOMPARE(col, 4);  // cube|(10)
  }

  // An existing semicolon is reused rather than a second one added.
  QCOMPARE(complete("cub;", 3), QString("cube();"));

  // Nothing following: full structure is inserted, as before.
  QCOMPARE(complete("cub", 3), QString("cube();"));
  QCOMPARE(complete("translat", 8), QString("translate()"));
}

void TestMainWindow::checkCompletionFiltersByGrammarContext()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);
  Feature::enable_feature("editor-enhancements");
  const auto featureGuard = qScopeGuard([] { Feature::enable_feature("editor-enhancements", false); });
  editor->setupAutoComplete();

  const auto complete = [editor](const QString& text) {
    editor->setPlainText(text);
    const QStringList lines = text.split('\n');
    editor->setCursorPosition(lines.size() - 1, lines.last().size());
    editor->triggerCompletion();
    QTest::keyClick(editor->qsci, Qt::Key_Tab);
    return editor->toPlainText();
  };

  // In expression position a module must not be offered. "cub" would otherwise
  // complete to cube(); here only the function "cubic"-less builtins apply, so the
  // text must be left exactly as typed.
  QCOMPARE(complete("x = cub"), QString("x = cub\t"));

  // The same prefix in statement position still completes the module.
  QCOMPARE(complete("cub"), QString("cube();"));

  // "sq" prefixes both the module square and the function sqrt, so the context alone
  // decides which one is offered.
  QCOMPARE(complete("x = sq"), QString("x = sqrt()"));
  QCOMPARE(complete("sq"), QString("square();"));

  // A function is not offered where a child module is required. "sqr" reaches the
  // module square as a subsequence, which is legitimate here; what must not happen
  // is the function sqrt being offered in child position.
  const QString inChildPosition = complete("translate([1,0,0]) sqr");
  QVERIFY2(!inChildPosition.contains("sqrt"), qPrintable("offered a function: " + inChildPosition));

  // A transform's child position does offer modules.
  QCOMPARE(complete("translate([1,0,0]) cub"), QString("translate([1,0,0]) cube();"));

  // Nothing is offered inside comments or strings.
  QCOMPARE(complete("// cub"), QString("// cub\t"));
  QCOMPARE(complete("x = \"cub"), QString("x = \"cub\t"));
}

void TestMainWindow::checkNamedParameterCompletion()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);
  Feature::enable_feature("editor-enhancements");
  const auto featureGuard = qScopeGuard([] { Feature::enable_feature("editor-enhancements", false); });
  editor->setupAutoComplete();

  const auto complete = [editor](const QString& text) {
    editor->setPlainText(text);
    editor->setCursorPosition(0, text.size());
    editor->triggerCompletion();
    QTest::keyClick(editor->qsci, Qt::Key_Tab);
    return editor->toPlainText();
  };

  // Inside a call, a parameter of that call completes to "name = " ready for a value.
  QCOMPARE(complete("cube(cent"), QString("cube(center = "));
  QCOMPARE(complete("cylinder(cent"), QString("cylinder(center = "));

  // A parameter of a different call is not offered. "r1" belongs to cylinder; note
  // that a prefix like "h" would be a poor test here, because the function has_key
  // legitimately completes in argument position as a value.
  QCOMPARE(complete("cube(r1"), QString("cube(r1\t"));
  QCOMPARE(complete("cylinder(r1"), QString("cylinder(r1 = "));

  // Outside any call a parameter name is not a candidate at all.
  QCOMPARE(complete("cent"), QString("cent\t"));

  // Having given a name, what follows is an ordinary value: "sq" resolves to the
  // function sqrt rather than the module square, so completion has switched back
  // to expression candidates.
  QCOMPARE(complete("cube(center = sq"), QString("cube(center = sqrt()"));
}

void TestMainWindow::checkCompletionIsCaseInsensitive()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);
  Feature::enable_feature("editor-enhancements");
  const auto featureGuard = qScopeGuard([] { Feature::enable_feature("editor-enhancements", false); });
  editor->setupAutoComplete();

  const auto complete = [editor](const QString& text) {
    editor->setPlainText(text);
    editor->setCursorPosition(0, text.size());
    editor->triggerCompletion();
    QTest::keyClick(editor->qsci, Qt::Key_Tab);
    return editor->toPlainText();
  };

  // A differently-cased prefix still finds the candidate, and the candidate's own
  // spelling is what gets inserted - so completion also corrects the case.
  QCOMPARE(complete("CUB"), QString("cube();"));
  QCOMPARE(complete("Trans"), QString("translate()"));
  QCOMPARE(complete("cub"), QString("cube();"));

  // Case-insensitivity does not reach past the grammar filter: a module is still
  // not offered where a value belongs.
  QCOMPARE(complete("x = CUB"), QString("x = CUB\t"));
}

void TestMainWindow::checkCompletionRanking()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);
  Feature::enable_feature("editor-enhancements");
  const auto featureGuard = qScopeGuard([] { Feature::enable_feature("editor-enhancements", false); });
  editor->setupAutoComplete();

  const QString declarations = "module cubz() { cube(); }\n";
  SourceFile *parsed = nullptr;
  QVERIFY(parse(parsed, declarations.toStdString(), "<test>", "<test>", 0));
  editor->correctUserVarNamesForCompletionFromSourceFile(parsed, true, true, true);

  const auto complete = [editor, &declarations](const QString& typed,
                                                std::function<void()> navigate = {}) {
    editor->setPlainText(declarations + typed);
    const QStringList lines = (declarations + typed).split('\n');
    editor->setCursorPosition(lines.size() - 1, lines.last().size());
    editor->triggerCompletion();
    if (navigate) navigate();
    QTest::keyClick(editor->qsci, Qt::Key_Tab);
    return editor->toPlainText().mid(declarations.size());
  };

  // Equal match quality, so the current file's declaration outranks the builtin -
  // even though "cube" sorts before "cubz" and QScintilla sorts the list.
  QCOMPARE(complete("cub"), QString("cubz();"));

  // The override must not trap the user: arrowing down still reaches the other
  // candidate. Without releasing on navigation the ranked choice would be forced
  // back on every keypress.
  // The list is shown in our order now, so the ranked choice is the first entry and
  // Down is the move that goes anywhere.
  // The override must not trap the user: arrowing away from the ranked choice
  // reaches whatever they select. Asserted as an invariant rather than against a
  // particular neighbour, because the list's contents change as candidate kinds
  // are added - argument shapes already inserted themselves between these two.
  const QString navigated = complete("cub", [editor] { QTest::keyClick(editor->qsci, Qt::Key_Down); });
  QVERIFY2(navigated != "cubz();",
           qPrintable("navigation did not release the ranked override; got: " + navigated));
  QVERIFY2(navigated.startsWith("cube"), qPrintable("unexpected candidate: " + navigated));

  // A subsequence-only match is reachable now that the popup is a user list: we
  // choose the entries, so Scintilla never has to match the typed word.
  QCOMPARE(complete("lex"), QString("linear_extrude()"));
}

void TestMainWindow::checkUsedLibrarySymbolsAreOffered()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);
  Feature::enable_feature("editor-enhancements");
  const auto featureGuard = qScopeGuard([] { Feature::enable_feature("editor-enhancements", false); });
  editor->setupAutoComplete();

  // A real library on disk: the editor resolves `use` through SourceFileCache,
  // which only holds files that were actually parsed.
  const QString libName = "completion-lib-tmp.scad";
  {
    QFile lib(libName);
    QVERIFY(lib.open(QIODevice::WriteOnly | QIODevice::Text));
    lib.write("module libwidget() { cube(); }\nlibrary_setting = 7;\n");
  }

  const QString declarations = QString("use <%1>\n").arg(libName);
  SourceFile *parsed = nullptr;
  QVERIFY(parse(parsed, declarations.toStdString(), "main.scad", "main.scad", 0));
  QVERIFY(parsed != nullptr);
  parsed->handleDependencies();  // parses the used library into SourceFileCache

  // Prove the fixture before testing behaviour: a failure here is the test's
  // setup, not the completion code.
  QVERIFY2(!parsed->usedlibs.empty(), "the `use` was not registered");
  QVERIFY2(SourceFileCache::instance()->lookup(parsed->usedlibs[0]) != nullptr,
           "the used library was not parsed into SourceFileCache");
  editor->correctUserVarNamesForCompletionFromSourceFile(parsed, true, true, true);

  const auto complete = [editor, &declarations](const QString& typed) {
    editor->setPlainText(declarations + typed);
    const QStringList lines = (declarations + typed).split('\n');
    editor->setCursorPosition(lines.size() - 1, lines.last().size());
    editor->triggerCompletion();
    QTest::keyClick(editor->qsci, Qt::Key_Tab);
    return editor->toPlainText().mid(declarations.size());
  };

  // A module from the used library completes, with its structure.
  QCOMPARE(complete("libwid"), QString("libwidget();"));

  // A variable does not cross a `use`, so it is not a candidate.
  QCOMPARE(complete("library_set"), QString("library_set\t"));

  QFile::remove(libName);
}

void TestMainWindow::checkArgumentShapeCompletion()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);
  Feature::enable_feature("editor-enhancements");
  const auto featureGuard = qScopeGuard([] { Feature::enable_feature("editor-enhancements", false); });
  editor->setupAutoComplete();

  const auto complete = [editor](const QString& typed, int downs = 0) {
    editor->setPlainText(typed);
    editor->setCursorPosition(0, typed.size());
    editor->triggerCompletion();
    for (int i = 0; i < downs; ++i) QTest::keyClick(editor->qsci, Qt::Key_Down);
    QTest::keyClick(editor->qsci, Qt::Key_Tab);
    return editor->toPlainText();
  };

  // The bare structure stays the default: a shape never displaces it.
  QCOMPARE(complete("transl"), QString("translate()"));

  // The seeded shape sits directly below it, and is inserted verbatim - complete,
  // valid, and a no-op until a field is edited.
  QCOMPARE(complete("transl", 1), QString("translate([0, 0, 0])"));

  // Nothing is appended to a shape: no second parenthesis, no stray semicolon.
  QCOMPARE(complete("scal", 1), QString("scale([1, 1, 1])"));

  // mirror is seeded too: a zero vector is a no-op, and one digit gives the
  // mirror that was actually wanted.
  QCOMPARE(complete("mirro"), QString("mirror()"));
  QCOMPARE(complete("mirro", 1), QString("mirror([0, 0, 0])"));
}

void TestMainWindow::checkSnippetFieldTraversal()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);
  Feature::enable_feature("editor-enhancements");
  const auto featureGuard = qScopeGuard([] { Feature::enable_feature("editor-enhancements", false); });
  editor->setupAutoComplete();

  // Accept the seeded shape, one entry below the bare structure.
  editor->setPlainText("transl");
  editor->setCursorPosition(0, 6);
  editor->triggerCompletion();
  QTest::keyClick(editor->qsci, Qt::Key_Down);
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QCOMPARE(editor->toPlainText(), QString("translate([0, 0, 0])"));

  // The first field is selected, so typing replaces its seeded value outright.
  QVERIFY(editor->snippetSessionActive());
  QCOMPARE(editor->qsci->selectedText(), QString("0"));
  QTest::keyClicks(editor->qsci, "5");
  QCOMPARE(editor->toPlainText(), QString("translate([5, 0, 0])"));

  // Tab steps to the next field - and the marks must have survived the edit,
  // which is why they are Scintilla indicators rather than stored offsets.
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QCOMPARE(editor->qsci->selectedText(), QString("0"));
  QTest::keyClicks(editor->qsci, "12");
  QCOMPARE(editor->toPlainText(), QString("translate([5, 12, 0])"));

  // Shift-Tab goes back to the field just edited.
  QTest::keyClick(editor->qsci, Qt::Key_Backtab);
  QCOMPARE(editor->qsci->selectedText(), QString("5"));

  // Tab past the last field finishes the call and leaves it behind.
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QVERIFY(!editor->snippetSessionActive());
  int line = -1, col = -1;
  editor->qsci->getCursorPosition(&line, &col);
  QCOMPARE(col, editor->toPlainText().size());

  // With no session running, Tab is an ordinary indent again.
  const QString before = editor->toPlainText();
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QVERIFY2(editor->toPlainText() != before, "Tab no longer indents once the session has ended");
}

void TestMainWindow::checkCaretAndTerminatorFromRealUse()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);
  Feature::enable_feature("editor-enhancements");
  const auto featureGuard = qScopeGuard([] { Feature::enable_feature("editor-enhancements", false); });
  editor->setupAutoComplete();

  const auto completeWord = [editor](const QString& typed, int downs = 0) {
    editor->setPlainText(typed);
    editor->setCursorPosition(0, typed.size());
    editor->triggerCompletion();
    for (int i = 0; i < downs; ++i) QTest::keyClick(editor->qsci, Qt::Key_Down);
    QTest::keyClick(editor->qsci, Qt::Key_Tab);
    int line = -1, col = -1;
    editor->qsci->getCursorPosition(&line, &col);
    return QPair<QString, int>(editor->toPlainText(), col);
  };

  // Typing the whole word and accepting must leave the caret between the
  // parentheses, ready for arguments - not after them.
  const auto whole = completeWord("translate");
  QCOMPARE(whole.first, QString("translate()"));
  QCOMPARE(whole.second, 10);  // translate(|)

  // Same for a partially typed word.
  const auto partial = completeWord("transl");
  QCOMPARE(partial.first, QString("translate()"));
  QCOMPARE(partial.second, 10);

  // A leaf module's seeded shape terminates the statement, exactly as its bare
  // structure does. cube() completes as cube(); so cube([1,1,1]) must too.
  const auto shape = completeWord("cub", 1);
  QCOMPARE(shape.first, QString("cube([1, 1, 1]);"));

  // A child module's shape must not be terminated: something follows it.
  const auto childShape = completeWord("transl", 1);
  QCOMPARE(childShape.first, QString("translate([0, 0, 0])"));
}

void TestMainWindow::checkTypingOpensTheCompletionList()
{
  restoreWindowInitialState();
  auto *editor = dynamic_cast<ScintillaEditor *>(window->activeEditor);
  QVERIFY(editor);
  Feature::enable_feature("editor-enhancements");
  const auto featureGuard = qScopeGuard([] { Feature::enable_feature("editor-enhancements", false); });
  editor->setupAutoComplete();

  // Everything else drives the popup through triggerCompletion(). This is the path
  // a person actually takes: QScintilla's own trigger is switched off while the
  // popup is ours, so typing has to open it via SCN_CHARADDED.
  editor->setPlainText("");
  editor->setCursorPosition(0, 0);
  editor->qsci->setFocus();
  QTest::keyClicks(editor->qsci, "transl");
  QVERIFY2(editor->qsci->isListActive(), "typing did not open the completion list");
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QCOMPARE(editor->toPlainText(), QString("translate()"));

  // And the spacing of a seeded shape survives insertion, which is the whole point
  // of driving a user list: QScintilla trims autocompletion entries at their first
  // space, but not user-list entries.
  editor->setPlainText("");
  editor->setCursorPosition(0, 0);
  QTest::keyClicks(editor->qsci, "transl");
  QTest::keyClick(editor->qsci, Qt::Key_Down);
  QTest::keyClick(editor->qsci, Qt::Key_Tab);
  QCOMPARE(editor->toPlainText(), QString("translate([0, 0, 0])"));
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

  QString filename = fixturePath("basic-ux/empty.scad");
  // When we open a new file,
  window->tabManager->open(filename);

  window->tabManager->saveAs(window->activeEditor, "test-tmp.scad");

  // The window title must also have the name of open file
  QCOMPARE(window->windowTitle(), "test-tmp.scad");
}

void TestMainWindow::checkAdvancedExportActionAvailable()
{
  auto *action = window->findChild<QAction *>("fileActionAdvancedExport");
  QVERIFY(action);
  QVERIFY(window->menuExport->actions().contains(action));
  QCOMPARE(action->text(), "Advanced Export...");
}

void TestMainWindow::checkChangingColorSchemeRecolorsPreparedPreview()
{
#ifdef ENABLE_OPENCSG
  restoreWindowInitialState();
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));
  window->qglview->setColorScheme("Cornfield");
  window->activeEditor->setPlainText("cube(100, center = true);");

  QVERIFY(QMetaObject::invokeMethod(window, "on_designActionPreview_triggered"));
  QTRY_VERIFY_WITH_TIMEOUT(window->previewRenderer != nullptr, 10000);
  window->qglview->repaint();
  const auto before = window->qglview->grabFramebuffer();
  QVERIFY(!before.isNull());
  const auto cornfield = before.pixelColor(before.width() / 2, before.height() / 2);

  window->qglview->setColorScheme("Starnight");
  window->qglview->repaint();
  const auto after = window->qglview->grabFramebuffer();
  QVERIFY(!after.isNull());
  const auto starnight = after.pixelColor(after.width() / 2, after.height() / 2);

  QVERIFY2(cornfield != starnight,
           "Changing schemes left the prepared preview colored by the previous scheme");
#endif
}

void TestMainWindow::checkCachedPreviewDistinguishesMaterialFromUncoloredGeometry()
{
#ifdef ENABLE_OPENCSG
  restoreWindowInitialState();
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));
  window->qglview->setColorScheme("Cornfield");

  const auto previous = Settings::SettingsMaterials::materialColors.value();
  Settings::SettingsMaterials::materialColors.setValue("PLA=#ffff00ff");

  const auto preview = [this](const QString& source) -> QImage {
    bool compiled = false;
    QObject::connect(
      window, &MainWindow::compilationDone, window, [&compiled](SourceFile *) { compiled = true; },
      Qt::SingleShotConnection);
    window->activeEditor->setPlainText(source);
    window->designActionPreview->trigger();
    QElapsedTimer timer;
    timer.start();
    while (!compiled && timer.elapsed() < 10000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    if (!compiled) return {};
    window->qglview->repaint();
    return window->qglview->grabFramebuffer().copy();
  };

  const QImage material = preview("material(\"PLA\") cube(100, center = true);");
  const QImage uncolored = preview("cube(100, center = true);");
  Settings::SettingsMaterials::materialColors.setValue(previous);

  QVERIFY(!material.isNull());
  QVERIFY(!uncolored.isNull());
  const QPoint center(uncolored.width() / 2, uncolored.height() / 2);
  QVERIFY2(material.pixelColor(center) != uncolored.pixelColor(center),
           "the cached PLA preview colored otherwise uncolored geometry");

  window->qglview->zoom(120, true);
  window->qglview->repaint();
  const QImage moved = window->qglview->grabFramebuffer().copy();
  QVERIFY2(uncolored.pixelColor(center) == moved.pixelColor(center),
           "moving the camera changed an uncolored object to a cached material color");
#endif
}

void TestMainWindow::checkRepeatPreviewOfManyProductsReusesCachedBuffers()
{
#ifdef ENABLE_OPENCSG
  // 150 separate cubes are 150 products. A cache capped at 100 entries evicts part of the model
  // it just built, so previewing it again rebuilds those products every time -- exactly the
  // heavy models the cache exists to help.
  restoreWindowInitialState();
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));
  window->activeEditor->setPlainText("for (i = [0:149]) translate([i * 3, 0, 0]) cube(1);");

  // Wait on the compile itself: a new renderer can be allocated at the old one's address, so
  // watching the pointer change is not a reliable signal.
  const auto previewAndPaint = [this]() {
    bool compiled = false;
    const auto connection = QObject::connect(window, &MainWindow::compilationDone, window,
                                             [&compiled](SourceFile *) { compiled = true; });
    QMetaObject::invokeMethod(window, "on_designActionPreview_triggered");
    QTRY_VERIFY_WITH_TIMEOUT(compiled, 20000);
    QObject::disconnect(connection);
    window->qglview->repaint();
  };

  const auto rootProducts = [this]() -> std::shared_ptr<CSGProducts> {
    const auto renderer = std::dynamic_pointer_cast<OpenCSGRenderer>(window->previewRenderer);
    return renderer ? renderer->rootProductsForTest() : nullptr;
  };
  // Held, not just its address: once the first renderer is gone its leaf could be freed and a new
  // one allocated at the same address, which would look like reuse.
  const auto firstLeaf = [&rootProducts]() -> std::shared_ptr<const PolySet> {
    const auto products = rootProducts();
    if (!products || products->products.empty()) return nullptr;
    const auto& product = products->products.front();
    return product.intersections.empty() ? nullptr : product.intersections.front().leaf->polyset;
  };

  const auto beforeFirst = OpenCSGRenderer::vboBuildsForTest();
  previewAndPaint();
  const auto afterFirst = OpenCSGRenderer::vboBuildsForTest();
  // The first paint has to have built the model, or the repeat's count measures the first build.
  QVERIFY2(afterFirst - beforeFirst >= 150,
           qPrintable(QStringLiteral("the first preview's paint built %1 products, expected 150")
                        .arg(static_cast<qulonglong>(afterFirst - beforeFirst))));
  const auto leafBefore = firstLeaf();
  const auto productCount = rootProducts() ? rootProducts()->products.size() : 0;
  previewAndPaint();
  const auto rebuilt = OpenCSGRenderer::vboBuildsForTest() - afterFirst;
  // The cache is keyed by PolySet identity, so this has to hold before the count means anything.
  QVERIFY2(leafBefore && leafBefore == firstLeaf(),
           qPrintable(QStringLiteral("repeat preview gave new leaf geometry (%1 products)")
                        .arg(static_cast<qulonglong>(productCount))));
  QVERIFY2(rebuilt == 0, qPrintable(QStringLiteral("the repeat preview rebuilt %1 of 150 products")
                                      .arg(static_cast<qulonglong>(rebuilt))));
#endif
}

void TestMainWindow::checkClosingWindowDoesNotUseFreedMembers()
{
  restoreWindowInitialState();

  const int windowCountBefore = scadApp->windowManager.getWindows().size();

  auto *extraWindow = new MainWindow(QStringList());
  QCOMPARE(scadApp->windowManager.getWindows().size(), windowCountBefore + 1);

  // Closing a window destroys its members, and destroying a child widget delivers events while
  // that is happening. MainWindow is still installed as an event filter at that point, so
  // MainWindow::eventFilter() can run against members that have already been destroyed and read
  // freed memory. That is silent in an ordinary build and a heap-use-after-free under
  // AddressSanitizer, which is what this test exists to catch.
  extraWindow->close();
  QCoreApplication::processEvents();

  QCOMPARE(scadApp->windowManager.getWindows().size(), windowCountBefore);
// ------------------------------------------------------------------------------------------
// Process isolation, from the window's point of view.
//
// Everything below the window has its own tests -- the channel, the codec, the worker's request
// path. What none of them can show is that a *window* actually uses any of it: that the flag is
// read, that a worker is started with the window, that a render takes the isolated branch, and
// that the geometry comes back to the same place the in-process path puts it. Without this, the
// feature is proven everywhere except where the user meets it.
//
// The window is constructed here rather than reused from the fixture because process isolation is
// latched at construction; the fixture's window was built before the flag was set.

namespace {

//! Runs `source` in a window of its own -- rendered (F6) or previewed (F5) -- and returns the
//! window once it has finished compiling, or null if it never did.
MainWindow *runInOwnWindow(const QString& source, const bool preview)
{
  // Heap-allocated and released through the event loop, never destroyed on the stack. A window is
  // the target of queued connections and timers; tearing one down while the application is still
  // delivering events to it crashes in whatever happens to touch it next -- which cost a
  // backtrace to find, landing in formatIdentifierToAction() reading exportMap on a dead window.
  auto *window = new MainWindow{QStringList{}};
  window->activeEditor->setPlainText(source);

  bool compiled = false;
  // Disconnected before returning: the window outlives this frame, and a later compile on it would
  // otherwise write through a reference to a local that no longer exists.
  const auto connection = QObject::connect(window, &MainWindow::compilationDone, window,
                                           [&compiled](SourceFile *) { compiled = true; });

  if (preview) window->designActionPreview->trigger();
  else window->designActionRender->trigger();

  // The work crosses a process boundary, so it is slower than an in-process one and the wait has
  // to be generous. It still has to end: a window left waiting forever is the failure this whole
  // feature exists to prevent, so timing out here is a real result and not a flake.
  QElapsedTimer timer;
  timer.start();
  while (!compiled && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  QObject::disconnect(connection);

  // Deliberately not destroyed. A MainWindow registers itself with application-wide state and is
  // the target of queued connections; tearing one down inside a running test process crashes in
  // whatever touches it next. The test process exits shortly after, so leaking it is the cheaper
  // and more honest option than pretending the teardown is safe.
  return compiled ? window : nullptr;
}

//! Renders again in a window that has already finished one, and waits for it the same way.
void renderAgain(MainWindow *window)
{
  bool compiled = false;
  const auto connection = QObject::connect(window, &MainWindow::compilationDone, window,
                                           [&compiled](SourceFile *) { compiled = true; });
  window->designActionRender->trigger();
  QElapsedTimer timer;
  timer.start();
  while (!compiled && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  QObject::disconnect(connection);
  QVERIFY2(compiled, "the second render never finished");
}

}  // namespace

void TestMainWindow::checkIsolatedRenderProducesGeometry()
{
  Feature::enable_feature("process-isolation");
  auto *window = runInOwnWindow(QStringLiteral("cube([10, 10, 10]);"), false);
  Feature::enable_feature("process-isolation", false);

  QVERIFY2(window != nullptr, "an isolated render never finished");
  const auto polyset = std::dynamic_pointer_cast<const PolySet>(window->rootGeom);
  QVERIFY2(polyset != nullptr, "the isolated result was not a mesh");
  QCOMPARE(polyset->vertices.size(), size_t{8});
}

void TestMainWindow::checkIsolatedPreviewProducesProducts()
{
  // A preview crosses the same boundary as a render, but comes back as a product list rather than
  // one mesh. The window has to composite what the worker sent; until it does, F5 under isolation
  // is either wrong or never ends.
  Feature::enable_feature("process-isolation");
  auto *window = runInOwnWindow(QStringLiteral("cube([10, 10, 10]);"), true);
  Feature::enable_feature("process-isolation", false);

  QVERIFY2(window != nullptr, "an isolated preview never finished");
  const auto& products = window->previewProductsForTest();
  QVERIFY2(products != nullptr, "an isolated preview produced no product list");
  QCOMPARE(products->size(), size_t{1});
  // Products alone would also be there if the window had quietly previewed in-process, which is
  // exactly what it did before this was wired -- so the test has to say where they came from.
  QCOMPARE(window->isolatedPreviewsForTest(), 1);
}

void TestMainWindow::checkIsolatedAutoReloadPreviewUsesWorker()
{
  // Auto-reload ends in csgReloadRender, not csgRender. If only F5's continuation knows about the
  // worker, every save-triggered preview quietly runs in this process: blocking the window and
  // evaluating a model the worker never saw.
  const QString path = QDir::temp().filePath(QStringLiteral("openscad-isolated-autoreload.scad"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
  file.write("cube([10, 10, 10]);");
  file.close();

  // Toggling auto-reload writes design/autoReload, and this test binary shares its settings with the
  // user's real application. Put back the value that was there -- not an assumed default -- however
  // this test exits, including a failed check's early return. Written straight to the settings rather
  // than through the action, which would restart this window's timer on the way out.
  const auto originalAutoReload = QSettingsCached{}.value("design/autoReload");
  const auto restoreAutoReload = qScopeGuard(
    [&originalAutoReload] { QSettingsCached{}.setValue("design/autoReload", originalAutoReload); });

  Feature::enable_feature("process-isolation");
  auto *window = new MainWindow{QStringList{}};  // leaked for the reason runInOwnWindow gives
  // Unlike runInOwnWindow's windows this one has a real file, so the auto-reload timer compileEnded
  // starts would keep previewing it for the rest of the run -- into a lambda whose captured local
  // is long gone. Both are stopped before this returns.
  window->designActionAutoReload->setChecked(false);
  window->activeEditor->filepath = path;
  bool compiled = false;
  const auto connection = QObject::connect(window, &MainWindow::compilationDone, window,
                                           [&compiled](SourceFile *) { compiled = true; });

  window->actionReloadRenderPreview();
  QElapsedTimer timer;
  timer.start();
  while (!compiled && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  QObject::disconnect(connection);
  window->designActionAutoReload->setChecked(false);
  Feature::enable_feature("process-isolation", false);

  QVERIFY2(compiled, "the auto-reload preview never finished");
  QVERIFY2(window->previewProductsForTest() != nullptr, "the auto-reload preview produced nothing");
  QCOMPARE(window->isolatedPreviewsForTest(), 1);
}

void TestMainWindow::checkIsolatedPreviewUsesGuiColorScheme()
{
#ifdef ENABLE_OPENCSG
  Feature::enable_feature("process-isolation");
  auto *previewWindow = runInOwnWindow(
    QStringLiteral(
      "render() difference() { cube(100, center = true); cylinder(r = 12, h = 200, center = true); }"),
    true);
  Feature::enable_feature("process-isolation", false);
  QVERIFY2(previewWindow != nullptr, "the isolated preview never finished");

  previewWindow->show();
  QVERIFY(QTest::qWaitForWindowExposed(previewWindow));
  previewWindow->qglview->setColorScheme("DeepOcean");
  previewWindow->qglview->zoom(120, true);
  previewWindow->qglview->repaint();
  const QImage moved = previewWindow->qglview->grabFramebuffer().copy();
  QVERIFY(!moved.isNull());

  qsizetype cornfieldPixels = 0;
  for (int y = 0; y < moved.height(); ++y) {
    for (int x = 0; x < moved.width(); ++x) {
      const auto color = moved.pixelColor(x, y);
      if (color.red() > 150 && color.green() > 130 && color.blue() < 80) ++cornfieldPixels;
    }
  }
  QCOMPARE(cornfieldPixels, 0);
#endif
}

void TestMainWindow::checkIsolatedRenderTagsSchemeColors()
{
  // F5 has checkIsolatedPreviewUsesGuiColorScheme; F6 had nothing, and it takes a different path --
  // a single mesh through exportFileByName rather than a product list.
  //
  // The invariant is that the worker does NOT resolve implicit face colors: it cannot, because the
  // color scheme belongs to the window and the worker has its own, unrelated one. It sends tags and
  // the window resolves them against the scheme it is showing. If the worker ever sent resolved
  // colors instead, the rendered object would keep the worker's scheme no matter what the user
  // selects -- checked here on the geometry rather than on pixels, which in this harness depend on
  // window exposure and view mode.
  Feature::enable_feature("process-isolation");
  auto *window = runInOwnWindow(
    QStringLiteral(
      "difference() { cube(100, center = true); cylinder(r = 12, h = 200, center = true); }"),
    false);
  Feature::enable_feature("process-isolation", false);
  QVERIFY2(window != nullptr, "the isolated render never finished");

  const auto polyset = std::dynamic_pointer_cast<const PolySet>(window->rootGeom);
  QVERIFY2(polyset != nullptr, "the isolated render produced no PolySet");
  QVERIFY2(!polyset->color_indices.empty(),
           "the render carried no per-face color data at all, so the check below proves nothing");

  const bool tagged =
    std::any_of(polyset->color_indices.begin(), polyset->color_indices.end(), [](int32_t index) {
      return index == PolySet::COLOR_INDEX_DEFAULT || index == PolySet::COLOR_INDEX_CUTOUT;
    });
  QVERIFY2(tagged,
           "the worker resolved implicit face colors itself instead of tagging them; the rendered "
           "object will keep the worker's color scheme whatever the user selects");
}

// The Customizer's policy is that once the user touches a value it wins until the document closes.
// A value the user has NEVER touched must not win: editing the variable's default in the text has to
// take effect on the first render, as it does with the feature off. Sending the widget's values
// unconditionally makes every edit render one step behind -- and F6 then exports that stale mesh.
void TestMainWindow::checkUntouchedCustomizerDoesNotOverrideEditedText()
{
  Feature::enable_feature("process-isolation");
  // A name no other test uses: a Customizer value outlives a complete source replacement.
  auto *window =
    runInOwnWindow(QStringLiteral("outrank_size = 10; // [10:100]\ncube(outrank_size);"), false);
  Feature::enable_feature("process-isolation", false);
  QVERIFY2(window != nullptr, "the first isolated render never finished");
  QCOMPARE(window->rootGeom->getBoundingBox().max().x(), 10.0);

  // Edit the default with the Customizer untouched: the edit must win.
  window->rootGeom.reset();
  window->activeEditor->setPlainText(
    QStringLiteral("outrank_size = 40; // [10:100]\ncube(outrank_size);"));
  renderAgain(window);
  QVERIFY(window->rootGeom != nullptr);
  QCOMPARE(window->rootGeom->getBoundingBox().max().x(), 40.0);

  // ...and once touched, the Customizer wins, which is the escape hatch the user needs. Look the
  // widget up now, not earlier: setParameters() rebuilds them whenever the source changes.
  window->rootGeom.reset();
  auto *spinBox = window->activeEditor->parameterWidget->findChild<QDoubleSpinBox *>("doubleSpinBox");
  QVERIFY2(spinBox != nullptr, "no Customizer spin box for the parameter");
  spinBox->setValue(70);
  renderAgain(window);
  QVERIFY(window->rootGeom != nullptr);
  QCOMPARE(window->rootGeom->getBoundingBox().max().x(), 70.0);
}

// -D definitions are appended to the text the window parses. The worker parses its own copy of the
// document, so unless they are appended there too, `openscad -D size=7 model.scad` renders one thing
// in the GUI and another under isolation -- silently, since nothing reports the difference.
void TestMainWindow::checkIsolatedRenderUsesCommandLineDefinitions()
{
  const auto previousCommands = commandline_commands;
  commandline_commands = "size = 7;\n";
  Feature::enable_feature("process-isolation");
  auto *window = runInOwnWindow(QStringLiteral("cube(size);"), false);
  Feature::enable_feature("process-isolation", false);
  commandline_commands = previousCommands;

  QVERIFY2(window != nullptr, "the isolated render never finished");
  QVERIFY2(window->rootGeom != nullptr, "the isolated render produced no geometry");
  QCOMPARE(window->rootGeom->getBoundingBox().max().x(), 7.0);
}

namespace {

// Slow enough in the worker that a second F5 reliably arrives while it is still computing, without
// making the suite slow. render() forces a real boolean rather than a cheap product list; `salt`
// keeps each test's model out of the other's cache.
QString slowPreviewModel(int salt)
{
  return QStringLiteral(
           "render() union() { for (i = [0:70]) translate([i * 6 + %1, 0, 0]) sphere(3, $fn = 72); }")
    .arg(salt);
}

//! Pumps events until the windows have been idle for a short stretch, so a preview that re-runs
//! itself on completion has had its chance to start and finish. Idle means the windows themselves
//! say so: an isolated window releases the application-wide lock while its worker computes.
bool waitUntilSettled(std::initializer_list<MainWindow *> windows, int timeoutMs = 90000)
{
  QElapsedTimer total;
  total.start();
  QElapsedTimer idle;
  bool idleRunning = false;
  while (total.elapsed() < timeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const bool busy =
      GuiLocker::isLocked() ||
      std::any_of(windows.begin(), windows.end(), [](MainWindow *w) { return w->isBusyForTest(); });
    if (busy) {
      idleRunning = false;
    } else if (!idleRunning) {
      idle.start();
      idleRunning = true;
    } else if (idle.elapsed() > 750) {
      return true;
    }
  }
  return false;
}

}  // namespace

// Pressing F5 while an isolated preview is still computing must not be lost. In-process, compile()
// runs synchronously, so a second F5 caught during it is re-run straight afterwards; under isolation
// compile() returns at once and the worker answers later, so that check had already passed and the
// request was silently dropped -- the window kept showing the OLD model after the user asked for the
// new one. The edited model must end up on screen, and it should not have to wait for the stale
// preview to run to completion first.
void TestMainWindow::checkF5DuringAnInFlightPreviewShowsTheEditedModel()
{
  Feature::enable_feature("process-isolation");
  auto *window = new MainWindow{QStringList{}};
  Feature::enable_feature("process-isolation", false);
  window->activeEditor->setPlainText(slowPreviewModel(1));
  window->designActionPreview->trigger();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  QVERIFY2(window->isBusyForTest(),
           "the first preview finished before the second F5, so this would not exercise an in-flight "
           "preview at all");

  window->activeEditor->setPlainText(QStringLiteral("cube(7);"));
  window->designActionPreview->trigger();

  QVERIFY2(waitUntilSettled({window}), "the window never settled after the second F5");
  const auto products = window->previewProductsForTest();
  QVERIFY2(products != nullptr, "no preview is showing");
  QCOMPARE(products->getBoundingBox().max().x(), 7.0);
}

// The other half: pressing F5 again with nothing changed while a preview is computing must not queue
// a second, identical preview behind it. That would redo seconds of work for a result already on the
// way. Once the request above is no longer dropped, this is what keeps it from being repeated.
void TestMainWindow::checkIdenticalPreviewRequestIsNotRepeated()
{
  Feature::enable_feature("process-isolation");
  auto *window = new MainWindow{QStringList{}};
  Feature::enable_feature("process-isolation", false);
  window->activeEditor->setPlainText(slowPreviewModel(2));
  const auto before = window->isolatedPreviewsForTest();
  const auto requestsBefore = window->isolatedPreviewRequestsForTest();

  window->designActionPreview->trigger();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  QVERIFY2(window->isBusyForTest(), "the first preview finished before the repeat F5 arrived");
  window->designActionPreview->trigger();

  QVERIFY2(waitUntilSettled({window}), "the window never settled after the repeat F5");
  QCOMPARE(window->isolatedPreviewsForTest() - before, 1);
  // One request, not two. Cancelling the first preview and running the same thing again would also
  // finish with a single completed preview, having thrown the first one's work away.
  QCOMPARE(window->isolatedPreviewRequestsForTest() - requestsBefore, 1);
}

// Each window has its own worker process, so one window's slow preview must not hold up another's.
// That is half the reason for isolation: a heavy model in one window should leave the rest usable.
// The second window's quick preview has to finish -- showing its own model -- while the first is
// still computing, rather than waiting its turn behind it.
void TestMainWindow::checkIsolatedWindowsPreviewConcurrently()
{
  Feature::enable_feature("process-isolation");
  auto *slow = new MainWindow{QStringList{}};
  auto *quick = new MainWindow{QStringList{}};
  Feature::enable_feature("process-isolation", false);

  slow->activeEditor->setPlainText(slowPreviewModel(3));
  const auto slowBefore = slow->isolatedPreviewsForTest();
  slow->designActionPreview->trigger();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  QCOMPARE(slow->isolatedPreviewsForTest(), slowBefore);  // the slow preview is really in flight

  quick->activeEditor->setPlainText(QStringLiteral("cube(9);"));
  const auto quickBefore = quick->isolatedPreviewsForTest();
  quick->designActionPreview->trigger();

  QElapsedTimer timer;
  timer.start();
  while (quick->isolatedPreviewsForTest() == quickBefore && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  QVERIFY2(quick->isolatedPreviewsForTest() > quickBefore,
           "the second window's preview never ran: it was blocked by the first window's");
  QVERIFY2(slow->isolatedPreviewsForTest() == slowBefore,
           "the second window only finished after the first did, so they ran one after another");
  QCOMPARE(quick->previewProductsForTest()->getBoundingBox().max().x(), 9.0);

  QVERIFY2(waitUntilSettled({slow, quick}), "the windows never settled");
}

void TestMainWindow::checkIsolatedRenderUsesCustomizerValues()
{
  // The worker parses its own copy of the source, so without being told the Customizer's values it
  // renders the document's defaults -- an F6 (and any export taken from it) that quietly ignores
  // what the user set in the pane.
  Feature::enable_feature("process-isolation");
  auto *window = runInOwnWindow(QStringLiteral("pw_side = 5; cube([pw_side, 5, 5]);"), false);
  QVERIFY2(window != nullptr, "the first isolated render never finished");
  auto polyset = std::dynamic_pointer_cast<const PolySet>(window->rootGeom);
  QVERIFY(polyset != nullptr);
  // Guards the measurement: if the default were already 20 the assertion below would prove nothing.
  QCOMPARE(polyset->getBoundingBox().sizes().x(), 5.0);

  ParameterSet values;
  values["pw_side"].put_value<double>(20);
  window->activeEditor->parameterWidget->importValuesForTest(values);

  // The source is unchanged, so setParameters() keeps the values just injected.
  renderAgain(window);
  Feature::enable_feature("process-isolation", false);

  polyset = std::dynamic_pointer_cast<const PolySet>(window->rootGeom);
  QVERIFY2(polyset != nullptr, "the second isolated render produced no mesh");
  QCOMPARE(polyset->getBoundingBox().sizes().x(), 20.0);
}

void TestMainWindow::checkInProcessPreviewProducesProducts()
{
  // The control for the isolated preview test: same window, same trigger, isolation off. If this
  // hangs too, the problem is previewing in a window the test never showed -- not the worker.
  Feature::enable_feature("process-isolation", false);
  auto *window = runInOwnWindow(QStringLiteral("cube([10, 10, 10]);"), true);
  QVERIFY2(window != nullptr, "the in-process preview never finished either");
  QVERIFY2(window->previewProductsForTest() != nullptr, "no product list in-process");
}

void TestMainWindow::checkAWindowWhoseWorkerCannotStartStillRenders()
{
  // The fallback matters more than it looks: a user whose worker cannot start -- a broken install,
  // a policy blocking the executable, a sandbox -- must still be able to render, rather than find
  // the application useless until they discover a preference.
  //
  // With the flag off, the same window takes the in-process path, which is exactly the state the
  // fallback leaves it in.
  Feature::enable_feature("process-isolation", false);
  auto *window = runInOwnWindow(QStringLiteral("cube([10, 10, 10]);"), false);

  QVERIFY2(window != nullptr, "an in-process render never finished");
  QVERIFY(std::dynamic_pointer_cast<const PolySet>(window->rootGeom) != nullptr);
}
