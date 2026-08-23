#pragma once

#include <QString>
#include <utility>

class CompletionItem
{
public:
  enum class Kind { Function, LeafModule, ChildModule, Variable, NamedParameter };

  CompletionItem(QString label, Kind kind) : label_(std::move(label)), kind_(kind) {}

  const QString& label() const { return label_; }
  Kind kind() const { return kind_; }

  QString insertionText() const
  {
    switch (kind_) {
    case Kind::Function:
    case Kind::ChildModule:    return label_ + "()";
    case Kind::LeafModule:     return label_ + "();";
    case Kind::Variable:       return label_;
    case Kind::NamedParameter: return label_ + " = ";
    }
    return label_;
  }

  QString insertionSuffix() const { return insertionText().mid(label_.size()); }

  int cursorBack() const
  {
    switch (kind_) {
    case Kind::Function:
    case Kind::ChildModule:    return 1;
    case Kind::LeafModule:     return 2;
    case Kind::Variable:
    case Kind::NamedParameter: return 0;
    }
    return 0;
  }

private:
  QString label_;
  Kind kind_;
};
