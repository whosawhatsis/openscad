#include <catch2/catch_test_macros.hpp>

#include "core/Arguments.h"
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
  CHECK(CompletionItem{"size", Kind::Variable}.cursorBack() == 0);
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
