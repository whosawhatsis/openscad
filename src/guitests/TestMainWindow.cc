#include "TestMainWindow.h"

#include <QScopeGuard>
#include <QString>
#include <QStringList>
#include <QFile>
#include <QTest>

#include <functional>

#include "Feature.h"
#include "core/SourceFile.h"
#include "core/SourceFileCache.h"
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

  QString filename =
    QString::fromStdString(PlatformUtils::resourceBasePath()) + "/tests/basic-ux/empty.scad";

  // When we open a new file,
  window->tabManager->open(filename);

  window->tabManager->saveAs(window->activeEditor, "test-tmp.scad");

  // The window title must also have the name of open file
  QCOMPARE(window->windowTitle(), "test-tmp.scad");
}
