# SimpleTIFF WASM Bindings

High-performance WebAssembly bindings for SimpleTIFF with **lazy loading** support for large whole-slide images.

## Features

- **🚀 Lazy Loading**: Only reads what you need via WORKERFS - no full file in memory!
- **🧵 Web Worker Architecture**: Non-blocking UI, true parallelism
- **🎯 C++ API**: Uses Emscripten's embind (not C-style exports)
- **📘 TypeScript Support**: Complete type definitions included
- **🗜️ All Compressions**: JPEG, LZW, ZSTD, Deflate built-in
- **🔲 Tiled & Striped**: Full support for both storage formats
- **📊 BigTIFF**: Handles files > 4GB

## Architecture

```
┌─────────────────┐         ┌──────────────────────┐
│   Main Thread   │         │    Web Worker        │
│                 │         │                      │
│  • File Picker  ├────────►│  • WASM Module       │
│  • Canvas       │  File   │  • WORKERFS          │
│  • UI Controls  │◄────────┤  • Lazy Reading      │
│                 │  Tiles  │                      │
└─────────────────┘         └──────────┬───────────┘
                                       │
                                       │ File.slice()
                                       ▼
                                ┌──────────────┐
                                │   SVS File   │
                                │   (5 GB)     │
                                └──────────────┘
                         Only ~1-5MB loaded initially!
                         Tiles loaded on-demand
```

## How Lazy Loading Works

1. **Parse** (fromFilePath): Reads only TIFF headers/IFDs (~1-5MB)
2. **On-Demand**: Tiles loaded only when `readTile()` is called
3. **WORKERFS**: Translates C++ `read()` calls to JavaScript `File.slice()`
4. **Zero Copy**: Direct memory mapping, no unnecessary copies

## Building

```bash
# From repository root
bazelisk build //aifo/simpletiff/wasm:wasm --platforms=//platforms:wasm32
```

Output files:

- `bazel-bin/aifo/simpletiff/wasm/simpletiff_multiplex_wasm/simpletiff_multiplex.wasm`
- `bazel-bin/aifo/simpletiff/wasm/simpletiff_multiplex_wasm/simpletiff_multiplex.js`

## Installation

Copy to your web project:

```bash
cp bazel-bin/aifo/simpletiff/wasm/simpletiff_multiplex_wasm/simpletiff_multiplex.{js,wasm} /your/project/public/
```

## Usage

### Web Worker (Recommended for Large Files)

```javascript
// simpletiff-worker.js
importScripts('simpletiff_multiplex.js');

let module = null;
let reader = null;

createSimpleTiffModule().then(mod => {
    module = mod;
    self.postMessage({ type: 'ready' });
});

self.onmessage = async function(e) {
    const { type, data } = e.data;

    if (type === 'loadFile') {
        // Mount file using WORKERFS (lazy loading!)
        FS.mkdir('/work');
        FS.mount(WORKERFS, { files: [data.file] }, '/work');

        // Parse TIFF - only reads headers (~1-5MB)
        reader = module.SimpleTiffReader.fromFilePath('/work/' + data.file.name);

        self.postMessage({
            type: 'fileLoaded',
            data: {
                numPages: reader.numPages,
                isBigTiff: reader.isBigTiff,
                fileSize: reader.fileSize
            }
        });
    }

    if (type === 'readTile') {
        // Only loads this specific tile from file!
        const page = reader.getPage(data.pageIndex);
        const tileData = page.readTile(data.tileIndex);

        // Transfer back (zero-copy)
        self.postMessage({
            type: 'imageData',
            data: { imageData: tileData, ...  }
        }, [tileData.buffer]);

        page.delete();
    }
};
```

```javascript
// main.js
const worker = new Worker("simpletiff-worker.js");

worker.onmessage = (e) => {
  if (e.data.type === "imageData") {
    renderToCanvas(e.data.data.imageData);
  }
};

// Pick file with File System Access API
const [fileHandle] = await window.showOpenFilePicker({
  types: [
    {
      description: "TIFF",
      accept: { "image/tiff": [".tiff", ".tif", ".svs"] },
    },
  ],
});

const file = await fileHandle.getFile();
worker.postMessage({ type: "loadFile", data: { file } });
```

