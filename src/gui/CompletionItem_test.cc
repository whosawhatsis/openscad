#include <catch2/catch_test_macros.hpp>

#include "core/Arguments.h"
#include "core/ArgumentShapes.h"
#include "core/Builtins.h"
#include "core/Children.h"
#include "core/module.h"
#include "core/SourceFile.h"
#include "core/UserModule.h"
#include "gui/CompletionItem.h"
#include "gui/UserSymbols.h"

#include <QMap>

#include <algorithm>
#include <functional>
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

TEST_CASE("completion inserts only the punctuation that is missing", "[completion]")
{
  using Kind = CompletionItem::Kind;
  const QChar none;

  const CompletionItem leaf{"cube", Kind::LeafModule};
  const CompletionItem child{"translate", Kind::ChildModule};
  const CompletionItem func{"sin", Kind::Function};
  const CompletionItem param{"center", Kind::NamedParameter};
  const CompletionItem var{"size", Kind::Variable};

  // Nothing follows: the full structure is written.
  CHECK(leaf.insertionFor(none).text == "();");
  CHECK(leaf.insertionFor(none).cursorBack == 2);
  CHECK(child.insertionFor(none).text == "()");
  CHECK(child.insertionFor(none).cursorBack == 1);
  CHECK(func.insertionFor(none).text == "()");

  // An open parenthesis already there is reused, whatever it contains.
  CHECK(leaf.insertionFor('(').text.isEmpty());
  CHECK(leaf.insertionFor('(').cursorBack == 0);
  CHECK(child.insertionFor('(').text.isEmpty());
  CHECK(func.insertionFor('(').text.isEmpty());

  // An existing semicolon is reused: parentheses still needed, terminator not.
  CHECK(leaf.insertionFor(';').text == "()");
  CHECK(leaf.insertionFor(';').cursorBack == 1);
  // Only leaf modules terminate, so a following semicolon changes nothing for the others.
  CHECK(child.insertionFor(';').text == "()");

  // Named parameters reuse an existing '='.
  CHECK(param.insertionFor(none).text == " = ");
  CHECK(param.insertionFor('=').text.isEmpty());

  // Plain identifiers never add punctuation.
  CHECK(var.insertionFor(none).text.isEmpty());
  CHECK(var.insertionFor('(').text.isEmpty());
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

TEST_CASE("parameter names are read from calltips conservatively", "[completion]")
{
  // A bare identifier at argument level is a parameter name.
  CHECK(parameterNamesFromCalltips({"cube(size)"}) == QStringList{"size"});
  CHECK(parameterNamesFromCalltips({"cylinder(h, r1, r2)"}) == QStringList{"h", "r1", "r2"});

  // "name = value" documents a parameter; the name is the half before the '='.
  CHECK(parameterNamesFromCalltips({"cylinder(h = height, r = radius, center = true)"}) ==
        QStringList{"h", "r", "center"});

  // A bracketed argument describes the contents of a vector, not parameter names.
  // Harvesting width/depth/height from it would offer arguments that do not exist.
  CHECK(parameterNamesFromCalltips({"cube([width, depth, height])"}).isEmpty());
  CHECK(parameterNamesFromCalltips({"translate([x, y, z])"}).isEmpty());
  CHECK(parameterNamesFromCalltips({"cube([width, depth, height], center = true)"}) ==
        QStringList{"center"});

  // Overloads contribute their union, in first-seen order and without duplicates.
  CHECK(parameterNamesFromCalltips({"cylinder(h, r1, r2)", "cylinder(h = height, r = radius)",
                                    "cylinder(h = height, d = diameter, center = true)"}) ==
        QStringList{"h", "r1", "r2", "r", "d", "center"});

  // Nothing sensible to take.
  CHECK(parameterNamesFromCalltips({"children()"}).isEmpty());
  CHECK(parameterNamesFromCalltips({}).isEmpty());
  CHECK(parameterNamesFromCalltips({"malformed("}).isEmpty());
}

TEST_CASE("user callables expose their own parameter names", "[completion]")
{
  const SourceFile& file = parseSnippet(
    "module widget(size, center = false) { cube(); }\n"
    "function scale2(v, factor = 2) = v * factor;");
  CHECK(parameterNamesOf(file, "widget") == QStringList{"size", "center"});
  CHECK(parameterNamesOf(file, "scale2") == QStringList{"v", "factor"});
  CHECK(parameterNamesOf(file, "nosuch").isEmpty());
}

TEST_CASE("use imports callables but never variables", "[completion]")
{
  // `use <lib>` makes a library's modules and functions available, and deliberately
  // not its variables - mirroring FileContext::lookup_local_module/function, which
  // only ever consults a used file's modules and functions.
  const SourceFile& library = parseSnippet(
    "module wrapper() { children(); }\n"
    "module widget() { cube(); }\n"
    "function twice(x) = x * 2;\n"
    "library_setting = 3;\n");

  SourceFile main("/tmp", "main.scad");
  main.usedlibs.push_back("lib.scad");

  const auto lookup = [&library](const std::string& path) -> const SourceFile * {
    return path == "lib.scad" ? &library : nullptr;
  };

  QMap<QString, CompletionItem::Kind> kinds;
  for (const auto& item : importedCompletionItems(main, lookup)) kinds.insert(item.label(), item.kind());

  CHECK(kinds.value("wrapper") == CompletionItem::Kind::ChildModule);
  CHECK(kinds.value("widget") == CompletionItem::Kind::LeafModule);
  CHECK(kinds.value("twice") == CompletionItem::Kind::Function);
  CHECK_FALSE(kinds.contains("library_setting"));
}

TEST_CASE("a used library that failed to parse yields nothing", "[completion]")
{
  // SourceFileCache::lookup returns nullptr when the library was missing or did not
  // compile. Completion must degrade to offering less, never crash.
  SourceFile main("/tmp", "main.scad");
  main.usedlibs.push_back("missing.scad");

  const auto lookup = [](const std::string&) -> const SourceFile * { return nullptr; };
  CHECK(importedCompletionItems(main, lookup).isEmpty());
}

TEST_CASE("use is not transitive", "[completion]")
{
  // a uses b, b uses c. OpenSCAD resolves names against a's own usedlibs only, so
  // c's modules are not visible in a - see FileContext::lookup_local_module.
  const SourceFile& c = parseSnippet("module from_c() { cube(); }");

  SourceFile b("/tmp", "b.scad");
  b.usedlibs.push_back("c.scad");

  SourceFile a("/tmp", "a.scad");
  a.usedlibs.push_back("b.scad");

  const auto lookup = [&b, &c](const std::string& path) -> const SourceFile * {
    if (path == "b.scad") return &b;
    if (path == "c.scad") return &c;
    return nullptr;
  };

  QStringList names;
  for (const auto& item : importedCompletionItems(a, lookup)) names << item.label();
  CHECK_FALSE(names.contains("from_c"));
}

TEST_CASE("argument shapes are complete calls that need no decoration", "[completion]")
{
  using Kind = CompletionItem::Kind;
  const CompletionItem shape{"translate", Kind::ArgumentShape, "translate([0, 0, 0])"};

  // The menu shows the shape; matching still works off the callable's name.
  CHECK(shape.label() == "translate");
  CHECK(shape.menuText() == "translate([0, 0, 0])");

  // Nothing is appended: the inserted text is already a complete call.
  CHECK(shape.insertionFor(QChar()).text.isEmpty());
  CHECK(shape.insertionFor('(').text.isEmpty());
  CHECK(shape.insertionFor(';').text.isEmpty());
  CHECK(shape.insertionFor(QChar()).cursorBack == 0);
}

TEST_CASE("curated shapes are seeded with neutral values", "[completion]")
{
  // Transforms are identities, so accepting a shape changes nothing until edited.
  CHECK(argumentShapesFor("translate") == std::vector<std::string>{"translate([0, 0, 0])"});
  CHECK(argumentShapesFor("scale") == std::vector<std::string>{"scale([1, 1, 1])"});

  // Primitives start at unit size.
  CHECK(argumentShapesFor("cube").front() == "cube([1, 1, 1])");
  CHECK(argumentShapesFor("sphere").front() == "sphere(r = 1)");

  // Ambiguous radius/diameter variants are disambiguated by naming the argument.
  const auto cylinder = argumentShapesFor("cylinder");
  CHECK(std::find(cylinder.begin(), cylinder.end(), "cylinder(h = 1, d = 1)") != cylinder.end());

  // mirror([0,0,0]) is an explicitly handled no-op - builtin_mirror leaves the
  // identity matrix in place for a zero vector - and one digit from the common case.
  CHECK(argumentShapesFor("mirror") == std::vector<std::string>{"mirror([0, 0, 0])"});

  // No shape where no value is both a no-op and a step towards what is wanted.
  CHECK(argumentShapesFor("color").empty());
  CHECK(argumentShapesFor("no_such_module").empty());
}
