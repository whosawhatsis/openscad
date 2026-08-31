#pragma once

#include <QString>
#include <utility>

class CompletionItem
{
public:
  enum class Kind {
    Function,
    LeafModule,
    ChildModule,
    Variable,
    NamedParameter,
    Keyword,
    /// A complete seeded call such as "translate([0, 0, 0])", offered next to the
    /// bare structure. The label stays the callable's name so matching and ranking
    /// behave normally; the shape is what the menu shows and what gets inserted.
    ArgumentShape,
  };

  CompletionItem(QString label, Kind kind) : label_(std::move(label)), kind_(kind) {}
  CompletionItem(QString label, Kind kind, QString shape)
    : label_(std::move(label)), kind_(kind), shape_(std::move(shape))
  {
  }

  const QString& shape() const { return shape_; }

  const QString& label() const { return label_; }
  Kind kind() const { return kind_; }

  QString insertionText() const
  {
    switch (kind_) {
    case Kind::Function:
    case Kind::ChildModule:    return label_ + "()";
    case Kind::LeafModule:     return label_ + "();";
    case Kind::ArgumentShape:  return shape_;
    case Kind::Variable:
    case Kind::Keyword:        return label_;
    case Kind::NamedParameter: return label_ + " = ";
    }
    return label_;
  }

  QString insertionSuffix() const { return insertionText().mid(label_.size()); }

  /**
   * @brief How the entry reads in the popup.
   *
   * A named parameter shows its '=' so that "size=" stays distinct from an
   * in-scope variable or function "size", which may legally be passed
   * positionally in the same place. Both meanings stay available.
   */
  QString menuText() const
  {
    if (kind_ == Kind::ArgumentShape) return shape_;
    return kind_ == Kind::NamedParameter ? label_ + "=" : label_;
  }

  /**
   * @brief What still has to be typed, given what already follows the caret.
   *
   * Completion runs over existing text as often as it runs at the end of a line.
   * Only the missing punctuation is inserted: an existing parenthesis or
   * semicolon is reused, and arguments already written are never touched.
   *
   * @param next First non-whitespace character after the caret, or a null
   *             QChar at end of line.
   */
  struct Insertion {
    QString text;
    int cursorBack;
  };

  Insertion insertionFor(QChar next) const
  {
    switch (kind_) {
    case Kind::Function:
    case Kind::ChildModule:
      // A call already opened is left exactly as it is; its arguments are not ours.
      return next == '(' ? Insertion{{}, 0} : Insertion{"()", 1};
    case Kind::LeafModule:
      if (next == '(') return {{}, 0};
      // A statement that already terminates does not need a second semicolon.
      return next == ';' ? Insertion{"()", 1} : Insertion{"();", 2};
    case Kind::NamedParameter: return next == '=' ? Insertion{{}, 0} : Insertion{" = ", 0};
    // Scintilla has already inserted the whole shape; nothing may be added to it.
    case Kind::ArgumentShape:
    case Kind::Variable:
    case Kind::Keyword:        break;
    }
    return {{}, 0};
  }

  int cursorBack() const
  {
    switch (kind_) {
    case Kind::Function:
    case Kind::ChildModule:    return 1;
    case Kind::LeafModule:     return 2;
    case Kind::Variable:
    case Kind::ArgumentShape:
    case Kind::Keyword:
    case Kind::NamedParameter: return 0;
    }
    return 0;
  }

private:
  QString label_;
  Kind kind_;
  QString shape_;
};
