#include <catch2/catch_test_macros.hpp>

#include "core/Arguments.h"
#include "core/Builtins.h"
#include "core/Children.h"
#include "core/module.h"
#include "core/SourceFile.h"
#include "core/UserModule.h"
#include "gui/CompletionItem.h"
#include "gui/UserSymbols.h"
#include "openscad.h"

#include <string>
#include <vector>

TEST_CASE("completion items generate callable structure", "[completion]")
{
  using Kind = CompletionItem::Kind;

  CHECK(CompletionItem{"sin", Kind::Function}.insertionText() == "sin()");
  CHECK(CompletionItem{"cube", Kind::LeafModule}.insertionText() == "cube();");
  CHECK(CompletionItem{"translate", Kind::ChildModule}.insertionText() == "translate()");
  CHECK(CompletionItem{"size", Kind::Variable}.insertionText() == "size");
  CHECK(CompletionItem{"center", Kind::NamedParameter}.insertionText() == "center = ");
  CHECK(CompletionItem{"sin", Kind::Function}.cursorBack() == 1);
  CHECK(CompletionItem{"cube", Kind::LeafModule}.cursorBack() == 2);
  CHECK(CompletionItem{"else", Kind::Keyword}.insertionText() == "else");
  CHECK(CompletionItem{"size", Kind::Variable}.cursorBack() == 0);
  CHECK(CompletionItem{"else", Kind::Keyword}.cursorBack() == 0);
}

TEST_CASE("builtin callback shape determines module kind", "[completion]")
{
  auto leaf = [](const ModuleInstantiation *, Arguments) -> std::shared_ptr<AbstractNode> {
    return nullptr;
  };
  auto child = [](const ModuleInstantiation *, Arguments,
                  const Children&) -> std::shared_ptr<AbstractNode> { return nullptr; };

  CHECK(BuiltinModule(leaf).kind() == BuiltinModule::Kind::Leaf);
  CHECK(BuiltinModule(child).kind() == BuiltinModule::Kind::Child);
}

TEST_CASE("statement builtins complete as leaf modules", "[completion]")
{
  Builtins::initialize();
  const auto& modules = Builtins::instance().getModules();

  const auto kindOf = [&modules](const char *name) {
    const auto it = modules.find(name);
    REQUIRE(it != modules.end());
    const auto *builtin = dynamic_cast<const BuiltinModule *>(it->second);
    REQUIRE(builtin);
    return builtin->kind();
  };

  // These take a Children argument but are overwhelmingly used as standalone statements,
  // so completion must terminate them with a semicolon.
  CHECK(kindOf("echo") == BuiltinModule::Kind::Leaf);
  CHECK(kindOf("assert") == BuiltinModule::Kind::Leaf);
  CHECK(kindOf("children") == BuiltinModule::Kind::Leaf);

  // Control flow and transforms exist to wrap children and must not be terminated.
  CHECK(kindOf("if") == BuiltinModule::Kind::Child);
  CHECK(kindOf("for") == BuiltinModule::Kind::Child);
  CHECK(kindOf("let") == BuiltinModule::Kind::Child);
  CHECK(kindOf("translate") == BuiltinModule::Kind::Child);
  CHECK(kindOf("union") == BuiltinModule::Kind::Child);
}

// ---------------------------------------------------------------------------
// User-defined symbol completion
// ---------------------------------------------------------------------------

namespace {

const SourceFile& parseSnippet(const std::string& text)
{
  Builtins::initialize();
  static std::vector<SourceFile *> keepAlive;
  SourceFile *file = nullptr;
  REQUIRE(parse(file, text, "<test>", "<test>", 0));
  REQUIRE(file != nullptr);
  keepAlive.push_back(file);
  return *file;
}

CompletionItem::Kind kindOfSymbol(const std::string& source, const QString& name)
{
  for (const auto& item : userCompletionItems(parseSnippet(source))) {
    if (item.label() == name) return item.kind();
  }
  FAIL("no completion item named " << name.toStdString());
  return CompletionItem::Kind::Variable;
}

}  // namespace

TEST_CASE("user modules are classified leaf or child-capable", "[completion]")
{
  using Kind = CompletionItem::Kind;

  // A module that never uses its children terminates with a semicolon.
  CHECK(kindOfSymbol("module leaf() { cube(); }", "leaf") == Kind::LeafModule);

  // A direct children() invocation makes it child-capable.
  CHECK(kindOfSymbol("module wrap() { children(); }", "wrap") == Kind::ChildModule);

  // Legacy singular child() counts too.
  CHECK(kindOfSymbol("module old() { child(0); }", "old") == Kind::ChildModule);

  // Conditional and nested-scope uses count - the module can still take children.
  CHECK(kindOfSymbol("module cond() { if (true) children(); }", "cond") == Kind::ChildModule);
  CHECK(kindOfSymbol("module deep() { translate([1,0,0]) union() { children(); } }", "deep") ==
        Kind::ChildModule);
  CHECK(kindOfSymbol("module els() { if (false) cube(); else children(); }", "els") ==
        Kind::ChildModule);

  // A body-level reference to $children counts even without invoking children().
  CHECK(kindOfSymbol("module counted() { echo($children); }", "counted") == Kind::ChildModule);

  // A nested module declaration is not the outer module's body.
  CHECK(kindOfSymbol("module outer() { module inner() { children(); } cube(); }", "outer") ==
        Kind::LeafModule);

  // A string literal that happens to contain $children is not a reference to it.
  CHECK(kindOfSymbol("module quoted() { echo(\"$children\"); }", "quoted") == Kind::LeafModule);

  // Transitive use through a helper does not count.
  CHECK(kindOfSymbol("module helper() { children(); } module caller() { helper(); }", "caller") ==
        Kind::LeafModule);
}

TEST_CASE("user functions and variables carry their own kinds", "[completion]")
{
  using Kind = CompletionItem::Kind;

  CHECK(kindOfSymbol("function f(x) = x * 2;", "f") == Kind::Function);
  CHECK(kindOfSymbol("size = 12;", "size") == Kind::Variable);
}