### Direct API (Small Files Only)

```javascript
import createSimpleTiffModule from "./simpletiff_multiplex.js";

const module = await createSimpleTiffModule();

// ⚠️ WARNING: Loads entire file into memory!
const response = await fetch("small.tiff");
const data = new Uint8Array(await response.arrayBuffer());

const reader = module.SimpleTiffReader.fromFileData(data);
try {
  const page = reader.getPage(0);
  const imageData = page.read();
  page.delete();
} finally {
  reader.delete();
}
```

## API Reference

### SimpleTiffReader

```typescript
interface SimpleTiffReader {
  readonly numPages: number; // uint32, not BigInt
  readonly isBigTiff: boolean;
  readonly fileSize: number; // double, for values > 2^53

  getPage(index: number): SimpleTiffPage;
  delete(): void;
}
```

**Factory Methods:**

- `fromFilePath(path: string)`: **Recommended** - lazy loading via WORKERFS
- `fromFileData(data: Uint8Array)`: Loads entire file into memory

### SimpleTiffPage

```typescript
interface SimpleTiffPage {
  // Dimensions
  readonly width: number;
  readonly height: number;
  readonly samplesPerPixel: number; // 1=grayscale, 3=RGB, 4=RGBA
  readonly bitsPerSample: number; // 8, 16, or 32

  // Format
  readonly compression: number; // 1=None, 5=LZW, 7=JPEG, 8=Deflate, 50000=ZSTD
  readonly photometric: number;
  readonly storageType: string; // "tiled", "striped", "single_jpeg"
  readonly isTiled: boolean;

  // Tiling (if isTiled)
  readonly tileWidth: number;
  readonly tileHeight: number;
  readonly numTilesX: number;
  readonly numTilesY: number;

  // Reading
  read(): Uint8Array | Uint16Array | Uint32Array;
  readRegion(
    x: number,
    y: number,
    width: number,
    height: number
  ): Uint8Array | Uint16Array | Uint32Array;
  readTile(index: number): Uint8Array | Uint16Array | Uint32Array;

  delete(): void;
}
```

## Demo

Run the interactive demo:

```bash
cd aifo/simpletiff/wasm/example
python3 -m http.server 8000
```

Open http://localhost:8000 and test with your TIFF files!

## Memory Usage Comparison

| Method                    | 5GB SVS File                      | 100MB TIFF                  |
| ------------------------- | --------------------------------- | --------------------------- |
| `fromFileData`            | ❌ Crashes (OOM)                  | ⚠️ 100MB in memory          |
| `fromFilePath` + WORKERFS | ✅ ~5MB headers + tiles on-demand | ✅ ~1MB + regions on-demand |

## Performance

For a typical whole-slide image (5GB SVS, 100K tiles):

- **Parse time**: ~100-500ms (only headers)
- **First tile**: ~10-50ms (JPEG decode)
- **Subsequent tiles**: ~5-20ms (cached decompressor)
- **Memory**: ~5MB base + active tiles only

## Browser Requirements

- **Chrome/Edge 86+**: Full support
- **Opera 72+**: Full support
- **Firefox**: File System API behind flag
- **Safari**: Not yet supported

Must use **HTTPS** or **localhost**.

## Troubleshooting

### "WORKERFS is not defined"

Add to BUILD.bazel linkopts:

```python
"-sWORKERFS=1",
```

### Worker fails to load

- Ensure worker file is in same directory as HTML
- Check console for CORS errors
- Verify `.wasm` file is also in same directory

### Still loads full file

Make sure you're using:

- ✅ `fromFilePath()` in worker (lazy)
- ❌ NOT `fromFileData()` (eager)

## Development

### Update After Code Changes

```bash
# Rebuild
bazelisk build //aifo/simpletiff/wasm:wasm --platforms=//platforms:wasm32

# Copy to example
cp bazel-bin/aifo/simpletiff/wasm/simpletiff_multiplex_wasm/simpletiff_multiplex.{js,wasm} \
   aifo/simpletiff/wasm/example/
```

### Test

```bash
cd aifo/simpletiff/wasm/example
python3 -m http.server 8000
```

## License

Copyright 2025 SimpleTIFF Authors. Apache 2.0 License.
