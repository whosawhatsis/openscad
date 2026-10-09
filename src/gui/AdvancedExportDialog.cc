#include "gui/AdvancedExportDialog.h"

#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLabel>
#include <QVBoxLayout>

#include <string>
#include <vector>

#include "command_line.h"
#include "gui/qtgettext.h"

AdvancedExportDialog::AdvancedExportDialog(const QString& initialArguments, QWidget *parent)
  : QDialog(parent)
{
  setWindowTitle(_("Advanced Export"));
  setModal(true);
  resize(720, 320);

  auto *layout = new QVBoxLayout(this);

  layout->addWidget(new QLabel(
    _("Command line arguments, run in this process against the already-rendered model:"), this));

  argumentsEdit = new QLineEdit(initialArguments, this);
  argumentsEdit->setPlaceholderText("-o model.png --imgsize=1920,1080");
  layout->addWidget(argumentsEdit);

  outputView = new QPlainTextEdit(this);
  outputView->setReadOnly(true);
  layout->addWidget(outputView, 1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  runButton = buttons->addButton(_("Run"), QDialogButtonBox::AcceptRole);
  layout->addWidget(buttons);

  connect(runButton, &QPushButton::clicked, this, &AdvancedExportDialog::run);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  // Enter in the text field runs, rather than closing the dialog.
  connect(argumentsEdit, &QLineEdit::returnPressed, this, &AdvancedExportDialog::run);
}

void AdvancedExportDialog::run()
{
  const auto args = split_command_line(argumentsEdit->text().toStdString());
  if (args.empty()) {
    outputView->appendPlainText(_("Nothing to run."));
    return;
  }

  // argv[0] is expected by the option parser; its value is only used in usage messages.
  std::vector<std::string> argv{"openscad"};
  argv.insert(argv.end(), args.begin(), args.end());

  outputView->appendPlainText("> " + argumentsEdit->text());

  // ponytail: synchronous, so the window freezes for the duration. Acceptable for a spike --
  // and how long it freezes for is itself something worth learning before building the real
  // thing. Move to a worker if this ever ships.
  runButton->setEnabled(false);
  QGuiApplication::setOverrideCursor(Qt::WaitCursor);

  std::string error;
  const int rc = run_command_line(argv, error);

  QGuiApplication::restoreOverrideCursor();
  runButton->setEnabled(true);

  if (rc == 0) {
    outputView->appendPlainText(_("Done."));
  } else if (error.empty()) {
    outputView->appendPlainText(_("Failed. See the console for details."));
  } else {
    outputView->appendPlainText(QString::fromStdString(error));
  }
}
