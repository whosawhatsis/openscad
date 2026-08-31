#include "gui/SnippetFields.h"

namespace {

bool isDelimiter(QChar c)
{
  return c == '(' || c == '[' || c == ',' || c == '=';
}
bool isNumberChar(QChar c)
{
  return c.isDigit() || c == '.' || c == '-' || c == '+';
}

}  // namespace

QList<SnippetField> shapeFieldRanges(const QString& shape)
{
  QList<SnippetField> fields;
  QChar previous;  // last non-space character seen

  for (int i = 0; i < shape.size();) {
    const QChar c = shape.at(i);

    if (c.isSpace()) {
      ++i;
      continue;
    }

    if (!isDelimiter(previous)) {
      previous = c;
      ++i;
      continue;
    }

    const int start = i;
    if (c == '"') {
      ++i;
      while (i < shape.size() && shape.at(i) != '"') {
        if (shape.at(i) == '\\') ++i;
        ++i;
      }
      if (i < shape.size()) ++i;  // closing quote
      fields.append({start, i - start});
    } else if (isNumberChar(c)) {
      while (i < shape.size() && isNumberChar(shape.at(i))) ++i;
      fields.append({start, i - start});
    } else if (shape.mid(i, 4) == "true") {
      i += 4;
      fields.append({start, 4});
    } else if (shape.mid(i, 5) == "false") {
      i += 5;
      fields.append({start, 5});
    } else {
      // A name, not a value: skip it whole so its digits cannot be picked up.
      while (i < shape.size() &&
             (shape.at(i).isLetterOrNumber() || shape.at(i) == '_' || shape.at(i) == '$')) {
        ++i;
      }
      if (i == start) ++i;  // never stall
    }

    previous = shape.at(i - 1);
  }

  return fields;
}
