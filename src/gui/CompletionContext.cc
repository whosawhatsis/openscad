#include "gui/CompletionContext.h"

#include <QList>
#include <QPair>

namespace {

bool isIdentifierChar(QChar c)
{
  return c.isLetterOrNumber() || c == '_' || c == '$';
}

}  // namespace

CaretContext classifyCaret(const QString& before)
{
  // Drop the partial word under the caret: "x = si" classifies exactly as "x = ".
  int end = before.size();
  while (end > 0 && isIdentifierChar(before.at(end - 1))) --end;

  // Forward scan. Comments and strings cannot be recognised by looking backwards -
  // a '"' or a '/' means nothing without knowing what came before it.
  QChar last;  // last significant character, null while none has been seen
  // Open brackets, and for '(' the identifier that preceded it - empty when the
  // parenthesis was grouping rather than calling.
  struct Bracket {
    QChar opener;
    QString callee;
    QStringList supplied;
  };
  QList<Bracket> open;
  QString pendingWord;  // identifier most recently completed, candidate callee
  bool inLineComment = false;
  bool inBlockComment = false;
  bool inString = false;

  for (int i = 0; i < end; ++i) {
    const QChar c = before.at(i);
    const QChar next = (i + 1 < end) ? before.at(i + 1) : QChar();

    if (inLineComment) {
      if (c == '\n') inLineComment = false;
      continue;
    }
    if (inBlockComment) {
      if (c == '*' && next == '/') {
        inBlockComment = false;
        ++i;
      }
      continue;
    }
    if (inString) {
      if (c == '\\') ++i;  // skip the escaped character
      else if (c == '"') inString = false;
      continue;
    }

    if (c == '/' && next == '/') {
      inLineComment = true;
      ++i;
      continue;
    }
    if (c == '/' && next == '*') {
      inBlockComment = true;
      ++i;
      continue;
    }
    if (c == '"') {
      inString = true;
      continue;
    }
    if (c.isSpace()) continue;

    // "use <path>" and "include <path>" are statements, not comparisons. Without
    // this the trailing '>' reads as greater-than and everything after an import
    // is classified as expression position, hiding every module from completion.
    if (c == '<' && (pendingWord == "use" || pendingWord == "include")) {
      while (i < end && before.at(i) != '>') ++i;
      last = ';';
      pendingWord.clear();
      continue;
    }

    if (isIdentifierChar(c)) {
      // Build up the identifier so a following '(' knows what is being called.
      if (!isIdentifierChar(last) || last.isNull()) pendingWord.clear();
      pendingWord += c;
    } else {
      if (c == '(' || c == '[' || c == '{') {
        open.append({c, c == '(' ? pendingWord : QString(), {}});
      } else if (c == ')' || c == ']' || c == '}') {
        if (!open.isEmpty()) open.removeLast();
      } else if (c == '=' && next != '=' && isIdentifierChar(last) && !pendingWord.isEmpty() &&
                 !open.isEmpty() && open.last().opener == '(') {
        // "name =" supplies that argument. Requiring the previous significant
        // character to be part of an identifier rules out ==, !=, <= and >=.
        open.last().supplied << pendingWord;
      }
      pendingWord.clear();
    }

    last = c;
  }

  CaretContext result;

  // An unterminated comment or string swallows the caret.
  if (inLineComment || inBlockComment || inString) {
    result.kind = CompletionContext::None;
    return result;
  }

  // The innermost still-open '(' that followed a name is the call being written.
  for (int i = open.size() - 1; i >= 0; --i) {
    if (open.at(i).opener == '(') {
      result.enclosingCall = open.at(i).callee;
      result.suppliedArguments = open.at(i).supplied;
      break;
    }
  }

  if (last.isNull()) {
    result.kind = CompletionContext::Statement;
    return result;
  }

  // A name may be introduced wherever a statement may start, and as the child of a
  // transform - which is what a ')' at this position introduces.
  if (last == ';' || last == '{' || last == '}' || last == ')') {
    result.kind = CompletionContext::Statement;
    return result;
  }

  // Right after an opening parenthesis or a separator, a named argument is as valid
  // as a value, so both sets of candidates apply.
  if (last == '(' || last == ',') {
    result.kind = CompletionContext::ArgumentName;
    return result;
  }

  // Anything that can only be followed by a value.
  if (QString("=+-*/%<>!&|?:[").contains(last)) {
    result.kind = CompletionContext::Expression;
    return result;
  }

  result.kind = CompletionContext::Unknown;
  return result;
}
