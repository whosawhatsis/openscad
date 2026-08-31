#include "gui/UserSymbols.h"

#include <sstream>
#include <string>

#include "core/AST.h"
#include "core/Assignment.h"
#include "core/Expression.h"
#include "core/LocalScope.h"
#include "core/ModuleInstantiation.h"
#include "core/SourceFile.h"
#include "core/UserModule.h"
#include "core/function.h"

namespace {

// Text of an expression with string literals blanked out, so a literal such as
// "$children" cannot be mistaken for a reference to the variable. Comments are
// already gone - this is the AST's own serialization, not the user's source.
//
// A structural walk over Expression would be exact, but Expression has fifteen
// subclasses and no visitor, so that is ~150 lines of mechanical dynamic_cast
// for a case this handles. If an expression visitor is ever added, use it here.
std::string expressionText(const std::shared_ptr<Expression>& expr)
{
  if (!expr) return {};
  std::ostringstream stream;
  stream << *expr;
  std::string text = stream.str();

  std::string stripped;
  stripped.reserve(text.size());
  bool inString = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (inString && c == '\\') {
      ++i;  // skip the escaped character
      continue;
    }
    if (c == '"') {
      inString = !inString;
      continue;
    }
    if (!inString) stripped += c;
  }
  return stripped;
}

bool argumentsReferenceChildrenCount(const AssignmentList& arguments)
{
  for (const auto& argument : arguments) {
    if (expressionText(argument->getExpr()).find("$children") != std::string::npos) return true;
  }
  return false;
}

bool scopeUsesChildren(const LocalScope& scope)
{
  for (const auto& assignment : scope.assignments) {
    if (expressionText(assignment->getExpr()).find("$children") != std::string::npos) return true;
  }

  for (const auto& instantiation : scope.moduleInstantiations) {
    if (!instantiation) continue;

    const std::string& name = instantiation->name();
    if (name == "children" || name == "child") return true;

    if (argumentsReferenceChildrenCount(instantiation->arguments)) return true;

    if (instantiation->scope && scopeUsesChildren(*instantiation->scope)) return true;

    const auto *ifElse = dynamic_cast<const IfElseModuleInstantiation *>(instantiation.get());
    if (ifElse) {
      const auto elseScope = ifElse->getElseScope();
      if (elseScope && scopeUsesChildren(*elseScope)) return true;
    }
  }

  // Deliberately does not descend into scope.getUserModules(): a nested
  // declaration is its own module's body, not this one's.
  return false;
}

}  // namespace

bool moduleAcceptsChildren(const UserModule& module)
{
  return module.body && scopeUsesChildren(*module.body);
}

QList<CompletionItem> userCompletionItems(const SourceFile& file)
{
  QList<CompletionItem> items;
  if (!file.scope) return items;

  for (const auto& [name, module] : file.scope->getUserModules()) {
    if (!module) continue;
    items.append(CompletionItem(QString::fromStdString(name), moduleAcceptsChildren(*module)
                                                                ? CompletionItem::Kind::ChildModule
                                                                : CompletionItem::Kind::LeafModule));
  }

  for (const auto& [name, function] : file.scope->getUserFunctions()) {
    items.append(CompletionItem(QString::fromStdString(name), CompletionItem::Kind::Function));
  }

  for (const auto& assignment : file.scope->assignments) {
    if (!assignment) continue;
    items.append(
      CompletionItem(QString::fromStdString(assignment->getName()), CompletionItem::Kind::Variable));
  }

  return items;
}

QStringList parameterNamesFromCalltips(const QStringList& calltips)
{
  QStringList names;

  for (const QString& calltip : calltips) {
    const int open = calltip.indexOf('(');
    const int close = calltip.lastIndexOf(')');
    if (open < 0 || close < open) continue;

    const QString inside = calltip.mid(open + 1, close - open - 1);

    // Split on commas that are not nested inside a bracket or parenthesis.
    QStringList parts;
    QString current;
    int depth = 0;
    for (const QChar c : inside) {
      if (c == '[' || c == '(') ++depth;
      else if (c == ']' || c == ')') --depth;

      if (c == ',' && depth == 0) {
        parts << current;
        current.clear();
      } else {
        current += c;
      }
    }
    parts << current;

    for (QString part : parts) {
      part = part.trimmed();
      if (part.isEmpty()) continue;

      // "[width, depth, height]" documents what goes in a vector, not a name.
      if (part.startsWith('[')) continue;

      const int equals = part.indexOf('=');
      const QString name = (equals >= 0 ? part.left(equals) : part).trimmed();
      if (name.isEmpty()) continue;

      // Anything that is not a plain identifier is prose, not a parameter.
      bool identifier = true;
      for (const QChar c : name) {
        if (!c.isLetterOrNumber() && c != '_' && c != '$') identifier = false;
      }
      if (!identifier) continue;

      if (!names.contains(name)) names << name;
    }
  }

  return names;
}

QStringList parameterNamesOf(const SourceFile& file, const QString& callable)
{
  QStringList names;
  if (!file.scope) return names;

  const std::string key = callable.toStdString();

  const auto& modules = file.scope->getUserModules();
  const auto module = modules.find(key);
  if (module != modules.end() && module->second) {
    for (const auto& parameter : module->second->parameters) {
      if (parameter) names << QString::fromStdString(parameter->getName());
    }
    return names;
  }

  const auto& functions = file.scope->getUserFunctions();
  const auto function = functions.find(key);
  if (function != functions.end() && function->second) {
    for (const auto& parameter : function->second->parameters) {
      if (parameter) names << QString::fromStdString(parameter->getName());
    }
  }

  return names;
}

QList<CompletionItem> importedCompletionItems(
  const SourceFile& file, const std::function<const SourceFile *(const std::string&)>& lookup)
{
  QList<CompletionItem> items;

  for (const auto& used : file.usedlibs) {
    const SourceFile *library = lookup(used);
    if (!library || !library->scope) continue;

    for (const auto& [name, module] : library->scope->getUserModules()) {
      if (!module) continue;
      items.append(CompletionItem(QString::fromStdString(name), moduleAcceptsChildren(*module)
                                                                  ? CompletionItem::Kind::ChildModule
                                                                  : CompletionItem::Kind::LeafModule));
    }
    for (const auto& [name, function] : library->scope->getUserFunctions()) {
      items.append(CompletionItem(QString::fromStdString(name), CompletionItem::Kind::Function));
    }
    // Deliberately not library->scope->assignments: a variable does not cross a
    // `use`. And deliberately not library->usedlibs: `use` is not transitive.
  }

  return items;
}
