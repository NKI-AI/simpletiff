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

// 8-bit YCbCr -> RGB of JPEG 2000 tiles: rounded integer tables, with
//   R = Y + 1.402 (Cr - 128)
//   G = Y - 0.34414 (Cb - 128) - 0.71414 (Cr - 128)
//   B = Y + 1.772 (Cb - 128)
// rounded to nearest and clamped to [0, 255].
//
// Compiled only when OpenJPEG is available.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "aifocore/status/result.h"
#include "gtest/gtest.h"
#include "simpletiff/jpeg2000.h"

#if defined(SIMPLETIFF_HAS_OPENJPEG)

extern "C" {
#include "openjpeg.h"
}  // extern "C"

namespace simpletiff {

// Declared in src/jpeg2000.cpp for test-only use.
aifocore::Result<void> PackInterleavedForTest(
    const opj_image_t& image, bool file_big_endian,
    uint16_t expected_bits_per_sample, uint16_t expected_samples_per_pixel,
    bool convert_ycbcr_to_rgb, int& out_width, int& out_height,
    std::vector<uint8_t>& out);

namespace {

using Ycc = std::array<int, 3>;
using Rgb = std::array<uint8_t, 3>;

// (Y, Cb, Cr) samples away from rounding ties, with the expected RGB. Each
// one decodes 1 grey level differently with truncation.
struct Sample {
  Ycc ycc;
  Rgb rgb;
};
constexpr std::array<Sample, 8> kSamples = {{
    {{81, 90, 240}, {238, 14, 14}},
    {{145, 54, 34}, {13, 238, 14}},
    {{41, 240, 110}, {16, 15, 239}},
    {{200, 100, 150}, {231, 194, 150}},
    {{10, 200, 60}, {0, 34, 138}},
    {{50, 160, 90}, {0, 66, 107}},
    {{128, 0, 255}, {255, 81, 0}},
    {{128, 255, 0}, {0, 176, 255}},
}};

// The double-precision reference: round to nearest, then clamp.
Rgb Reference(const Ycc& ycc) {
  const double y = ycc[0];
  const double cb = ycc[1] - 128.0;
  const double cr = ycc[2] - 128.0;
  const auto to8 = [](double v) {
    return static_cast<uint8_t>(std::clamp(std::floor(v + 0.5), 0.0, 255.0));
  };
  return {to8(y + 1.402 * cr), to8(y - 0.34414 * cb - 0.71414 * cr),
          to8(y + 1.772 * cb)};
}

// An 8-bit 4:4:4 YCbCr image of `width` x `height` pixels, row-major.
opj_image_t* MakeImage(const std::vector<Ycc>& pixels, uint32_t width) {
  const uint32_t height = static_cast<uint32_t>(pixels.size()) / width;
  opj_image_cmptparm_t params[3]{};
  for (auto& p : params) {
    p.prec = 8;
    p.sgnd = 0;
    p.dx = 1;
    p.dy = 1;
    p.w = width;
    p.h = height;
  }
  opj_image_t* image = opj_image_create(3, params, OPJ_CLRSPC_SYCC);
  if (image == nullptr) {
    return nullptr;
  }
  image->x0 = 0;
  image->y0 = 0;
  image->x1 = width;
  image->y1 = height;
  for (size_t i = 0; i < pixels.size(); ++i) {
    for (int c = 0; c < 3; ++c) {
      image->comps[c].data[i] = pixels[i][c];
    }
  }
  return image;
}

// The interleaved RGB the packer produces for `pixels`.
std::vector<Rgb> Convert(const std::vector<Ycc>& pixels, uint32_t width) {
  opj_image_t* image = MakeImage(pixels, width);
  EXPECT_NE(image, nullptr);
  if (image == nullptr) {
    return {};
  }
  std::vector<uint8_t> out;
  int w = 0;
  int h = 0;
  auto result =
      PackInterleavedForTest(*image, /*file_big_endian=*/false,
                             /*expected_bits_per_sample=*/8,
                             /*expected_samples_per_pixel=*/3,
                             /*convert_ycbcr_to_rgb=*/true, w, h, out);
  opj_image_destroy(image);
  EXPECT_TRUE(result) << result.error().ToString();
  EXPECT_EQ(out.size(), pixels.size() * 3);
  std::vector<Rgb> rgb(out.size() / 3);
  std::memcpy(rgb.data(), out.data(), rgb.size() * 3);
  return rgb;
}

TEST(Jpeg2000Ycbcr8Test, NeutralChromaReturnsY) {
  std::vector<Ycc> pixels;
  for (int y = 0; y < 256; ++y) {
    pixels.push_back({y, 128, 128});
  }
  const auto rgb = Convert(pixels, 256);
  ASSERT_EQ(rgb.size(), pixels.size());
  for (int y = 0; y < 256; ++y) {
    const auto v = static_cast<uint8_t>(y);
    EXPECT_EQ(rgb[y], (Rgb{v, v, v})) << "Y=" << y;
  }
}

TEST(Jpeg2000Ycbcr8Test, SamplesMatchRoundedReference) {
  std::vector<Ycc> pixels;
  for (const auto& s : kSamples) {
    pixels.push_back(s.ycc);
  }
  const auto rgb = Convert(pixels, static_cast<uint32_t>(pixels.size()));
  ASSERT_EQ(rgb.size(), kSamples.size());
  for (size_t i = 0; i < kSamples.size(); ++i) {
    EXPECT_EQ(Reference(kSamples[i].ycc), kSamples[i].rgb) << "sample " << i;
    EXPECT_EQ(rgb[i], kSamples[i].rgb) << "sample " << i;
  }
}

// Every (Cb, Cr) pair at mid-grey. At Y = 128 the reference has no exact
// .5 ties that are not clamped, so the tables must match it everywhere.
TEST(Jpeg2000Ycbcr8Test, AllChromaPairsMatchRoundedReference) {
  std::vector<Ycc> pixels;
  for (int cb = 0; cb < 256; ++cb) {
    for (int cr = 0; cr < 256; ++cr) {
      pixels.push_back({128, cb, cr});
    }
  }
  const auto rgb = Convert(pixels, 256);
  ASSERT_EQ(rgb.size(), pixels.size());
  int mismatches = 0;
  for (size_t i = 0; i < pixels.size(); ++i) {
    if (rgb[i] != Reference(pixels[i]) && ++mismatches <= 5) {
      ADD_FAILURE() << "Cb=" << pixels[i][1] << " Cr=" << pixels[i][2];
    }
  }
  EXPECT_EQ(mismatches, 0);
}

TEST(Jpeg2000Ycbcr8Test, ExtremesClamp) {
  const std::vector<Ycc> pixels = {
      {0, 0, 0},        // R, B below 0; G above Y
      {255, 255, 255},  // R, B above 255; G below Y
      {255, 0, 0},      // G above 255
      {0, 255, 255},    // G below 0
      {0, 128, 128},   {255, 128, 128},
  };
  const std::vector<Rgb> expected = {
      {0, 135, 0},   {255, 121, 255}, {76, 255, 28},
      {178, 0, 225}, {0, 0, 0},       {255, 255, 255},
  };
  const auto rgb = Convert(pixels, static_cast<uint32_t>(pixels.size()));
  ASSERT_EQ(rgb.size(), expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(rgb[i], expected[i]) << "pixel " << i;
    EXPECT_EQ(Reference(pixels[i]), expected[i]) << "pixel " << i;
  }
}

// In-memory output stream for the OpenJPEG encoder.
struct MemoryStream {
  std::vector<uint8_t> data;
  size_t pos = 0;
};

OPJ_SIZE_T StreamWrite(void* buffer, OPJ_SIZE_T n, void* user) {
  auto* s = static_cast<MemoryStream*>(user);
  if (s->pos + n > s->data.size()) {
    s->data.resize(s->pos + n);
  }
  std::memcpy(s->data.data() + s->pos, buffer, n);
  s->pos += n;
  return n;
}

OPJ_OFF_T StreamSkip(OPJ_OFF_T n, void* user) {
  auto* s = static_cast<MemoryStream*>(user);
  s->pos += static_cast<size_t>(n);
  if (s->pos > s->data.size()) {
    s->data.resize(s->pos);
  }
  return n;
}

OPJ_BOOL StreamSeek(OPJ_OFF_T pos, void* user) {
  auto* s = static_cast<MemoryStream*>(user);
  s->pos = static_cast<size_t>(pos);
  if (s->pos > s->data.size()) {
    s->data.resize(s->pos);
  }
  return OPJ_TRUE;
}

// A lossless J2K codestream of `image`, with no component transform, so the
// decoder returns exactly the YCbCr samples that were encoded.
std::vector<uint8_t> EncodeLossless(opj_image_t* image) {
  opj_cparameters_t params;
  opj_set_default_encoder_parameters(&params);
  params.tcp_numlayers = 1;
  params.tcp_rates[0] = 0;  // lossless
  params.cp_disto_alloc = 1;
  params.irreversible = 0;
  params.tcp_mct = 0;
  params.numresolution = 1;

  MemoryStream out;
  opj_codec_t* codec = opj_create_compress(OPJ_CODEC_J2K);
  opj_stream_t* stream =
      opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, OPJ_FALSE);
  opj_stream_set_user_data(stream, &out, nullptr);
  opj_stream_set_write_function(stream, StreamWrite);
  opj_stream_set_skip_function(stream, StreamSkip);
  opj_stream_set_seek_function(stream, StreamSeek);
  const bool ok = opj_setup_encoder(codec, &params, image) &&
                  opj_start_compress(codec, image, stream) &&
                  opj_encode(codec, stream) && opj_end_compress(codec, stream);
  opj_stream_destroy(stream);
  opj_destroy_codec(codec);
  EXPECT_TRUE(ok);
  return ok ? out.data : std::vector<uint8_t>{};
}

// End to end through `DecodeJpeg2000`: a small 8-bit YCbCr codestream decodes
// to known pixels.
TEST(Jpeg2000Ycbcr8Test, CodestreamDecodesToKnownPixels) {
  std::vector<Ycc> pixels;
  for (const auto& s : kSamples) {
    pixels.push_back(s.ycc);
  }
  constexpr uint32_t kWidth = 4;  // 4 x 2 pixels
  opj_image_t* image = MakeImage(pixels, kWidth);
  ASSERT_NE(image, nullptr);
  const auto codestream = EncodeLossless(image);
  opj_image_destroy(image);
  ASSERT_FALSE(codestream.empty());

  std::vector<uint8_t> out;
  int w = 0;
  int h = 0;
  auto result = DecodeJpeg2000(std::span<const uint8_t>(codestream),
                               /*file_big_endian=*/false,
                               /*expected_bits_per_sample=*/8,
                               /*expected_samples_per_pixel=*/3,
                               /*convert_ycbcr_to_rgb=*/true, w, h, out);
  ASSERT_TRUE(result) << result.error().ToString();
  ASSERT_EQ(w, static_cast<int>(kWidth));
  ASSERT_EQ(h, 2);
  ASSERT_EQ(out.size(), kSamples.size() * 3);
  for (size_t i = 0; i < kSamples.size(); ++i) {
    const Rgb got = {out[i * 3], out[i * 3 + 1], out[i * 3 + 2]};
    EXPECT_EQ(got, kSamples[i].rgb) << "pixel " << i;
  }
}

}  // namespace
}  // namespace simpletiff

#endif  // SIMPLETIFF_HAS_OPENJPEG
