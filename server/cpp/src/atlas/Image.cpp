#include "Image.h"

#include <png.h>

namespace atlas {

Image::Image() {}
Image::~Image() {}

void Image::load(const std::string &path) {
  // Based upon https://gist.github.com/niw/5963798
  FILE *fp = fopen(path.c_str(), "rb");

  png_structp png =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) return;

  png_infop info = png_create_info_struct(png);
  if (!info) return;

  if (setjmp(png_jmpbuf(png))) return;

  png_init_io(png, fp);

  png_read_info(png, info);

  _width = png_get_image_width(png, info);
  _height = png_get_image_height(png, info);
  png_byte color_type = png_get_color_type(png, info);
  png_byte bit_depth = png_get_bit_depth(png, info);

  // Read any color_type into 8bit depth, RGBA format.
  // See http://www.libpng.org/pub/png/libpng-manual.txt

  if (bit_depth == 16) png_set_strip_16(png);

  if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);

  // PNG_COLOR_TYPE_GRAY_ALPHA is always 8 or 16bit depth.
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    png_set_expand_gray_1_2_4_to_8(png);

  if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);

  // These color_type don't have an alpha channel then fill it with 0xff.
  if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY ||
      color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

  if (color_type == PNG_COLOR_TYPE_GRAY ||
      color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png);

  png_read_update_info(png, info);

  png_bytepp rows = (png_bytepp)png_malloc(png, sizeof(png_bytep) * _height);
  for (uint32_t y = 0; y < _height; y++) {
    rows[y] = (png_bytep)malloc(png_get_rowbytes(png, info));
  }

  png_read_image(png, rows);

  _pixels.resize(_width * _height);
  for (uint32_t y = 0; y < _height; ++y) {
    png_bytep row = rows[y];
    for (uint32_t x = 0; x < _width; ++x) {
      Pixel &p = _pixels[y * _width + x];
      p.r = *row++;
      p.g = *row++;
      p.b = *row++;
      p.a = *row++;
    }
  }

  fclose(fp);
  png_destroy_read_struct(&png, &info, NULL);

  for (int y = 0; y < _height; y++) {
    free(rows[y]);
  }
  free(rows);
}

Image::Pixel *Image::pixels() { return _pixels.data(); }

const Image::Pixel *Image::pixels() const { return _pixels.data(); }

uint32_t Image::width() const { return _width; }
uint32_t Image::height() const { return _height; }

Image::Pixel &Image::operator()(uint32_t x, uint32_t y) {
  return _pixels[y * _width + x];
}

const Image::Pixel &Image::operator()(uint32_t x, uint32_t y) const {
  return _pixels[y * _width + x];
}

}  // namespace atlas
