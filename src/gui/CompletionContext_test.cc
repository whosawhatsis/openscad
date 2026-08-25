#include <catch2/catch_test_macros.hpp>

#include <QString>

#include "gui/CompletionContext.h"

namespace {
CompletionContext ctx(const char *before)
{
  return classifyCaret(QString(before)).kind;
}
QString call(const char *before)
{
  return classifyCaret(QString(before)).enclosingCall;
}
}  // namespace

TEST_CASE("caret context is classified from the text before it", "[completion]")
{
  // Statement position: a module may be instantiated here, an expression may not.
  CHECK(ctx("") == CompletionContext::Statement);
  CHECK(ctx("   ") == CompletionContext::Statement);
  CHECK(ctx("cube();\n") == CompletionContext::Statement);
  CHECK(ctx("module foo() {\n  ") == CompletionContext::Statement);
  CHECK(ctx("if (x) {\n") == CompletionContext::Statement);
  CHECK(ctx("}\n") == CompletionContext::Statement);

  // A closing parenthesis at statement level introduces the child of a transform.
  CHECK(ctx("translate([1, 2, 3]) ") == CompletionContext::Statement);

  // Expression position: functions and variables, never modules.
  CHECK(ctx("x = ") == CompletionContext::Expression);
  CHECK(ctx("x = 1 + ") == CompletionContext::Expression);
  CHECK(ctx("x = -") == CompletionContext::Expression);
  CHECK(ctx("x = [1, 2][") == CompletionContext::Expression);
  CHECK(ctx("x = a > ") == CompletionContext::Expression);
  CHECK(ctx("x = c ? ") == CompletionContext::Expression);

  // Directly after an opening parenthesis or a comma a named argument may be given,
  // and so may an ordinary value.
  CHECK(ctx("cube(") == CompletionContext::ArgumentName);
  CHECK(ctx("cube(1, ") == CompletionContext::ArgumentName);
  // Once a name has been given the value itself is an ordinary expression.
  CHECK(ctx("cube(size = ") == CompletionContext::Expression);

  // Comments and strings take no completion at all.
  CHECK(ctx("// talking about cu") == CompletionContext::None);
  CHECK(ctx("x = \"some cu") == CompletionContext::None);
  CHECK(ctx("/* cu") == CompletionContext::None);

  // A finished comment does not swallow what follows it.
  CHECK(ctx("/* note */ ") == CompletionContext::Statement);
  CHECK(ctx("// note\n") == CompletionContext::Statement);
  CHECK(ctx("x = \"done\" + ") == CompletionContext::Expression);

  // The partial word being typed is not itself context.
  CHECK(ctx("cub") == CompletionContext::Statement);
  CHECK(ctx("x = si") == CompletionContext::Expression);
  CHECK(ctx("$f") == CompletionContext::Statement);
}

TEST_CASE("the enclosing call is identified for named arguments", "[completion]")
{
  // Inside a call's argument list, the callable being called is known.
  CHECK(call("cube(") == "cube");
  CHECK(call("cube(1, ") == "cube");
  CHECK(call("cube(size = 1, ") == "cube");
  CHECK(call("translate([1,2,3]) cylinder(") == "cylinder");

  // Nesting reports the innermost call, which is the one an argument belongs to.
  CHECK(call("cube(size = max(") == "max");
  CHECK(call("cube(size = max(1, 2), ") == "cube");

  // A bracket is not a call, and neither is a bare parenthesised expression.
  CHECK(call("x = [") == QString());
  CHECK(call("x = (") == QString());

  // Outside any argument list there is no enclosing call.
  CHECK(call("cube(1);\n") == QString());
  CHECK(call("") == QString());

  // Whitespace between the name and its parenthesis is still a call.
  CHECK(call("cube (") == "cube");
}

TEST_CASE("arguments already given at the call site are known", "[completion]")
{
  const auto supplied = [](const char *before) {
    return classifyCaret(QString(before)).suppliedArguments;
  };

  CHECK(supplied("cube(") == QStringList{});
  CHECK(supplied("cube(size = 1, ") == QStringList{"size"});
  CHECK(supplied("cylinder(h = 1, r = 2, ") == QStringList{"h", "r"});

  // Only the innermost call's own arguments count.
  CHECK(supplied("cube(size = max(a = 1, ") == QStringList{"a"});
  CHECK(supplied("cube(size = max(1, 2), ") == QStringList{"size"});

  // Comparisons are not assignments.
  CHECK(supplied("f(x == 1, ") == QStringList{});
  CHECK(supplied("f(x >= 1, ") == QStringList{});
  CHECK(supplied("f(x != 1, ") == QStringList{});

  // Positional arguments supply no names.
  CHECK(supplied("cube(1, ") == QStringList{});
}
