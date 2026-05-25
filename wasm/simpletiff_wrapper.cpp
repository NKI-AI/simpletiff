// Copyright 2025 SimpleTIFF Authors
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

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// NOLINTNEXTLINE(build/include_order)
#include <emscripten/bind.h>
// NOLINTNEXTLINE(build/include_order)
#include <emscripten/val.h>

#include "aifocore/platform/portability.h"
#include "simpletiff/index.h"
#include "simpletiff/reader.h"
#include "simpletiff/tiff_parser.h"

namespace simpletiff::wasm {

/// Internal state for SimpleTiffReaderWrapper
/// Manages file descriptor and TiffIndex lifecycle
struct ReaderState {
  int fd = -1;
  simpletiff::TiffIndex index;
  std::string file_path;
  bool is_temp_file = false;

  ~ReaderState() {
    // Index destructor will handle closing fd
    // Don't close fd here as index owns it
    fd = -1;
    // Clean up temp file if it exists and we marked it as temporary
    if (!file_path.empty() && is_temp_file) {
      aifocore::portable_unlink(file_path.c_str());
    }
  }

  // Non-copyable, non-moveable
  ReaderState() = default;
  ReaderState(const ReaderState&) = delete;
  ReaderState& operator=(const ReaderState&) = delete;
  ReaderState(ReaderState&&) = delete;
  ReaderState& operator=(ReaderState&&) = delete;
};

/// Forward declaration
class SimpleTiffReaderWrapper;

/// WASM wrapper for a TIFF page
class SimpleTiffPageWrapper {
 public:
  SimpleTiffPageWrapper(std::shared_ptr<ReaderState> state, uint32_t page_index)
      : state_(std::move(state)), page_index_(page_index) {
    if (!state_ || page_index_ >= state_->index.NumPages()) {
      throw std::runtime_error("Invalid page index");
    }
  }

  /// Get page width
  uint32_t width() const { return state_->index.Page(page_index_).width; }

  /// Get page height
  uint32_t height() const { return state_->index.Page(page_index_).height; }

  /// Get samples per pixel (channels)
  uint16_t samplesPerPixel() const {
    return state_->index.Page(page_index_).samples_per_pixel;
  }

  /// Get bits per sample
  uint16_t bitsPerSample() const {
    return state_->index.Page(page_index_).bits_per_sample;
  }

  /// Get photometric interpretation
  uint16_t photometric() const {
    return state_->index.Page(page_index_).photometric;
  }

  /// Get compression type code
  uint16_t compression() const {
    return state_->index.Page(page_index_).compression;
  }

  /// Get storage type as string
  std::string storageType() const {
    switch (state_->index.Page(page_index_).storage) {
      case simpletiff::Storage::kTiles:
        return "tiled";
      case simpletiff::Storage::kStrips:
        return "striped";
      case simpletiff::Storage::kSingleJpeg:
        return "single_jpeg";
      default:
        return "unknown";
    }
  }

  /// Check if page is tiled
  bool isTiled() const {
    return state_->index.Page(page_index_).storage ==
           simpletiff::Storage::kTiles;
  }

  /// Get tile width (only for tiled pages)
  uint16_t tileWidth() const {
    if (!isTiled()) {
      throw std::runtime_error("Page is not tiled");
    }
    const auto& page = state_->index.Page(page_index_);
    return state_->index.Tiles(page.payload_id).tile_w;
  }

  /// Get tile height (only for tiled pages)
  uint16_t tileHeight() const {
    if (!isTiled()) {
      throw std::runtime_error("Page is not tiled");
    }
    const auto& page = state_->index.Page(page_index_);
    return state_->index.Tiles(page.payload_id).tile_h;
  }

  /// Get number of tiles horizontally (only for tiled pages)
  uint32_t numTilesX() const {
    if (!isTiled()) {
      throw std::runtime_error("Page is not tiled");
    }
    const auto& page = state_->index.Page(page_index_);
    return state_->index.Tiles(page.payload_id).tiles_x;
  }

  /// Get number of tiles vertically (only for tiled pages)
  uint32_t numTilesY() const {
    if (!isTiled()) {
      throw std::runtime_error("Page is not tiled");
    }
    const auto& page = state_->index.Page(page_index_);
    return state_->index.Tiles(page.payload_id).tiles_y;
  }

