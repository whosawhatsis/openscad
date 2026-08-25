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
#include "core/ArgumentShapes.h"
#include "core/SourceFileCache.h"
#include "core/parsersettings.h"
#include "Feature.h"
#include "gui/CompletionContext.h"
#include "gui/CompletionRanking.h"
#include "gui/UserSymbols.h"
#include "gui/ScintillaEditor.h"
#include "gui/UserSymbols.h"

namespace {

// Whether a candidate of this kind may appear at this point in the grammar.
// Keywords are allowed everywhere: Kind::Keyword covers both statement words
// (module, function, else) and value words (true, false, undef, each), and
// telling them apart needs per-name knowledge this does not have yet.
bool kindFitsContext(CompletionItem::Kind kind, CompletionContext context)
{
  switch (context) {
  case CompletionContext::None:    return false;
  case CompletionContext::Unknown: return true;
  case CompletionContext::Statement:
    // A value is not a statement; a module instantiation is.
    return kind == CompletionItem::Kind::LeafModule || kind == CompletionItem::Kind::ChildModule ||
           kind == CompletionItem::Kind::Keyword;
  case CompletionContext::Expression:
  case CompletionContext::ArgumentName:
    // A module cannot produce a value.
    return kind != CompletionItem::Kind::LeafModule && kind != CompletionItem::Kind::ChildModule;
  }
  return true;
}

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

