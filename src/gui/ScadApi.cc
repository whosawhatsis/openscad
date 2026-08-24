#include "gui/ScadApi.h"

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <string>
#include <unordered_map>

#include "core/BuiltinContext.h"
#include "core/Builtins.h"
#include "core/EvaluationSession.h"
#include "core/module.h"
#include "core/parsersettings.h"
#include "Feature.h"
#include "gui/ScintillaEditor.h"
#include "gui/UserSymbols.h"

namespace {

bool isInString(const std::u32string& text, const int col)
{
  // first see if we are in a string literal. if so, don't allow auto complete
  bool lastWasEscape = false;
  bool inString = false;
  int dx = 0;
  int count = col;
  while (count-- > 0) {
    const char32_t ch = text.at(dx++);
    if (ch == '\\') lastWasEscape = true;  // next character will be literal handle \"
    else if (lastWasEscape) lastWasEscape = false;
    else if (ch == '"')  // string toggle
      inString = !inString;
  }
  return inString;
}

bool isUseOrInclude(const QString& text, const int col)
{
  const QRegularExpression re("\\s*(use|include)\\s*<[^>]*$");
  const QRegularExpressionMatch match = re.match(text.left(col));
  return match.hasMatch();
}

template <typename C>
QStringList getSorted(const QFileInfoList& list, C cond)
{
  QStringList result;
  for (const auto& info : list) {
    if (cond(info)) {
      result << info.fileName();
    }
  }
  result.sort();
  return result;
}

}  // namespace

ScadApi::ScadApi(ScintillaEditor *editor, QsciLexer *lexer) : QsciAbstractAPIs(lexer), editor(editor)
{
  std::unordered_map<std::string, CompletionItem::Kind> kinds;
  for (const auto& [name, function] : Builtins::instance().getFunctions()) {
    kinds.emplace(name, CompletionItem::Kind::Function);
  }
  for (const auto& [name, module] : Builtins::instance().getModules()) {
    const auto *builtin = dynamic_cast<const BuiltinModule *>(module);
    if (!builtin) continue;
    kinds.emplace(name, builtin->kind() == BuiltinModule::Kind::Leaf
                          ? CompletionItem::Kind::LeafModule
                          : CompletionItem::Kind::ChildModule);
  }

  // keywordList is the authoritative set of completable names: it holds every registered
  // builtin plus the language keywords (else, module, true, ...) that have no registry entry.
  // Deriving completions from it keeps the offered names identical to the pre-CompletionItem
  // behavior; the registries only supply the semantic kind.
  for (const auto& iter : Builtins::keywordList) {
    QStringList calltipList;
    for (const auto& it : iter.second) calltipList.append(QString::fromStdString(it));

    funcs.append(ApiFunc(QString::fromStdString(iter.first), calltipList));

    const auto kind = kinds.find(iter.first);
    completions.append(
      CompletionItem(QString::fromStdString(iter.first),
                     kind == kinds.end() ? CompletionItem::Kind::Keyword : kind->second));
  }
}

void ScadApi::updateAutoCompletionList(const QStringList& context, QStringList& list)
{
  int line, col;
  editor->qsci->getCursorPosition(&line, &col);
  const auto& text = editor->qsci->text(line);

  if (isInString(text.toStdU32String(), col)) {
    return;
  } else if (isUseOrInclude(text, col)) {
    autoCompleteFolder(context, text, col, list);
  } else {
    autoCompleteFunctions(context, list);
  }
}

void ScadApi::autoCompleteFolder(const QStringList& context, const QString& text, const int col,
                                 QStringList& list)
{
  const QRegularExpression re(R"(\s*(use|include)\s*<\s*)");
  const auto useDir = QFileInfo{text.left(col).replace(re, "")}.dir().path();

  QFileInfoList dirs;
  dirs << QFileInfo(editor->filepath);
  for (const auto& path : get_library_path()) {
    dirs << QFileInfo(QString::fromStdString(path) + "/");
  }

  for (const auto& info : dirs) {
    const auto dir = QDir{info.dir().filePath(useDir)};
    if (!dir.exists()) {
      continue;
    }

    QFileInfoList result;
    const auto& prefix = context.last();
    const auto& infoList =
      dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::Readable | QDir::NoDotAndDotDot);
    for (const auto& info : infoList) {
      if (info.fileName().startsWith(prefix) && (info.isDir() || info.suffix().toLower() == "scad")) {
        result << info;
      }
    }

    list << getSorted(result, [](const QFileInfo& i) { return i.isDir(); });
    list << getSorted(result, [](const QFileInfo& i) { return i.isFile(); });
    list.removeDuplicates();
  }
}