  /// Read full page as typed array
  emscripten::val read() const {
    const auto& page = state_->index.Page(page_index_);
    simpletiff::Roi roi{0, 0, page.width, page.height};
    return readRegionInternal(roi);
  }

  /// Read region from page
  /// @param x X offset
  /// @param y Y offset
  /// @param width Region width
  /// @param height Region height
  emscripten::val readRegion(uint32_t x, uint32_t y, uint32_t width,
                             uint32_t height) const {
    simpletiff::Roi roi{x, y, width, height};
    return readRegionInternal(roi);
  }

  /// Read a single tile by index (tiled pages only)
  emscripten::val readTile(uint32_t tile_index) const {
    if (!isTiled()) {
      throw std::runtime_error("Page is not tiled");
    }

    std::vector<uint8_t> tile_data;
    int tile_w = 0, tile_h = 0;
    simpletiff::DecodeContext ctx;

    auto result = simpletiff::ReadTile(state_->index, page_index_, tile_index,
                                       ctx, tile_data, tile_w, tile_h);
    if (!result) {
      throw std::runtime_error("Failed to read tile: " +
                               result.error().message());
    }

    // Create typed array based on bits_per_sample
    return createTypedArray(tile_data, tile_h, tile_w, samplesPerPixel(),
                            bitsPerSample());
  }

 private:
  std::shared_ptr<ReaderState> state_;
  uint32_t page_index_;

  /// Internal method to read region
  emscripten::val readRegionInternal(const simpletiff::Roi& roi) const {
    const auto& page = state_->index.Page(page_index_);
    const uint32_t bps = page.bits_per_sample;
    const uint32_t spp = page.samples_per_pixel;
    const uint32_t bytes_per_sample = bps / 8;
    const uint32_t bytes_per_pixel = bytes_per_sample * spp;
    const size_t stride = roi.width * bytes_per_pixel;
    const size_t buffer_size = stride * roi.height;

    // Allocate buffer
    std::vector<uint8_t> buffer(buffer_size);

    // Read the region
    simpletiff::DecodeContext ctx;
    auto result = simpletiff::ReadPage(state_->index, page_index_, roi, ctx,
                                       buffer.data(), static_cast<int>(stride));
    if (!result) {
      throw std::runtime_error("Failed to read page region: " +
                               result.error().message());
    }

    // Create typed array based on bits_per_sample
    return createTypedArray(buffer, roi.height, roi.width, spp, bps);
  }

  /// Create typed array with appropriate type
  static emscripten::val createTypedArray(const std::vector<uint8_t>& buffer,
                                          uint32_t height, uint32_t width,
                                          uint16_t spp, uint16_t bps) {
    const size_t total_elements = static_cast<size_t>(height) * width * spp;

    if (bps == 8) {
      // uint8 - use typed_memory_view for safe zero-copy access
      auto view = emscripten::typed_memory_view(buffer.size(), buffer.data());
      // Create a new Uint8Array and copy the data
      emscripten::val array =
          emscripten::val::global("Uint8Array").new_(total_elements);
      array.call<void>("set", view);
      return array;
    } else if (bps == 16) {
      // uint16 - reinterpret buffer as uint16_t array
      const uint16_t* data_ptr =
          reinterpret_cast<const uint16_t*>(buffer.data());
      auto view = emscripten::typed_memory_view(total_elements, data_ptr);
      emscripten::val array =
          emscripten::val::global("Uint16Array").new_(total_elements);
      array.call<void>("set", view);
      return array;
    } else if (bps == 32) {
      // uint32 - reinterpret buffer as uint32_t array
      const uint32_t* data_ptr =
          reinterpret_cast<const uint32_t*>(buffer.data());
      auto view = emscripten::typed_memory_view(total_elements, data_ptr);
      emscripten::val array =
          emscripten::val::global("Uint32Array").new_(total_elements);
      array.call<void>("set", view);
      return array;
    } else {
      throw std::runtime_error("Unsupported bits_per_sample: " +
                               std::to_string(bps));
    }
  }
};

/// WASM wrapper for TIFF reader
class SimpleTiffReaderWrapper {
 public:
  /// Factory method: create reader from file path (for WORKERFS)
  static SimpleTiffReaderWrapper fromFilePath(const std::string& file_path) {
    auto state = std::make_shared<ReaderState>();
    state->file_path = file_path;
    state->is_temp_file = false;  // Not a temporary file, don't delete on exit

    // Try to open the file first to check if it's accessible
    FILE* test_fp = std::fopen(file_path.c_str(), "rb");
    if (test_fp == nullptr) {
      throw std::runtime_error("Failed to open file: " + file_path +
                               " (errno: " + std::to_string(errno) + ")");
    }

    // Check if we can seek
    if (std::fseek(test_fp, 0, SEEK_END) != 0) {
      std::fclose(test_fp);
      throw std::runtime_error("Failed to seek in file: " + file_path);
    }

    int64_t file_size = std::ftell(test_fp);
    if (file_size < 0) {
      std::fclose(test_fp);
      throw std::runtime_error("Failed to get file size: " + file_path);
    }

    // Reset file pointer
    std::fseek(test_fp, 0, SEEK_SET);
    std::fclose(test_fp);

    // Use OpenTiff which uses portable I/O
    bool success = simpletiff::OpenTiff(file_path, state->index, state->fd);

    if (!success) {
      throw std::runtime_error("Failed to parse TIFF file: " + file_path);
    }

    return SimpleTiffReaderWrapper(std::move(state));
  }

