(function (global, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory(require("./image_pipeline.js"));
  } else {
    global.PaperFrameQuantizer = factory(global.PaperFrameImage);
  }
})(typeof globalThis === "object" ? globalThis : this, function (imagePipeline) {
  "use strict";

  if (!imagePipeline) {
    throw new Error("PaperFrameImage must be loaded before PaperFrameQuantizer");
  }

  const MODES = Object.freeze(["nearest", "floyd-steinberg", "atkinson", "bayer-4x4"]);
  const PALETTE = Object.freeze([
    Object.freeze({ name: "black", code: 0x0, rgb: Object.freeze([0, 0, 0]) }),
    Object.freeze({ name: "white", code: 0x1, rgb: Object.freeze([255, 255, 255]) }),
    Object.freeze({ name: "yellow", code: 0x2, rgb: Object.freeze([255, 255, 0]) }),
    Object.freeze({ name: "red", code: 0x3, rgb: Object.freeze([255, 0, 0]) }),
    Object.freeze({ name: "blue", code: 0x5, rgb: Object.freeze([0, 0, 255]) }),
    Object.freeze({ name: "green", code: 0x6, rgb: Object.freeze([0, 255, 0]) }),
  ]);
  const BAYER_4X4 = Object.freeze([
    0, 8, 2, 10,
    12, 4, 14, 6,
    3, 11, 1, 9,
    15, 7, 13, 5,
  ]);

  function nearestPalette(valueR, valueG, valueB) {
    let bestIndex = 0;
    let bestDistance = Number.POSITIVE_INFINITY;
    for (let index = 0; index < PALETTE.length; index += 1) {
      const color = PALETTE[index].rgb;
      const distance = ((valueR - color[0]) ** 2) +
        ((valueG - color[1]) ** 2) + ((valueB - color[2]) ** 2);
      if (distance < bestDistance) {
        bestDistance = distance;
        bestIndex = index;
      }
    }
    return bestIndex;
  }

  function clamp(value) {
    return Math.max(0, Math.min(255, value));
  }

  function validateMode(mode) {
    if (!MODES.includes(mode)) {
      throw new RangeError(`mode must be one of: ${MODES.join(", ")}`);
    }
  }

  function createResult(width, height, codes) {
    const data = new Uint8ClampedArray(width * height * 4);
    for (let index = 0; index < codes.length; index += 1) {
      const color = PALETTE[codes[index]].rgb;
      const output = index * 4;
      data[output] = color[0];
      data[output + 1] = color[1];
      data[output + 2] = color[2];
      data[output + 3] = 255;
    }
    return Object.freeze({
      width,
      height,
      data,
      codes,
      palette: PALETTE,
    });
  }

  function quantizeNearest(raster) {
    const source = imagePipeline.flattenOnWhite(raster);
    const codes = new Uint8Array(source.width * source.height);
    for (let index = 0; index < codes.length; index += 1) {
      const sourceIndex = index * 4;
      codes[index] = nearestPalette(
        source.data[sourceIndex],
        source.data[sourceIndex + 1],
        source.data[sourceIndex + 2],
      );
    }
    return createResult(source.width, source.height, codes);
  }

  function addError(work, width, height, x, y, red, green, blue, factor) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return;
    }
    const index = ((y * width) + x) * 3;
    work[index] += red * factor;
    work[index + 1] += green * factor;
    work[index + 2] += blue * factor;
  }

  function quantizeErrorDiffusion(raster, mode) {
    const source = imagePipeline.flattenOnWhite(raster);
    const width = source.width;
    const height = source.height;
    const codes = new Uint8Array(width * height);
    const work = new Float32Array(width * height * 3);
    for (let index = 0; index < codes.length; index += 1) {
      const sourceIndex = index * 4;
      const workIndex = index * 3;
      work[workIndex] = source.data[sourceIndex];
      work[workIndex + 1] = source.data[sourceIndex + 1];
      work[workIndex + 2] = source.data[sourceIndex + 2];
    }

    for (let y = 0; y < height; y += 1) {
      for (let x = 0; x < width; x += 1) {
        const workIndex = ((y * width) + x) * 3;
        const red = clamp(work[workIndex]);
        const green = clamp(work[workIndex + 1]);
        const blue = clamp(work[workIndex + 2]);
        const paletteIndex = nearestPalette(red, green, blue);
        codes[(y * width) + x] = paletteIndex;
        const color = PALETTE[paletteIndex].rgb;
        const errorR = red - color[0];
        const errorG = green - color[1];
        const errorB = blue - color[2];
        if (mode === "floyd-steinberg") {
          addError(work, width, height, x + 1, y, errorR, errorG, errorB, 7 / 16);
          addError(work, width, height, x - 1, y + 1, errorR, errorG, errorB, 3 / 16);
          addError(work, width, height, x, y + 1, errorR, errorG, errorB, 5 / 16);
          addError(work, width, height, x + 1, y + 1, errorR, errorG, errorB, 1 / 16);
        } else {
          addError(work, width, height, x + 1, y, errorR, errorG, errorB, 1 / 8);
          addError(work, width, height, x + 2, y, errorR, errorG, errorB, 1 / 8);
          addError(work, width, height, x - 1, y + 1, errorR, errorG, errorB, 1 / 8);
          addError(work, width, height, x, y + 1, errorR, errorG, errorB, 1 / 8);
          addError(work, width, height, x + 1, y + 1, errorR, errorG, errorB, 1 / 8);
          addError(work, width, height, x, y + 2, errorR, errorG, errorB, 1 / 8);
        }
      }
    }
    return createResult(width, height, codes);
  }

  function quantizeBayer(raster) {
    const source = imagePipeline.flattenOnWhite(raster);
    const codes = new Uint8Array(source.width * source.height);
    for (let y = 0; y < source.height; y += 1) {
      for (let x = 0; x < source.width; x += 1) {
        const index = (y * source.width) + x;
        const sourceIndex = index * 4;
        const threshold = (BAYER_4X4[((y & 3) * 4) + (x & 3)] + 0.5) / 16 - 0.5;
        const offset = threshold * 64;
        codes[index] = nearestPalette(
          clamp(source.data[sourceIndex] + offset),
          clamp(source.data[sourceIndex + 1] + offset),
          clamp(source.data[sourceIndex + 2] + offset),
        );
      }
    }
    return createResult(source.width, source.height, codes);
  }

  function quantize(raster, mode) {
    const selectedMode = mode || "nearest";
    validateMode(selectedMode);
    if (selectedMode === "nearest") {
      return quantizeNearest(raster);
    }
    if (selectedMode === "bayer-4x4") {
      return quantizeBayer(raster);
    }
    return quantizeErrorDiffusion(raster, selectedMode);
  }

  return Object.freeze({
    MODES,
    PALETTE,
    BAYER_4X4,
    nearestPalette,
    quantize,
  });
});
