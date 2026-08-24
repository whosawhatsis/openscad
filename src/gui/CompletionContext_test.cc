#include <catch2/catch_test_macros.hpp>

#include <QString>

#include "gui/CompletionContext.h"

namespace {
CompletionContext ctx(const char *before)
{
  return classifyCompletionContext(QString(before));
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
