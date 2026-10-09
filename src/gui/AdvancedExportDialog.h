#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>

class MainWindow;

/*!
   A deliberately minimal "type a command line and run it" dialog.

   This is a spike, not a finished feature. It exists to find out which export options people
   actually reach for, so that a real UI can be built around the ones that matter instead of
   guessed at in advance. It therefore offers no widgets per option: one text field, seeded from
   the current viewport, and a Run button.

   The arguments are executed in-process by run_command_line(), so the run reuses whatever
   geometry the GUI has already evaluated rather than starting from zero the way a separate
   openscad process would.

   Known and accepted for a spike -- see the feature page for row 02:
     - options that mutate process-global state (--enable, --backend) leave the application
       changed afterwards;
     - the run is synchronous, so the GUI is unresponsive while it happens.
 */
class AdvancedExportDialog : public QDialog
{
  Q_OBJECT

public:
  AdvancedExportDialog(const QString& initialArguments, QWidget *parent = nullptr);

private slots:
  void run();

private:
  QLineEdit *argumentsEdit;
  QPlainTextEdit *outputView;
  QPushButton *runButton;
};
