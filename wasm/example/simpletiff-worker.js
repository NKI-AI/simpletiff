// SimpleTIFF Web Worker
// Handles TIFF processing in background thread with lazy file loading

// Import the WASM module (ES6 module syntax for worker)
import createSimpleTiffModule from "./simpletiff_multiplex.js";

let module = null;
let FS = null; // Filesystem API from Emscripten
let reader = null;
let currentFile = null;

// Initialize WASM module
createSimpleTiffModule()
  .then((mod) => {
    module = mod;

    FS = mod.FS; // Get FS from the module

    // Debug: Check if WORKERFS is available
    console.log("Available filesystems:", Object.keys(FS.filesystems));

    if (!FS) {
      throw new Error("FS is not exported from WASM module");
    }

    if (!FS.filesystems.WORKERFS) {
      throw new Error(
        "WORKERFS not available. Available filesystems: " +
          Object.keys(FS.filesystems).join(", ")
      );
    }

    self.postMessage({ type: "ready" });
  })
  .catch((err) => {
    self.postMessage({
      type: "error",
      message: "Failed to load WASM module: " + err.message,
    });
  });

self.onmessage = async function (e) {
  const { type, data } = e.data;

  try {
    switch (type) {
      case "loadFile":
        await handleLoadFile(data);
        break;

      case "getPage":
        handleGetPage(data);
        break;

      case "readFull":
        handleReadFull(data);
        break;

      case "readRegion":
        handleReadRegion(data);
        break;

      case "readTile":
        handleReadTile(data);
        break;

      case "close":
        handleClose();
        break;

      default:
        throw new Error("Unknown message type: " + type);
    }
  } catch (err) {
    self.postMessage({
      type: "error",
      message: err.message,
      stack: err.stack,
    });
  }
};

async function handleLoadFile(data) {
  const { file } = data;

  if (!module) {
    throw new Error("WASM module not initialized");
  }

  // Clean up previous file
  if (reader) {
    reader.delete();
    reader = null;
  }

  currentFile = file;

  // Mount file using WORKERFS (lazy loading!)
  const mountPoint = "/work";
  try {
    // Unmount if already mounted
    try {
      FS.unmount(mountPoint);
    } catch (e) {
      console.log("Unmount warning (expected first time):", e.message || e);
    }

    // Create mount point
    try {
      FS.mkdir(mountPoint);
    } catch (e) {
      console.log("Mkdir warning (expected if exists):", e.message || e);
    }

    console.log("About to mount file:", file.name, "size:", file.size);

    // Mount the file with WORKERFS (enables lazy loading)
    // WORKERFS is a filesystem type, accessed via FS.filesystems
    FS.mount(
      FS.filesystems.WORKERFS,
      {
        files: [file],
      },
      mountPoint
    );

    console.log("File mounted successfully");

    const filePath = `${mountPoint}/${file.name}`;
    console.log("Opening TIFF at:", filePath);

    // Parse TIFF using WORKERFS (lazy loading - only reads headers!)
    // The file stays as a File object, WORKERFS translates FS calls to File.slice()
    const startTime = performance.now();

    reader = module.SimpleTiffReader.fromFilePath(filePath);
    const parseTime = performance.now() - startTime;

    console.log("TIFF parsed successfully in", parseTime.toFixed(2), "ms");

    self.postMessage({
      type: "fileLoaded",
      data: {
        numPages: reader.numPages,
        isBigTiff: reader.isBigTiff,
        fileSize: reader.fileSize,
        parseTime: parseTime,
      },
    });
  } catch (err) {
    console.error("Error in handleLoadFile:", err);
    throw new Error(
      "Failed to mount/parse file: " +
        (err.message || err.toString() || "Unknown error")
    );
  }
}

function handleGetPage(data) {
  const { pageIndex } = data;

  if (!reader) {
    throw new Error("No file loaded");
  }

  const page = reader.getPage(pageIndex);

  const pageInfo = {
    width: page.width,
    height: page.height,
    samplesPerPixel: page.samplesPerPixel,
    bitsPerSample: page.bitsPerSample,
    compression: page.compression,
    photometric: page.photometric,
    storageType: page.storageType,
    isTiled: page.isTiled,
  };

  if (page.isTiled) {
    pageInfo.tileWidth = page.tileWidth;
    pageInfo.tileHeight = page.tileHeight;
    pageInfo.numTilesX = page.numTilesX;
    pageInfo.numTilesY = page.numTilesY;
  }

  // Don't delete the page yet - just send info
  page.delete();

  self.postMessage({
    type: "pageInfo",
    data: pageInfo,
  });
}

function handleReadFull(data) {
  const { pageIndex } = data;

  if (!reader) {
    throw new Error("No file loaded");
  }

  const startTime = performance.now();
  const page = reader.getPage(pageIndex);

  try {
    const imageData = page.read();
    const readTime = performance.now() - startTime;

    // Transfer the buffer (zero-copy)
    self.postMessage(
      {
        type: "imageData",
        data: {
          imageData: imageData,
          width: page.width,
          height: page.height,
          channels: page.samplesPerPixel,
          bitsPerSample: page.bitsPerSample,
          readTime: readTime,
        },
      },
      [imageData.buffer]
    );
  } finally {
    page.delete();
  }
}

function handleReadRegion(data) {
  const { pageIndex, x, y, width, height } = data;

  if (!reader) {
    throw new Error("No file loaded");
  }

  const startTime = performance.now();
  const page = reader.getPage(pageIndex);

  try {
    const imageData = page.readRegion(x, y, width, height);
    const readTime = performance.now() - startTime;

    // Transfer the buffer (zero-copy)
    self.postMessage(
      {
        type: "imageData",
        data: {
          imageData: imageData,
          width: width,
          height: height,
          channels: page.samplesPerPixel,
          bitsPerSample: page.bitsPerSample,
          readTime: readTime,
        },
      },
      [imageData.buffer]
    );
  } finally {
    page.delete();
  }
}

function handleReadTile(data) {
  const { pageIndex, tileIndex } = data;

  if (!reader) {
    throw new Error("No file loaded");
  }

  const startTime = performance.now();
  const page = reader.getPage(pageIndex);

  try {
    if (!page.isTiled) {
      throw new Error("Page is not tiled");
    }

    const imageData = page.readTile(tileIndex);
    const readTime = performance.now() - startTime;

    // Transfer the buffer (zero-copy)
    self.postMessage(
      {
        type: "imageData",
        data: {
          imageData: imageData,
          width: page.tileWidth,
          height: page.tileHeight,
          channels: page.samplesPerPixel,
          bitsPerSample: page.bitsPerSample,
          readTime: readTime,
        },
      },
      [imageData.buffer]
    );
  } finally {
    page.delete();
  }
}

function handleClose() {
  if (reader) {
    reader.delete();
    reader = null;
  }

  // Unmount filesystem
  try {
    FS.unmount("/work");
  } catch (e) {
    // Ignore errors
  }

  currentFile = null;

  self.postMessage({ type: "closed" });
}