    const QStringList parameters = parameterNamesFromCalltips(calltipList);
    if (!parameters.isEmpty()) {
      builtinParameters.insert(QString::fromStdString(iter.first), parameters);
    }

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

// Cost: copies the buffer up to the caret on every keystroke that opens the popup.
// Fine at the size OpenSCAD files actually reach, and completion is debounced, but it
// is linear in file size. If it ever matters, Scintilla already knows whether a
// position is inside a comment or a string (SCI_GETSTYLEAT, as ScadLexer2::blockStart
// uses), which would replace the scan with a backwards walk of a few characters.
QString ScadApi::textBeforeCursor() const
{
  int line, col;
  editor->qsci->getCursorPosition(&line, &col);
  const QString all = editor->toPlainText();

  int index = 0;
  for (int l = 0; l < line; ++l) {
    const int next = all.indexOf('\n', index);
    if (next < 0) return all;
    index = next + 1;
  }
  return all.left(index + col);
}

void ScadApi::autoCompleteFunctions(const QStringList& context, QStringList& list)
{
  const QString& c = context.last();
  const CaretContext caret = Feature::ExperimentalEditorEnhancements.is_enabled()
                               ? classifyCaret(textBeforeCursor())
                               : CaretContext{};
  const CompletionContext where = caret.kind;
  if (where == CompletionContext::None) return;

  // Named arguments belong to one call and are offered nowhere else.
  if (where == CompletionContext::ArgumentName && !caret.enclosingCall.isEmpty()) {
    const QStringList parameters = userParameters.contains(caret.enclosingCall)
                                     ? userParameters.value(caret.enclosingCall)
                                     : builtinParameters.value(caret.enclosingCall);
    for (const QString& parameter : parameters) {
      if (!parameter.startsWith(c, Qt::CaseInsensitive)) continue;
      const CompletionItem item{parameter, CompletionItem::Kind::NamedParameter};
      if (!list.contains(item.menuText())) list << item.menuText();
    }
  }
  // for now we only auto-complete functions and modules
  if (c.isEmpty()) {
    return;
  }

  for (const auto& item : completions) {
    const QString& name = item.label();
    const bool matches = Feature::ExperimentalEditorEnhancements.is_enabled()
                           ? name.startsWith(c, Qt::CaseInsensitive)
                           : name.startsWith(c);
    if (matches && kindFitsContext(item.kind(), where)) {
      if (!list.contains(item.menuText())) {
        list << item.menuText();
      }
    }
  }

  if (Feature::ExperimentalEditorEnhancements.is_enabled()) {
    // Match without regard to case, but keep the case that was actually typed
    // rather than rewriting it to the candidate's.
    editor->qsci->SendScintilla(QsciScintilla::SCI_AUTOCSETIGNORECASE, 1UL);
    editor->qsci->SendScintilla(
      QsciScintilla::SCI_AUTOCSETCASEINSENSITIVEBEHAVIOUR,
      static_cast<unsigned long>(QsciScintilla::SC_CASEINSENSITIVEBEHAVIOUR_RESPECTCASE));

    QList<CompletionCandidate> candidates;

    if (where == CompletionContext::ArgumentName && !caret.enclosingCall.isEmpty()) {
      const bool userDefined = userParameters.contains(caret.enclosingCall);
      const QStringList parameters = userDefined ? userParameters.value(caret.enclosingCall)
                                                 : builtinParameters.value(caret.enclosingCall);
      for (const QString& parameter : parameters) {
        candidates.append({CompletionItem(parameter, CompletionItem::Kind::NamedParameter),
                           userDefined ? CompletionSource::CurrentFile : CompletionSource::Builtin,
                           caret.suppliedArguments.contains(parameter)});
      }
    }
    for (const auto& item : completions) {
      if (!kindFitsContext(item.kind(), where)) continue;
      candidates.append({item, CompletionSource::Builtin, false});

      // Seeded call shapes accompany the bare structure, never replace it.
      for (const auto& shape : argumentShapesFor(item.label().toStdString())) {
        // A shape is a whole statement for a leaf module, exactly as its bare
        // structure is: cube() completes to cube();, so cube([1,1,1]) must too.
        // The table describes arguments; whether the call terminates is the
        // callable's kind, which is known here and not there.
        const QString text =
          QString::fromStdString(shape) + (item.kind() == CompletionItem::Kind::LeafModule ? ";" : "");
        candidates.append({CompletionItem(item.label(), CompletionItem::Kind::ArgumentShape, text),
                           CompletionSource::Builtin, false});
      }
    }
    for (const auto& item : importedCompletions) {
      if (kindFitsContext(item.kind(), where)) {
        candidates.append({item, CompletionSource::Imported, false});
      }
    }
    for (const auto& item : userCompletions) {
      if (kindFitsContext(item.kind(), where)) {
        candidates.append({item, CompletionSource::CurrentFile, false});
      }
    }

    list.clear();
    for (const auto& candidate : rankCandidates(candidates, c)) {
      const QString text = candidate.item.menuText();
      if (!list.contains(text)) list << text;
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

int ScadApi::wordStartColumn(const QString& lineText, int col)
{
  int start = col;
  while (start > 0) {
    const QChar c = lineText.at(start - 1);
    if (!c.isLetterOrNumber() && c != '_' && c != '$') break;
    --start;
  }
  return start;
}

QStringList ScadApi::completionList()
{
  int line, col;
  editor->qsci->getCursorPosition(&line, &col);
  const QString lineText = editor->qsci->text(line);
  const int start = wordStartColumn(lineText, col);

  QStringList list;
  updateAutoCompletionList(QStringList{lineText.mid(start, col - start)}, list);

  return list;
}

void ScadApi::acceptUserListSelection(const QString& text)
{
  int line, col;
  editor->qsci->getCursorPosition(&line, &col);
  const QString lineText = editor->qsci->text(line);
  const int start = wordStartColumn(lineText, col);

  editor->qsci->setSelection(line, start, line, col);
  editor->qsci->replaceSelectedText(text);

  // The caret now sits directly after the inserted entry, which is the state the
  // AcsAPIs path leaves behind too, so the same completion logic applies.
  completeSelection(text);
}

void ScadApi::completeSelection(const QString& selection)
{
  if (!Feature::ExperimentalEditorEnhancements.is_enabled()) return;

  // A named argument is accepted as "name = ", leaving the caret where the value
  // goes; the caret context there is an expression, so the next completion offers
  // values rather than more argument names.
  if (selection.endsWith('=')) {
    const QString name = selection.left(selection.size() - 1);
    int line, col;
    editor->qsci->getCursorPosition(&line, &col);
    editor->qsci->setSelection(line, col - selection.size(), line, col);
    editor->qsci->replaceSelectedText(name + " = ");
    return;
  }

  // Nearest definition first: this file shadows an imported name, which shadows a
  // builtin. Same precedence the ranking uses, so the structure inserted always
  // belongs to the candidate that was actually offered.
  // A shape arrives as its whole text; Scintilla has already inserted it, so there
  // is nothing to add. Its seeded values become editable fields instead, stepped
  // through with Tab.
  if (selection.contains('(') && (selection.endsWith(')') || selection.endsWith(");"))) {
    const int end = editor->qsci->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS);
    const int start = end - selection.toUtf8().size();

    editor->beginSnippetSession(start, selection);
    return;
  }

  for (const auto& item : userCompletions + importedCompletions + completions) {
    if (item.label() != selection) continue;

    int line, col;
    editor->qsci->getCursorPosition(&line, &col);
    const QString tail = editor->qsci->text(line).mid(col);

    int skip = 0;
    while (skip < tail.size() && (tail.at(skip) == ' ' || tail.at(skip) == '\t')) ++skip;
    const QChar next = skip < tail.size() ? tail.at(skip) : QChar();

    const auto insertion = item.insertionFor(next);
    if (!insertion.text.isEmpty()) {
      editor->insert(insertion.text);
      // QsciScintilla::insert() puts the text at the caret and leaves the caret
      // where it was, so the destination has to be computed from the position
      // before the insertion rather than read back afterwards.
      editor->qsci->setCursorPosition(line, col + insertion.text.size() - insertion.cursorBack);
      return;
    }

    // Reusing parentheses that are already there: step inside them only when they
    // are empty, so an existing argument is neither overwritten nor split.
    if (next == '(') {
      int inner = skip + 1;
      while (inner < tail.size() && (tail.at(inner) == ' ' || tail.at(inner) == '\t')) ++inner;
      if (inner < tail.size() && tail.at(inner) == ')') {
        editor->qsci->setCursorPosition(line, col + skip + 1);
      }
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
  userParameters.clear();
  importedCompletions.clear();

  // Symbols reaching this file through `use`. `include` needs nothing here: it is
  // textual, so an included file's symbols are already in sourceFile->scope above.
  const auto lookupLibrary = [](const std::string& path) -> const SourceFile * {
    return SourceFileCache::instance()->lookup(path);
  };
  if (flagAutoCompleteIncludeModules || flagAutoCompleteIncludeFunctions) {
    for (const auto& item : importedCompletionItems(*sourceFile, lookupLibrary)) {
      const bool wanted = item.kind() == CompletionItem::Kind::Function
                            ? flagAutoCompleteIncludeFunctions
                            : flagAutoCompleteIncludeModules;
      if (wanted) importedCompletions.append(item);
    }
  }
  for (const auto& used : sourceFile->usedlibs) {
    const SourceFile *library = lookupLibrary(used);
    if (!library) continue;
    for (const auto& item : importedCompletions) {
      if (userParameters.contains(item.label())) continue;  // the current file wins
      const QStringList parameters = parameterNamesOf(*library, item.label());
      if (!parameters.isEmpty()) userParameters.insert(item.label(), parameters);
    }
  }

  for (const auto& item : userCompletionItems(*sourceFile)) {
    const bool wanted =
      item.kind() == CompletionItem::Kind::Variable
        ? flagAutoCompleteIncludeVariables
        : (item.kind() == CompletionItem::Kind::Function ? flagAutoCompleteIncludeFunctions
                                                         : flagAutoCompleteIncludeModules);
    if (wanted) userCompletions.append(item);

    if (item.kind() != CompletionItem::Kind::Variable) {
      const QStringList parameters = parameterNamesOf(*sourceFile, item.label());
      if (!parameters.isEmpty()) userParameters.insert(item.label(), parameters);
    }
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
