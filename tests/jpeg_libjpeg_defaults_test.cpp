// Copyright 2026 Jonas Teuwen. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// DecodeJpeg decodes with libjpeg's default settings, so its output matches
// libtiff, OpenSlide and Pillow. Built only with the libjpeg backend.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

// <jpeglib.h> uses size_t and the global FILE without including their
// headers, so <stdio.h> must come first.
// clang-format off
#include <stdio.h>
#include <jpeglib.h>
// clang-format on

#include "gtest/gtest.h"
#include "simpletiff/io_utils.h"
#include "simpletiff/reader.h"

namespace simpletiff {
namespace {

constexpr int kWidth = 64;
constexpr int kHeight = 48;

// A colour gradient with some texture. `ycbcr_420` encodes it as YCbCr with
// 2x2 chroma subsampling, where both the IDCT and the chroma upsampling change
// the output; otherwise as RGB-coded JPEG, where only the IDCT does.
std::vector<uint8_t> EncodeTestJpeg(bool ycbcr_420) {
  std::vector<uint8_t> rgb(static_cast<size_t>(kWidth) * kHeight * 3);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      uint8_t* px = &rgb[(static_cast<size_t>(y) * kWidth + x) * 3];
      px[0] = static_cast<uint8_t>(x * 4);
      px[1] = static_cast<uint8_t>(y * 5);
      px[2] = static_cast<uint8_t>(((x ^ y) & 7) * 32);
    }
  }
  jpeg_compress_struct cinfo{};
  jpeg_error_mgr jerr{};
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  unsigned char* out = nullptr;
  unsigned long out_size = 0;  // NOLINT(runtime/int): libjpeg API type.
  jpeg_mem_dest(&cinfo, &out, &out_size);
  cinfo.image_width = kWidth;
  cinfo.image_height = kHeight;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;
  jpeg_set_defaults(&cinfo);  // YCbCr 4:2:0
  if (!ycbcr_420) {
    jpeg_set_colorspace(&cinfo, JCS_RGB);  // no colour transform or subsampling
  }
  jpeg_set_quality(&cinfo, 75, TRUE);
  jpeg_start_compress(&cinfo, TRUE);
  while (cinfo.next_scanline < cinfo.image_height) {
    JSAMPROW row = &rgb[static_cast<size_t>(cinfo.next_scanline) * kWidth * 3];
    jpeg_write_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_compress(&cinfo);
  std::vector<uint8_t> jpeg(out, out + out_size);
  jpeg_destroy_compress(&cinfo);
  free(out);
  return jpeg;
}

// A plain libjpeg decode that keeps every default setting.
std::vector<uint8_t> ReferenceDecode(const std::vector<uint8_t>& jpeg) {
  jpeg_decompress_struct cinfo{};
  jpeg_error_mgr jerr{};
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, const_cast<uint8_t*>(jpeg.data()), jpeg.size());
  jpeg_read_header(&cinfo, TRUE);
  cinfo.out_color_space = JCS_RGB;
  jpeg_start_decompress(&cinfo);
  std::vector<uint8_t> rgb(static_cast<size_t>(cinfo.output_width) *
                           cinfo.output_height * 3);
  while (cinfo.output_scanline < cinfo.output_height) {
    JSAMPROW row = &rgb[static_cast<size_t>(cinfo.output_scanline) *
                        cinfo.output_width * 3];
    jpeg_read_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  return rgb;
}

std::vector<uint8_t> Decode(DecodeContext& ctx,
                            const std::vector<uint8_t>& jpeg) {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgb;
  JpegDecodeOptions options;
  options.treat_ycbcr_as_rgb = false;
  EXPECT_TRUE(DecodeJpeg(ctx, jpeg, width, height, rgb, options));
  EXPECT_EQ(width, kWidth);
  EXPECT_EQ(height, kHeight);
  return rgb;
}

TEST(JpegLibjpegDefaultsTest, YcbcrSubsampledMatchesLibjpegDefaults) {
  const auto jpeg = EncodeTestJpeg(/*ycbcr_420=*/true);
  DecodeContext ctx;
  EXPECT_EQ(Decode(ctx, jpeg), ReferenceDecode(jpeg));
}

TEST(JpegLibjpegDefaultsTest, RgbCodedMatchesLibjpegDefaults) {
  const auto jpeg = EncodeTestJpeg(/*ycbcr_420=*/false);
  DecodeContext ctx;
  EXPECT_EQ(Decode(ctx, jpeg), ReferenceDecode(jpeg));
}

// The decompressor in a context is reused across decodes.
TEST(JpegLibjpegDefaultsTest, ReusedContextMatchesLibjpegDefaults) {
  const auto ycbcr = EncodeTestJpeg(/*ycbcr_420=*/true);
  const auto rgb = EncodeTestJpeg(/*ycbcr_420=*/false);
  DecodeContext ctx;
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(Decode(ctx, ycbcr), ReferenceDecode(ycbcr));
    EXPECT_EQ(Decode(ctx, rgb), ReferenceDecode(rgb));
  }
}

}  // namespace
}  // namespace simpletiff
