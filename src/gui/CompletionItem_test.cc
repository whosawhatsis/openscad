#include <catch2/catch_test_macros.hpp>

#include "core/Arguments.h"
#include "core/Builtins.h"
#include "core/Children.h"
#include "core/module.h"
#include "gui/CompletionItem.h"

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
