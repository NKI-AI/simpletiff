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

// Tests for the column crop of DecodeJpeg (libjpeg backend).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <jpeglib.h>

#include "simpletiff/io_utils.h"
#include "simpletiff/reader.h"

namespace {

// Encodes a width x height RGB gradient with 4:2:0 subsampling.
std::vector<uint8_t> EncodeTestJpeg(int width, int height) {
  std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      uint8_t* p = &rgb[(static_cast<size_t>(y) * width + x) * 3];
      p[0] = static_cast<uint8_t>(x * 7 + y);
      p[1] = static_cast<uint8_t>(x * 3 ^ y * 5);
      p[2] = static_cast<uint8_t>((x + y) * 11);
    }
  }
  jpeg_compress_struct cinfo;
  jpeg_error_mgr jerr;
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  unsigned char* out = nullptr;
  unsigned long out_size = 0;
  jpeg_mem_dest(&cinfo, &out, &out_size);
  cinfo.image_width = static_cast<JDIMENSION>(width);
  cinfo.image_height = static_cast<JDIMENSION>(height);
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 90, TRUE);
  jpeg_start_compress(&cinfo, TRUE);
  while (cinfo.next_scanline < cinfo.image_height) {
    JSAMPROW row = &rgb[static_cast<size_t>(cinfo.next_scanline) * width * 3];
    jpeg_write_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_compress(&cinfo);
  std::vector<uint8_t> jpeg(out, out + out_size);
  jpeg_destroy_compress(&cinfo);
  std::free(out);
  return jpeg;
}

TEST(JpegCropTest, CroppedColumnsMatchTheFullDecode) {
  const std::vector<uint8_t> jpeg = EncodeTestJpeg(640, 24);
  simpletiff::DecodeContext ctx;
  const simpletiff::JpegDecodeOptions full_options = {
      .treat_ycbcr_as_rgb = false};
  int full_w = 0;
  int full_h = 0;
  std::vector<uint8_t> full;
  ASSERT_TRUE(
      simpletiff::DecodeJpeg(ctx, jpeg, full_w, full_h, full, full_options));
  ASSERT_EQ(full_w, 640);

  uint32_t decoded_x = 12345;
  const simpletiff::JpegDecodeOptions crop_options = {
      .treat_ycbcr_as_rgb = false,
      .crop_x = 200,
      .crop_width = 100,
      .out_crop_x = &decoded_x,
  };
  int crop_w = 0;
  int crop_h = 0;
  std::vector<uint8_t> crop;
  ASSERT_TRUE(
      simpletiff::DecodeJpeg(ctx, jpeg, crop_w, crop_h, crop, crop_options));
  ASSERT_EQ(crop_h, full_h);
  // The decoded range covers the request, starting at or before it.
  ASSERT_LE(decoded_x, 200u);
  ASSERT_GE(decoded_x + static_cast<uint32_t>(crop_w), 300u);
#ifdef LIBJPEG_TURBO_VERSION
  EXPECT_LT(crop_w, full_w);
#endif
  for (int y = 0; y < crop_h; ++y) {
    for (int x = 0; x < crop_w * 3; ++x) {
      const size_t full_i =
          (static_cast<size_t>(y) * full_w + decoded_x) * 3 + x;
      const size_t crop_i = static_cast<size_t>(y) * crop_w * 3 + x;
      ASSERT_EQ(crop[crop_i], full[full_i]) << "row " << y << " byte " << x;
    }
  }
}

TEST(JpegCropTest, OutOfRangeCropDecodesTheWholeImage) {
  const std::vector<uint8_t> jpeg = EncodeTestJpeg(64, 16);
  simpletiff::DecodeContext ctx;
  uint32_t decoded_x = 12345;
  const simpletiff::JpegDecodeOptions options = {
      .treat_ycbcr_as_rgb = false,
      .crop_x = 60,
      .crop_width = 10,
      .out_crop_x = &decoded_x,
  };
  int w = 0;
  int h = 0;
  std::vector<uint8_t> out;
  ASSERT_TRUE(simpletiff::DecodeJpeg(ctx, jpeg, w, h, out, options));
  EXPECT_EQ(w, 64);
  EXPECT_EQ(decoded_x, 0u);
}

}  // namespace
