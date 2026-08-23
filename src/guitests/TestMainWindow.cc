#include "TestMainWindow.h"

#include <QString>
#include <QStringList>
#include <QTest>

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
