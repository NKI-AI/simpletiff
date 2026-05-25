// Copyright 2025 SimpleTIFF Authors
//
// TypeScript type definitions for SimpleTIFF WASM bindings

/**
 * Represents a single page in a TIFF file
 */
export interface SimpleTiffPage {
  /**
   * Page width in pixels
   */
  readonly width: number;

  /**
   * Page height in pixels
   */
  readonly height: number;

  /**
   * Number of samples per pixel (channels)
   * e.g., 1 for grayscale, 3 for RGB, 4 for RGBA
   */
  readonly samplesPerPixel: number;

  /**
   * Number of bits per sample (8, 16, or 32)
   */
  readonly bitsPerSample: number;

  /**
   * Photometric interpretation
   * 0 = WhiteIsZero
   * 1 = BlackIsZero
   * 2 = RGB
   * 3 = Palette
   * 6 = YCbCr
   */
  readonly photometric: number;

  /**
   * Compression type code
   * 1 = None (uncompressed)
   * 5 = LZW
   * 7 = JPEG
   * 8 = Deflate (ZIP)
   * 50000 = ZSTD
   */
  readonly compression: number;

  /**
   * Storage type: "tiled", "striped", "single_jpeg", or "unknown"
   */
  readonly storageType: string;

  /**
   * True if page uses tiled storage
   */
  readonly isTiled: boolean;

  /**
   * Tile width in pixels (only for tiled pages)
   * @throws Error if page is not tiled
   */
  readonly tileWidth: number;

  /**
   * Tile height in pixels (only for tiled pages)
   * @throws Error if page is not tiled
   */
  readonly tileHeight: number;

  /**
   * Number of tiles horizontally (only for tiled pages)
   * @throws Error if page is not tiled
   */
  readonly numTilesX: number;

  /**
   * Number of tiles vertically (only for tiled pages)
   * @throws Error if page is not tiled
   */
  readonly numTilesY: number;

  /**
   * Read the full page as a typed array
   *
   * @returns Typed array with shape [height, width, channels]
   *          - Uint8Array for 8-bit data
   *          - Uint16Array for 16-bit data
   *          - Uint32Array for 32-bit data
   * @throws Error if reading fails
   */
  read(): Uint8Array | Uint16Array | Uint32Array;

  /**
   * Read a region from the page
   *
   * @param x X offset (left edge)
   * @param y Y offset (top edge)
   * @param width Region width in pixels
   * @param height Region height in pixels
   * @returns Typed array with shape [height, width, channels]
   *          - Uint8Array for 8-bit data
   *          - Uint16Array for 16-bit data
   *          - Uint32Array for 32-bit data
   * @throws Error if reading fails or region is out of bounds
   */
  readRegion(
    x: number,
    y: number,
    width: number,
    height: number
  ): Uint8Array | Uint16Array | Uint32Array;

  /**
   * Read a single tile by index (tiled pages only)
   *
   * Tiles are indexed in row-major order (left-to-right, top-to-bottom).
   *
   * @param index Linear tile index (0-based)
   * @returns Typed array with shape [tile_height, tile_width, channels]
   *          - Uint8Array for 8-bit data
   *          - Uint16Array for 16-bit data
   *          - Uint32Array for 32-bit data
   * @throws Error if page is not tiled or if reading fails
   */
  readTile(index: number): Uint8Array | Uint16Array | Uint32Array;

  /**
   * Delete the page object and free memory
   */
  delete(): void;
}

/**
 * High-performance TIFF reader with support for multi-page files
 */
export interface SimpleTiffReader {
  /**
   * Number of pages in the TIFF file
   */
  readonly numPages: number;

  /**
   * True if this is a BigTIFF file (supports files > 4GB)
   */
  readonly isBigTiff: boolean;

  /**
   * File size in bytes
   */
  readonly fileSize: number;

  /**
   * Get a page by index
   *
   * @param index Page index (0-based)
   * @returns SimpleTiffPage object
   * @throws Error if index is out of range
   */
  getPage(index: number): SimpleTiffPage;

  /**
   * Delete the reader object and free memory
   */
  delete(): void;
}

/**
 * SimpleTiffReader constructor interface
 */
export interface SimpleTiffReaderConstructor {
  /**
   * Create a SimpleTiffReader from file path (WORKERFS)
   *
   * **Recommended for large files** - enables lazy loading via WORKERFS.
   * Only reads headers during parsing (~1-5MB), then loads tiles on-demand.
   *
   * @param filePath Path to file in WORKERFS (e.g., "/work/slide.svs")
   * @returns SimpleTiffReader instance
   * @throws Error if file cannot be opened or parsed
   */
  fromFilePath(filePath: string): SimpleTiffReader;

  /**
   * Create a SimpleTiffReader from file data
   *
   * **WARNING:** Loads entire file into memory. Use `fromFilePath` with
   * WORKERFS for large files to enable lazy loading.
   *
   * @param data File data as Uint8Array
   * @returns SimpleTiffReader instance
   * @throws Error if file cannot be parsed or is not a valid TIFF
   */
  fromFileData(data: Uint8Array): SimpleTiffReader;
}

/**
 * SimpleTIFF WASM module interface
 */
export interface SimpleTiffModule {
  /**
   * SimpleTiffReader class constructor
   */
  SimpleTiffReader: SimpleTiffReaderConstructor;
}

/**
 * Create and initialize the SimpleTIFF WASM module
 *
 * @returns Promise that resolves to the initialized module
 *
 * @example
 * ```typescript
 * import createSimpleTiffModule from './simpletiff_multiplex.js';
 *
 * const module = await createSimpleTiffModule();
 *
 * // Load a TIFF file
 * const response = await fetch('image.tiff');
 * const arrayBuffer = await response.arrayBuffer();
 * const data = new Uint8Array(arrayBuffer);
 *
 * // Create reader
 * const reader = module.SimpleTiffReader.fromFileData(data);
 *
 * // Access metadata
 * console.log(`Pages: ${reader.numPages}`);
 * console.log(`BigTIFF: ${reader.isBigTiff}`);
 *
 * // Read first page
 * const page = reader.getPage(0);
 * console.log(`Size: ${page.width}x${page.height}`);
 * console.log(`Channels: ${page.samplesPerPixel}`);
 * console.log(`Bit depth: ${page.bitsPerSample}`);
 *
 * // Read image data
 * const imageData = page.read();
 *
 * // Clean up
 * page.delete();
 * reader.delete();
 * ```
 */
declare function createSimpleTiffModule(): Promise<SimpleTiffModule>;

export default createSimpleTiffModule;
