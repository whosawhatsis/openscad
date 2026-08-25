#include <catch2/catch_test_macros.hpp>

#include <QString>

#include "gui/SnippetFields.h"

namespace {

QStringList fieldsOf(const QString& shape)
{
  QStringList out;
  for (const auto& field : shapeFieldRanges(shape)) out << shape.mid(field.first, field.second);
  return out;
}

}  // namespace

TEST_CASE("a shape's editable fields are its argument values", "[completion]")
{
  CHECK(fieldsOf("translate([0,0,0])") == QStringList{"0", "0", "0"});
  CHECK(fieldsOf("scale([1,1,1])") == QStringList{"1", "1", "1"});
  CHECK(fieldsOf("square([1,1])") == QStringList{"1", "1"});

  // Named arguments: the value is a field, the name is not.
  CHECK(fieldsOf("sphere(r=1)") == QStringList{"1"});
  CHECK(fieldsOf("rotate_extrude(angle=360)") == QStringList{"360"});
  CHECK(fieldsOf("linear_extrude(height=1,twist=0)") == QStringList{"1", "0"});

  // A digit inside a parameter name is not a value. This is why a field has to
  // follow a delimiter: r1 and r2 would otherwise contribute phantom fields.
  CHECK(fieldsOf("cylinder(h=1,r1=1,r2=1)") == QStringList{"1", "1", "1"});
  CHECK(fieldsOf("cylinder(h=1,d=1)") == QStringList{"1", "1"});

  // Booleans and mixed forms.
  CHECK(fieldsOf("cube([1,1,1],center=true)") == QStringList{"1", "1", "1", "true"});
  CHECK(fieldsOf("rotate(0,[0,0,1])") == QStringList{"0", "0", "0", "1"});

  // Negative and fractional values stay single fields.
  CHECK(fieldsOf("translate([-1,0.5,+2])") == QStringList{"-1", "0.5", "+2"});

  // Strings are one field including their quotes.
  CHECK(fieldsOf("color(\"red\")") == QStringList{"\"red\""});

  // Nothing editable.
  CHECK(fieldsOf("children()").isEmpty());
  CHECK(fieldsOf("cube()").isEmpty());
  CHECK(fieldsOf("").isEmpty());
}

TEST_CASE("inserted calls put a space after every comma", "[completion]")
{
  // Menu entries must be space-free, because Scintilla treats a space in a list
  // entry as the end of the inserted word. The spacing is restored on insertion.
  CHECK(spacedAfterCommas("cube([1,1,1])") == "cube([1, 1, 1])");
  CHECK(spacedAfterCommas("cube([1,1,1],center=true);") == "cube([1, 1, 1], center=true);");
  CHECK(spacedAfterCommas("rotate(0,[0,0,1])") == "rotate(0, [0, 0, 1])");

  // Idempotent: a comma already followed by a space is left alone.
  CHECK(spacedAfterCommas("cube([1, 1, 1])") == "cube([1, 1, 1])");

  // Nothing to do.
  CHECK(spacedAfterCommas("translate([0,0,0])") == "translate([0, 0, 0])");
  CHECK(spacedAfterCommas("sphere(r=1)") == "sphere(r=1)");
  CHECK(spacedAfterCommas("") == "");

  // A comma inside a string literal is the author's, not ours to reformat.
  CHECK(spacedAfterCommas("color(\"a,b\")") == "color(\"a,b\")");
  CHECK(spacedAfterCommas("f(\"a,b\",1,2)") == "f(\"a,b\", 1, 2)");
}
