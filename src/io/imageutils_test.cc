#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "io/imageutils.h"
#include "lodepng/lodepng.h"

namespace {

//! Decode a PNG's text chunks back out, so the tests assert what a reader sees
//! rather than what we think we wrote.
std::vector<std::pair<std::string, std::string>> readText(const std::string& png)
{
  lodepng::State state;
  state.decoder.read_text_chunks = 1;
  state.decoder.remember_unknown_chunks = 1;
  unsigned width = 0, height = 0;
  std::vector<unsigned char> out;
  const auto err = lodepng::decode(out, width, height, state,
                                   reinterpret_cast<const unsigned char *>(png.data()), png.size());
  std::vector<std::pair<std::string, std::string>> text;
  if (err) return text;
  for (size_t i = 0; i < state.info_png.text_num; ++i) {
    text.emplace_back(state.info_png.text_keys[i], state.info_png.text_strings[i]);
  }
  return text;
}

std::string encodeGray16(const std::vector<std::pair<std::string, std::string>>& text)
{
  // 2x1 pixels of 16-bit grey, big-endian, which is what the depth path hands over.
  const unsigned char pixels[4] = {0x12, 0x34, 0x56, 0x78};
  std::ostringstream out(std::ios::binary);
  REQUIRE(write_png_gray16(out, pixels, 2, 1, text));
  return out.str();
}

}  // namespace

TEST_CASE("a 16-bit depth PNG carries its metadata in a text chunk", "[imageutils]")
{
  // The metric profiles are always 16-bit, and the 16-bit writer is lodepng on
  // every platform - so unlike the 8-bit outputs, this metadata is not
  // platform-dependent.
  const auto png = encodeGray16({{"openscad.depthmap", "{\"units_per_mm\": 100}"}});
  const auto text = readText(png);
  REQUIRE(text.size() == 1);
  CHECK(text[0].first == "openscad.depthmap");
  CHECK(text[0].second == "{\"units_per_mm\": 100}");
}

TEST_CASE("text chunks do not disturb the pixels", "[imageutils]")
{
  // A depth map's whole value is its numbers; metadata must not cost a bit of it.
  const auto bare = encodeGray16({});
  const auto tagged = encodeGray16({{"openscad.camera", "{}"}});
  lodepng::State s1, s2;
  unsigned w = 0, h = 0;
  std::vector<unsigned char> a, b;
  REQUIRE(lodepng::decode(a, w, h, s1, reinterpret_cast<const unsigned char *>(bare.data()),
                          bare.size()) == 0);
  REQUIRE(lodepng::decode(b, w, h, s2, reinterpret_cast<const unsigned char *>(tagged.data()),
                          tagged.size()) == 0);
  CHECK(a == b);
}

TEST_CASE("writing no metadata leaves the file unchanged", "[imageutils]")
{
  // An ordinary export must not start carrying chunks it never used to.
  CHECK(readText(encodeGray16({})).empty());
}
