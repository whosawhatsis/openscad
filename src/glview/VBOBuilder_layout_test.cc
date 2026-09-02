#include <catch2/catch_all.hpp>

#include "glview/VBOBuilder.h"

// The interleaved layout of the shader attribute block (registry row 64).
//
// This is a weaker test than the ones on the CPU-side finish maths, and
// deliberately so: what actually matters here -- that the shader receives the
// right floats -- needs a GL context and a render comparison, which belongs
// with the fragment-shader work. What this pins is the one thing that breaks
// silently and cannot be seen in a screenshot without knowing to look: the
// offsets the renderer computes for glVertexAttribPointer. If the axis
// attribute is inserted in the wrong order, or sized wrong, every attribute
// after it reads from the wrong place in the buffer and the surface shades
// from whatever bytes happen to be there.

TEST_CASE("the shader attribute block is barycentric, material, material axis",
          "[glview][VBOBuilder][anisotropy]")
{
  // The order is the ShaderAttribIndex enum, and the renderer indexes the
  // attribute vector by it directly. Appending rather than inserting keeps the
  // two existing offsets unchanged.
  CHECK(BARYCENTRIC_ATTRIB == 0);
  CHECK(MATERIAL_ATTRIB == 1);
  CHECK(MATERIAL_AXIS_ATTRIB == 2);
}

TEST_CASE("the axis attribute is four floats laid out after the finish",
          "[glview][VBOBuilder][anisotropy]")
{
  // Mirrors what VBOBuilder::addShaderData() builds.
  VertexData data;
  data.addAttributeData(std::make_shared<AttributeData<GLubyte, 4, GL_UNSIGNED_BYTE>>());
  data.addAttributeData(std::make_shared<AttributeData<GLfloat, 4, GL_FLOAT>>());
  data.addAttributeData(std::make_shared<AttributeData<GLfloat, 4, GL_FLOAT>>());

  REQUIRE(data.attributes().size() == 3);

  const size_t barycentricSize = 4 * sizeof(GLubyte);
  const size_t finishSize = 4 * sizeof(GLfloat);

  CHECK(data.attributes()[MATERIAL_AXIS_ATTRIB]->count() == 4);
  CHECK(data.attributes()[MATERIAL_AXIS_ATTRIB]->glType() == GL_FLOAT);

  // The axis block starts after barycentric and the finish, and adding it must
  // not have moved either of them.
  CHECK(data.interleavedOffset(BARYCENTRIC_ATTRIB) == 0);
  CHECK(data.interleavedOffset(MATERIAL_ATTRIB) == barycentricSize);
  CHECK(data.interleavedOffset(MATERIAL_AXIS_ATTRIB) == barycentricSize + finishSize);
  CHECK(data.stride() == barycentricSize + finishSize + finishSize);
}
