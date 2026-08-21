#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "io/imageutils.h"
#include "lodepng/lodepng.h"

namespace {

// A 2x2 RGBA image: opaque red, opaque green, fully transparent blue, half-transparent white.
std::vector<uint8_t> testPixels()
{
  return {
    255, 0,   0,   255,  //
    0,   255, 0,   255,  //
    0,   0,   255, 0,    //
    255, 255, 255, 128,  //
  };
}

// Encode via write_png(), then decode back with lodepng, forcing the decoder to report whatever
// colortype the file actually declares rather than converting it for us.
lodepng::State decodePng(const std::string& png, std::vector<uint8_t>& out, unsigned& w, unsigned& h)
{
  lodepng::State state;
  state.decoder.color_convert = 0;
  const std::vector<uint8_t> in(png.begin(), png.end());
  REQUIRE(lodepng::decode(out, w, h, state, in) == 0);
  return state;
}

std::string encode(bool with_alpha)
{
  auto pixels = testPixels();
  std::ostringstream out(std::ios::binary);
  REQUIRE(write_png(out, pixels.data(), 2, 2, with_alpha));
  return out.str();
}

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

TEST_CASE("write_png discards alpha by default", "[imageutils]")
{
  std::vector<uint8_t> decoded;
  unsigned w = 0, h = 0;
  auto state = decodePng(encode(false), decoded, w, h);

  CHECK(w == 2);
  CHECK(h == 2);
  // No alpha channel at all, so nothing downstream can misinterpret it.
  CHECK(state.info_png.color.colortype == LCT_RGB);
  REQUIRE(decoded.size() == 2 * 2 * 3);
  // Colors survive, including the one whose source alpha was 0.
  CHECK(decoded[0] == 255);  // red
  CHECK(decoded[1] == 0);
  CHECK(decoded[2] == 0);
  CHECK(decoded[6] == 0);  // blue, was fully transparent
  CHECK(decoded[7] == 0);
  CHECK(decoded[8] == 255);
}

TEST_CASE("write_png preserves alpha when asked", "[imageutils]")
{
  std::vector<uint8_t> decoded;
  unsigned w = 0, h = 0;
  auto state = decodePng(encode(true), decoded, w, h);

  CHECK(w == 2);
  CHECK(h == 2);
  CHECK(state.info_png.color.colortype == LCT_RGBA);
  REQUIRE(decoded.size() == 2 * 2 * 4);
  CHECK(decoded[3] == 255);   // opaque red stays opaque
  CHECK(decoded[11] == 0);    // fully transparent pixel stays transparent
  CHECK(decoded[15] == 128);  // partial alpha is not rounded to 0 or 255
  // Alpha must not be premultiplied into the color channels.
  CHECK(decoded[12] == 255);
  CHECK(decoded[13] == 255);
  CHECK(decoded[14] == 255);
}

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
