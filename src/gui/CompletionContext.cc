#include "gui/CompletionContext.h"

namespace {

bool isIdentifierChar(QChar c)
{
  return c.isLetterOrNumber() || c == '_' || c == '$';
}

}  // namespace

CompletionContext classifyCompletionContext(const QString& before)
{
  // Drop the partial word under the caret: "x = si" classifies exactly as "x = ".
  int end = before.size();
  while (end > 0 && isIdentifierChar(before.at(end - 1))) --end;

  // Forward scan. Comments and strings cannot be recognised by looking backwards -
  // a '"' or a '/' means nothing without knowing what came before it.
  QChar last;  // last significant character, null while none has been seen
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

    last = c;
  }

  // An unterminated comment or string swallows the caret.
  if (inLineComment || inBlockComment || inString) return CompletionContext::None;

  if (last.isNull()) return CompletionContext::Statement;

  // A name may be introduced wherever a statement may start, and as the child of a
  // transform - which is what a ')' at this position introduces.
  if (last == ';' || last == '{' || last == '}' || last == ')') return CompletionContext::Statement;

  // Right after an opening parenthesis or a separator, a named argument is as valid
  // as a value, so both sets of candidates apply.
  if (last == '(' || last == ',') return CompletionContext::ArgumentName;

  // Anything that can only be followed by a value.
  if (QString("=+-*/%<>!&|?:[").contains(last)) return CompletionContext::Expression;

  return CompletionContext::Unknown;
}
