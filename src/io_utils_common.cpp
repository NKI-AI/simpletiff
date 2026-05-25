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

#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

#include "aifocore/platform/portability.h"

namespace simpletiff {

bool ReadBytes(int fd, size_t file_size, uint64_t offset, uint64_t length,
               std::vector<uint8_t>& out, bool strict) {
  if (length == 0) {
    out.clear();
    return true;
  }

  if (fd < 0) {
    return false;
  }

  const uint64_t file_sz = static_cast<uint64_t>(file_size);

  // In strict mode, validate against the file size up front.
  if (strict) {
    if (offset > file_sz || length > file_sz - offset) {
      return false;
    }
  } else {
    // In loose mode, clamp instead of failing.
    if (offset >= file_sz) {
      out.clear();
      return true;
    }
    if (length > file_sz - offset) {
      length = file_sz - offset;
    }
  }

  if (length == 0) {
    out.clear();
    return true;
  }

  out.resize(static_cast<size_t>(length));

  const ssize_t bytes_read = aifocore::portable_pread(
      fd, out.data(), static_cast<size_t>(length), offset);

  if (bytes_read < 0) {
    return false;
  }

  if (static_cast<size_t>(bytes_read) != length) {
    if (strict) {
      return false;
    }
    // Loose mode: shrink to what we actually got.
    out.resize(static_cast<size_t>(bytes_read));
  }

  return true;
}

std::span<const uint8_t> ReadBytesSpan(int fd, size_t file_size,
                                       uint64_t offset, uint64_t length,
                                       std::vector<uint8_t>& buffer,
                                       bool strict) {
  if (!ReadBytes(fd, file_size, offset, length, buffer, strict)) {
    return {};
  }
  return {buffer.data(), buffer.size()};
}

void ComposeJpegStream(std::span<const uint8_t> tables,
                       std::span<const uint8_t> payload,
                       std::vector<uint8_t>& out) {
  constexpr uint8_t kSoi[2] = {0xFF, 0xD8};
  constexpr uint8_t kEoi[2] = {0xFF, 0xD9};

  const auto starts_with = [](std::span<const uint8_t> s,
                              const uint8_t* marker) -> bool {
    return s.size() >= 2 && s[0] == marker[0] && s[1] == marker[1];
  };
  const auto ends_with = [](std::span<const uint8_t> s,
                            const uint8_t* marker) -> bool {
    return s.size() >= 2 && s[s.size() - 2] == marker[0] &&
           s[s.size() - 1] == marker[1];
  };

  const bool tables_has_soi = starts_with(tables, kSoi);
  const bool tables_has_eoi = ends_with(tables, kEoi);
  const bool payload_has_soi = starts_with(payload, kSoi);
  const bool payload_has_eoi = ends_with(payload, kEoi);

  size_t out_sz = 2 /*SOI*/ + tables.size() + payload.size() + 2 /*EOI*/;
  if (tables_has_soi)
    out_sz -= 2;
  if (tables_has_eoi)
    out_sz -= 2;
  if (payload_has_soi)
    out_sz -= 2;
  if (payload_has_eoi)
    out_sz -= 2;

  out.resize(out_sz);
  uint8_t* ptr = out.data();

  std::memcpy(ptr, kSoi, 2);
  ptr += 2;

  const size_t tables_start = tables_has_soi ? 2 : 0;
  const size_t tables_len =
      tables.size() - tables_start - (tables_has_eoi ? 2 : 0);
  if (tables_len > 0) {
    std::memcpy(ptr, tables.data() + tables_start, tables_len);
    ptr += tables_len;
  }

  const size_t payload_start = payload_has_soi ? 2 : 0;
  const size_t payload_len =
      payload.size() - payload_start - (payload_has_eoi ? 2 : 0);
  if (payload_len > 0) {
    std::memcpy(ptr, payload.data() + payload_start, payload_len);
    ptr += payload_len;
  }

  std::memcpy(ptr, kEoi, 2);
}

void CopyTileInto(uint8_t* dst, int dst_stride, const uint8_t* tile_data,
                  int tile_width, int tile_height, int dst_x, int dst_y,
                  int roi_width, int roi_height, int samples_per_pixel) {
  // Clip tile placement against ROI bounds, including negative offsets.
  const int src_start_x = std::max(0, -dst_x);
  const int src_start_y = std::max(0, -dst_y);
  const int dst_start_x = std::max(0, dst_x);
  const int dst_start_y = std::max(0, dst_y);

  const int max_copy_w = tile_width - src_start_x;
  const int max_copy_h = tile_height - src_start_y;
  const int roi_copy_w = roi_width - dst_start_x;
  const int roi_copy_h = roi_height - dst_start_y;

  const int w_copy = std::min(max_copy_w, roi_copy_w);
  const int h_copy = std::min(max_copy_h, roi_copy_h);

  if (w_copy <= 0 || h_copy <= 0) {
    return;
  }

  const int bytes_per_pixel = samples_per_pixel;  // 8-bit/sample assumption
  for (int r = 0; r < h_copy; ++r) {
    const uint8_t* src_row = tile_data +
                             (src_start_y + r) * tile_width * bytes_per_pixel +
                             src_start_x * bytes_per_pixel;
    uint8_t* dst_row =
        dst + (dst_start_y + r) * dst_stride + dst_start_x * bytes_per_pixel;
    std::memcpy(dst_row, src_row,
                static_cast<size_t>(w_copy) * bytes_per_pixel);
  }
}

}  // namespace simpletiff