  /// Get number of pages (cast to uint32 to avoid BigInt in JavaScript)
  uint32_t numPages() const {
    return static_cast<uint32_t>(state_->index.NumPages());
  }

  /// Check if this is a BigTIFF
  bool isBigTiff() const { return state_->index.IsBigTiff(); }

  /// Get file size (return as double to avoid BigInt issues)
  double fileSize() const {
    return static_cast<double>(state_->index.FileSize());
  }

  /// Get page by index
  SimpleTiffPageWrapper getPage(size_t index) const {
    if (index >= state_->index.NumPages()) {
      throw std::out_of_range("Page index out of range");
    }
    return SimpleTiffPageWrapper(state_, static_cast<uint32_t>(index));
  }

 private:
  std::shared_ptr<ReaderState> state_;

  explicit SimpleTiffReaderWrapper(std::shared_ptr<ReaderState> state)
      : state_(std::move(state)) {}
};

}  // namespace simpletiff::wasm

// Emscripten bindings
EMSCRIPTEN_BINDINGS(simpletiff) {
  emscripten::class_<simpletiff::wasm::SimpleTiffReaderWrapper>(
      "SimpleTiffReader")
      .class_function("fromFilePath",
                      &simpletiff::wasm::SimpleTiffReaderWrapper::fromFilePath)
      .property("numPages",
                &simpletiff::wasm::SimpleTiffReaderWrapper::numPages)
      .property("isBigTiff",
                &simpletiff::wasm::SimpleTiffReaderWrapper::isBigTiff)
      .property("fileSize",
                &simpletiff::wasm::SimpleTiffReaderWrapper::fileSize)
      .function("getPage", &simpletiff::wasm::SimpleTiffReaderWrapper::getPage);

  emscripten::class_<simpletiff::wasm::SimpleTiffPageWrapper>("SimpleTiffPage")
      .property("width", &simpletiff::wasm::SimpleTiffPageWrapper::width)
      .property("height", &simpletiff::wasm::SimpleTiffPageWrapper::height)
      .property("samplesPerPixel",
                &simpletiff::wasm::SimpleTiffPageWrapper::samplesPerPixel)
      .property("bitsPerSample",
                &simpletiff::wasm::SimpleTiffPageWrapper::bitsPerSample)
      .property("photometric",
                &simpletiff::wasm::SimpleTiffPageWrapper::photometric)
      .property("compression",
                &simpletiff::wasm::SimpleTiffPageWrapper::compression)
      .property("storageType",
                &simpletiff::wasm::SimpleTiffPageWrapper::storageType)
      .property("isTiled", &simpletiff::wasm::SimpleTiffPageWrapper::isTiled)
      .property("tileWidth",
                &simpletiff::wasm::SimpleTiffPageWrapper::tileWidth)
      .property("tileHeight",
                &simpletiff::wasm::SimpleTiffPageWrapper::tileHeight)
      .property("numTilesX",
                &simpletiff::wasm::SimpleTiffPageWrapper::numTilesX)
      .property("numTilesY",
                &simpletiff::wasm::SimpleTiffPageWrapper::numTilesY)
      .function("read", &simpletiff::wasm::SimpleTiffPageWrapper::read)
      .function("readRegion",
                &simpletiff::wasm::SimpleTiffPageWrapper::readRegion)
      .function("readTile", &simpletiff::wasm::SimpleTiffPageWrapper::readTile);
}
