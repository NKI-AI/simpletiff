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

#include "simpletiff/io_utils.h"

#include <cstdlib>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include "jpeg-compressor/jpgd.h"
#include "simpletiff/reader.h"

namespace simpletiff {

// `DecodeContext` is declared in reader.h with libjpeg-shaped state fields
// (`jpeg_cinfo`, `jpeg_err`). jpgd doesn't need any persistent decoder
// state, so we just keep those nulled out and reuse the buffer fields.
DecodeContext::DecodeContext() = default;
DecodeContext::~DecodeContext() = default;

DecodeContext::DecodeContext(DecodeContext&& other) noexcept
    : jpeg_stream_buffer(std::move(other.jpeg_stream_buffer)),
      temp_buffer(std::move(other.temp_buffer)),
      jpeg_cinfo(nullptr),
      jpeg_err(nullptr) {}

DecodeContext& DecodeContext::operator=(DecodeContext&& other) noexcept {
  if (this != &other) {
    jpeg_stream_buffer = std::move(other.jpeg_stream_buffer);
    temp_buffer = std::move(other.temp_buffer);
  }
  return *this;
}

bool DecodeJpeg(DecodeContext& /*ctx*/, std::span<const uint8_t> jpeg_data,
                int& out_width, int& out_height, std::vector<uint8_t>& out_rgb,
                const JpegDecodeOptions& options) {
  if (jpeg_data.empty()) {
    return false;
  }

  int actual_comps = 0;
  int width = 0;
  int height = 0;

  const uint32_t flags = options.treat_ycbcr_as_rgb
                             ? jpgd::jpeg_decoder::cFlagNoYCbCrConversion
                             : 0u;

  // Request 3 components (RGB). jpgd allocates the result with malloc().
  unsigned char* decoded = jpgd::decompress_jpeg_image_from_memory(
      jpeg_data.data(), static_cast<int>(jpeg_data.size()), &width, &height,
      &actual_comps, /*req_comps=*/3, flags);

  if (!decoded) {
    return false;
  }

  out_width = width;
  out_height = height;

  const size_t num_pixels =
      static_cast<size_t>(width) * static_cast<size_t>(height);
  out_rgb.resize(num_pixels * 3);
  std::memcpy(out_rgb.data(), decoded, num_pixels * 3);

  std::free(decoded);

  return true;
}

}  // namespace simpletiff
