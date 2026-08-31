#pragma once

#include <string>
#include <utility>
#include <vector>

#include <cstdlib>
#include <iostream>

// pixels is always RGBA; with_alpha=false discards the alpha channel on output.
bool write_png(const char *filename, unsigned char *pixels, int width, int height,
               bool with_alpha = false);
bool write_png(std::ostream& output, unsigned char *pixels, int width, int height,
               bool with_alpha = false);
/*!
   Write 16-bit greyscale PNG, optionally with tEXt metadata chunks.

   The metric depth profiles are always 16 bits, and this writer is lodepng on
   every platform - so metadata written here behaves the same everywhere. The
   8-bit path is not like that: macOS writes those through CoreGraphics, which
   exposes only a fixed set of PNG keywords, so anything embedded there would be
   platform-dependent until that path is unified on lodepng (upstream #6962).

   Uncompressed tEXt rather than zTXt: these payloads are a few hundred bytes and
   readable in a hex dump, which is worth more than the saving.
 */
bool write_png_gray16(std::ostream& output, const unsigned char *pixels, int width, int height,
                      const std::vector<std::pair<std::string, std::string>>& text = {});
void flip_image(const unsigned char *src, unsigned char *dst, size_t pixelsize, size_t width,
                size_t height);
