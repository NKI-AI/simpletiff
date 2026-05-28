// Copyright 2025 Jonas Teuwen. All Rights Reserved.
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

// libjpeg-turbo backed JPEG decoder + DecodeContext lifecycle for simpletiff.
// The decoder-agnostic helpers (ReadBytes, ComposeJpegStream, CopyTileInto,
// ...) live in io_utils_common.cpp so that both this file and io_utils_jpgd.cpp
// can share them without drift.

#include "simpletiff/io_utils.h"

#include <jpeglib.h>

#include <span>
#include <utility>
#include <vector>

#include "simpletiff/reader.h"

namespace simpletiff {

DecodeContext::DecodeContext() = default;

DecodeContext::~DecodeContext() {
  if (jpeg_cinfo) {
    jpeg_destroy_decompress(jpeg_cinfo);
    delete jpeg_cinfo;
    delete jpeg_err;
  }
}

DecodeContext::DecodeContext(DecodeContext&& other) noexcept
    : jpeg_stream_buffer(std::move(other.jpeg_stream_buffer)),
      temp_buffer(std::move(other.temp_buffer)),
      jpeg_cinfo(other.jpeg_cinfo),
      jpeg_err(other.jpeg_err) {
  other.jpeg_cinfo = nullptr;
  other.jpeg_err = nullptr;
}

DecodeContext& DecodeContext::operator=(DecodeContext&& other) noexcept {
  if (this != &other) {
    if (jpeg_cinfo) {
      jpeg_destroy_decompress(jpeg_cinfo);
      delete jpeg_cinfo;
      delete jpeg_err;
    }

    jpeg_stream_buffer = std::move(other.jpeg_stream_buffer);
    temp_buffer = std::move(other.temp_buffer);
    jpeg_cinfo = other.jpeg_cinfo;
    jpeg_err = other.jpeg_err;

    other.jpeg_cinfo = nullptr;
    other.jpeg_err = nullptr;
  }
  return *this;
}

namespace {

void SilentJpegOutputMessage(j_common_ptr cinfo) {
  (void)cinfo;  // Suppress all libjpeg warnings.
}

void EnsureJpegDecompressor(DecodeContext& ctx) {
  // Lazily create + reuse the decompressor: setup is expensive and the
  // calling tile executor reuses one DecodeContext per worker thread.
  if (!ctx.jpeg_cinfo) {
    ctx.jpeg_err = new jpeg_error_mgr;
    ctx.jpeg_cinfo = new jpeg_decompress_struct;
    ctx.jpeg_cinfo->err = jpeg_std_error(ctx.jpeg_err);
    ctx.jpeg_err->output_message = SilentJpegOutputMessage;
    jpeg_create_decompress(ctx.jpeg_cinfo);
  } else {
    jpeg_abort_decompress(ctx.jpeg_cinfo);
  }
}

}  // namespace

bool DecodeJpeg(DecodeContext& ctx, std::span<const uint8_t> jpeg_data,
                int& out_width, int& out_height, std::vector<uint8_t>& out_rgb,
                const JpegDecodeOptions& options) {
  if (jpeg_data.empty()) {
    return false;
  }

  EnsureJpegDecompressor(ctx);
  jpeg_decompress_struct& cinfo = *ctx.jpeg_cinfo;

  jpeg_mem_src(&cinfo, const_cast<uint8_t*>(jpeg_data.data()),
               static_cast<size_t>(jpeg_data.size()));

  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    return false;
  }

  // Color-space handling. TIFF-in-JPEG comes in two flavours in the wild:
  //  - genuine YCbCr (Photometric=YCbCr) that needs YCbCr->RGB conversion;
  //  - "legacy RGB" that's actually stored as JCS_YCbCr but should be read
  //    verbatim (treat_ycbcr_as_rgb = true skips conversion).
  if (cinfo.jpeg_color_space == JCS_YCbCr) {
    if (options.treat_ycbcr_as_rgb) {
      cinfo.jpeg_color_space = JCS_RGB;
      cinfo.out_color_space = JCS_RGB;
    } else {
      cinfo.out_color_space = JCS_RGB;
    }
  } else if (cinfo.jpeg_color_space == JCS_RGB) {
    cinfo.out_color_space = JCS_RGB;
  } else if (cinfo.jpeg_color_space == JCS_GRAYSCALE) {
    cinfo.out_color_space = JCS_GRAYSCALE;
  } else {
    cinfo.out_color_space = JCS_RGB;
  }

  cinfo.dct_method = JDCT_IFAST;      // Fast integer DCT.
  cinfo.do_fancy_upsampling = FALSE;  // Speed win on YCbCr 4:2:0.
  cinfo.do_block_smoothing = FALSE;   // Disable smoothing for speed.

  if (jpeg_start_decompress(&cinfo) != TRUE) {
    jpeg_abort_decompress(&cinfo);
    return false;
  }

  out_width = static_cast<int>(cinfo.output_width);
  out_height = static_cast<int>(cinfo.output_height);

  const size_t row_stride = static_cast<size_t>(cinfo.output_width) *
                            static_cast<size_t>(cinfo.output_components);
  out_rgb.resize(row_stride * cinfo.output_height);

  while (cinfo.output_scanline < cinfo.output_height) {
    JSAMPROW row_pointer =
        out_rgb.data() +
        static_cast<size_t>(cinfo.output_scanline) * row_stride;
    if (jpeg_read_scanlines(&cinfo, &row_pointer, 1) != 1) {
      jpeg_abort_decompress(&cinfo);
      return false;
    }
  }

  if (jpeg_finish_decompress(&cinfo) != TRUE) {
    jpeg_abort_decompress(&cinfo);
    return false;
  }

  return true;
}

}  // namespace simpletiff
