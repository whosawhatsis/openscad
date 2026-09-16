#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "io/imageutils.h"
#include "lodepng/lodepng.h"

bool write_png_gray16(std::ostream& output, const unsigned char *pixels, int width, int height,
                      const std::vector<std::pair<std::string, std::string>>& text)
{
  std::vector<unsigned char> dataout;
  lodepng::State state;
  for (const auto& [key, value] : text) {
    lodepng_add_text(&state.info_png, key.c_str(), value.c_str());
  }
  // tEXt, not zTXt: see the note on the declaration.
  state.encoder.text_compression = 0;
  state.encoder.auto_convert = false;
  // The caller already hands us big-endian 16-bit greyscale, which is exactly
  // what PNG stores, so tell lodepng not to convert on the way in either.
  state.info_raw.colortype = LCT_GREY;
  state.info_raw.bitdepth = 16;
  state.info_png.color.colortype = LCT_GREY;
  state.info_png.color.bitdepth = 16;
  const auto err = lodepng::encode(dataout, pixels, width, height, state);
  if (err) return false;
  output.write(reinterpret_cast<const char *>(dataout.data()), dataout.size());
  if (output.bad()) std::cerr << "Error writing to ostream\n";
  return output.good();
}