void ScadApi::autoCompleteFunctions(const QStringList& context, QStringList& list)
{
  const QString& c = context.last();
  // for now we only auto-complete functions and modules
  if (c.isEmpty()) {
    return;
  }

  for (const auto& item : completions) {
    const QString& name = item.label();
    if (name.startsWith(c)) {
      if (!list.contains(name)) {
        list << name;
      }
    }
  }

  if (Feature::ExperimentalEditorEnhancements.is_enabled()) {
    for (const auto& item : userCompletions) {
      if (item.label().startsWith(c) && !list.contains(item.label())) {
        list << item.label();
      }
    }
    return;
  }

  // for auto-complete on user varables
  if (list.isEmpty()) {
    foreach (const QString& name, userVariableNames) {
      if (name.contains(c)) {
        list.append(name);
      }
    }
  }
}

void ScadApi::autoCompletionSelected(const QString& /*selection*/)
{
}

void ScadApi::completeSelection(const QString& selection)
{
  if (!Feature::ExperimentalEditorEnhancements.is_enabled()) return;

  // User symbols first: a user-defined name shadows a builtin of the same name.
  for (const auto& item : userCompletions + completions) {
    if (item.label() != selection) continue;

    editor->insert(item.insertionSuffix());
    if (item.cursorBack() != 0) {
      int line, col;
      editor->qsci->getCursorPosition(&line, &col);
      editor->qsci->setCursorPosition(line, col - item.cursorBack());
    }
    return;
  }
}

QStringList ScadApi::callTips(const QStringList& context, int /*commas*/,
                              QsciScintilla::CallTipsStyle /*style*/, QList<int>& /*shifts*/)
{
  QStringList callTips;
  for (const auto& func : funcs) {
    if (func.get_name() == context.at(context.size() - 2)) {
      callTips = func.get_params();
      break;
    }
  }
  return callTips;
}

void ScadApi::correctUserVarNamesForCompletionFromSourceFile(const SourceFile *sourceFile,
                                                             bool flagAutoCompleteIncludeVariables,
                                                             bool flagAutoCompleteIncludeModules,
                                                             bool flagAutoCompleteIncludeFunctions)
{
  // No parsed file means the source is currently malformed. Keep the last reliable
  // symbol set rather than emptying the list mid-edit.
  if (!sourceFile) return;

  userVariableNames.clear();
  userCompletions.clear();

  for (const auto& item : userCompletionItems(*sourceFile)) {
    const bool wanted =
      item.kind() == CompletionItem::Kind::Variable
        ? flagAutoCompleteIncludeVariables
        : (item.kind() == CompletionItem::Kind::Function ? flagAutoCompleteIncludeFunctions
                                                         : flagAutoCompleteIncludeModules);
    if (wanted) userCompletions.append(item);
  }

  if (flagAutoCompleteIncludeVariables) {
    for (const auto& assignment : sourceFile->scope->assignments) {
      userVariableNames << QString::fromStdString(assignment->getName());
    }
  }

  if (flagAutoCompleteIncludeModules) {
    for (const auto& [fnname, fn] : sourceFile->scope->getUserModules()) {
      userVariableNames << QString::fromStdString(fnname);
    }
  }

  if (flagAutoCompleteIncludeModules) {
    for (const auto& [modname, mod] : sourceFile->scope->getUserFunctions()) {
      userVariableNames << QString::fromStdString(modname);
    }
  }

  userVariableNames.removeDuplicates();
}

void ScadApi::correctUserVarNamesForCompletionFromInputText(bool flagAutoCompleteIncludeVariables,
                                                            bool flagAutoCompleteIncludeModules,
                                                            bool flagAutoCompleteIncludeFunctions)
{
  const QString text = editor->toPlainText();

  auto fnCollectName = [&text](const QRegularExpression& re, QStringList& target) {
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
      auto match = it.next();
      const QString name = match.captured(1);
      if (!name.isEmpty()) {
        target << name;
      }
    }
  };

  userVariableNames.clear();

  static QRegularExpression const varPattern(R"(^\s*([a-zA-Z_\$][a-zA-Z0-9_]*)\s*=\s*(?!function\b))",
                                             QRegularExpression::MultilineOption);

  static QRegularExpression const modPattern(R"(\bmodule\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\()");

  static QRegularExpression const funcPattern(R"(\bfunction\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*[\(=])");

  if (flagAutoCompleteIncludeVariables) {
    fnCollectName(varPattern, userVariableNames);
  }

  if (flagAutoCompleteIncludeModules) {
    fnCollectName(modPattern, userVariableNames);
  }

  if (flagAutoCompleteIncludeFunctions) {
    fnCollectName(funcPattern, userVariableNames);
  }

  userVariableNames.removeDuplicates();
}
