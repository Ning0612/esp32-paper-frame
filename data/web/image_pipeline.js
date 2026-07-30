(function (global, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory();
  } else {
    global.PaperFrameImage = factory();
  }
})(typeof globalThis === "object" ? globalThis : this, function () {
  "use strict";

  const FIT_MODES = Object.freeze(["contain", "cover", "fill", "crop"]);
  const ORIENTATION_PROFILES = Object.freeze({
    landscape: Object.freeze({ orientation: 0, width: 800, height: 440 }),
    portrait: Object.freeze({ orientation: 1, width: 480, height: 760 }),
  });

  function assertInteger(value, name) {
    if (!Number.isInteger(value) || value <= 0) {
      throw new RangeError(`${name} must be a positive integer`);
    }
  }

  function cloneBytes(data) {
    if (data instanceof Uint8ClampedArray) {
      return new Uint8ClampedArray(data);
    }
    return new Uint8ClampedArray(data);
  }

  function makeRaster(width, height, data) {
    assertInteger(width, "width");
    assertInteger(height, "height");
    const expected = width * height * 4;
    const pixels = data == null ? new Uint8ClampedArray(expected) : cloneBytes(data);
    if (pixels.length !== expected) {
      throw new RangeError("RGBA data length does not match raster dimensions");
    }
    return { width, height, data: pixels };
  }

  function copyPixel(source, sourceIndex, destination, destinationIndex) {
    destination[destinationIndex] = source[sourceIndex];
    destination[destinationIndex + 1] = source[sourceIndex + 1];
    destination[destinationIndex + 2] = source[sourceIndex + 2];
    destination[destinationIndex + 3] = source[sourceIndex + 3];
  }

  function flattenOnWhite(raster, background) {
    const source = makeRaster(raster.width, raster.height, raster.data);
    const color = background || [255, 255, 255];
    if (!Array.isArray(color) || color.length !== 3 || color.some((value) =>
      !Number.isInteger(value) || value < 0 || value > 255)) {
      throw new RangeError("background must contain three byte values");
    }
    const output = new Uint8ClampedArray(source.data.length);
    for (let index = 0; index < source.data.length; index += 4) {
      const alpha = source.data[index + 3] / 255;
      output[index] = Math.round((source.data[index] * alpha) + (color[0] * (1 - alpha)));
      output[index + 1] = Math.round((source.data[index + 1] * alpha) + (color[1] * (1 - alpha)));
      output[index + 2] = Math.round((source.data[index + 2] * alpha) + (color[2] * (1 - alpha)));
      output[index + 3] = 255;
    }
    return makeRaster(source.width, source.height, output);
  }

  function orientExif(raster, orientation) {
    const source = makeRaster(raster.width, raster.height, raster.data);
    if (!Number.isInteger(orientation) || orientation < 1 || orientation > 8) {
      throw new RangeError("EXIF orientation must be an integer from 1 to 8");
    }
    if (orientation === 1) {
      return source;
    }
    const swapsAxes = orientation >= 5 && orientation <= 8;
    const width = swapsAxes ? source.height : source.width;
    const height = swapsAxes ? source.width : source.height;
    const output = new Uint8ClampedArray(width * height * 4);
    for (let y = 0; y < height; y += 1) {
      for (let x = 0; x < width; x += 1) {
        let sourceX = x;
        let sourceY = y;
        switch (orientation) {
          case 2:
            sourceX = source.width - 1 - x;
            break;
          case 3:
            sourceX = source.width - 1 - x;
            sourceY = source.height - 1 - y;
            break;
          case 4:
            sourceY = source.height - 1 - y;
            break;
          case 5:
            sourceX = y;
            sourceY = x;
            break;
          case 6:
            sourceX = y;
            sourceY = source.height - 1 - x;
            break;
          case 7:
            sourceX = source.width - 1 - y;
            sourceY = source.height - 1 - x;
            break;
          case 8:
            sourceX = source.width - 1 - y;
            sourceY = x;
            break;
          default:
            throw new RangeError("unsupported EXIF orientation");
        }
        const sourceIndex = ((sourceY * source.width) + sourceX) * 4;
        const destinationIndex = ((y * width) + x) * 4;
        copyPixel(source.data, sourceIndex, output, destinationIndex);
      }
    }
    return makeRaster(width, height, output);
  }

  function mirror(raster, mirrorX, mirrorY) {
    const source = makeRaster(raster.width, raster.height, raster.data);
    if (!mirrorX && !mirrorY) {
      return source;
    }
    const output = new Uint8ClampedArray(source.data.length);
    for (let y = 0; y < source.height; y += 1) {
      for (let x = 0; x < source.width; x += 1) {
        const sourceX = mirrorX ? source.width - 1 - x : x;
        const sourceY = mirrorY ? source.height - 1 - y : y;
        const sourceIndex = ((sourceY * source.width) + sourceX) * 4;
        const destinationIndex = ((y * source.width) + x) * 4;
        copyPixel(source.data, sourceIndex, output, destinationIndex);
      }
    }
    return makeRaster(source.width, source.height, output);
  }

  function rotate90Cw(raster) {
    const source = makeRaster(raster.width, raster.height, raster.data);
    const width = source.height;
    const height = source.width;
    const output = new Uint8ClampedArray(width * height * 4);
    for (let y = 0; y < height; y += 1) {
      for (let x = 0; x < width; x += 1) {
        const sourceX = y;
        const sourceY = source.height - 1 - x;
        const sourceIndex = ((sourceY * source.width) + sourceX) * 4;
        const destinationIndex = ((y * width) + x) * 4;
        copyPixel(source.data, sourceIndex, output, destinationIndex);
      }
    }
    return makeRaster(width, height, output);
  }

  function cropCenter(raster, width, height) {
    const source = makeRaster(raster.width, raster.height, raster.data);
    assertInteger(width, "crop width");
    assertInteger(height, "crop height");
    if (width > source.width || height > source.height) {
      throw new RangeError("crop cannot exceed source dimensions");
    }
    const left = Math.floor((source.width - width) / 2);
    const top = Math.floor((source.height - height) / 2);
    const output = new Uint8ClampedArray(width * height * 4);
    for (let y = 0; y < height; y += 1) {
      const sourceStart = (((top + y) * source.width) + left) * 4;
      const destinationStart = y * width * 4;
      output.set(source.data.subarray(sourceStart, sourceStart + (width * 4)), destinationStart);
    }
    return makeRaster(width, height, output);
  }

  function resizeNearest(raster, width, height) {
    const source = makeRaster(raster.width, raster.height, raster.data);
    assertInteger(width, "resize width");
    assertInteger(height, "resize height");
    if (source.width === width && source.height === height) {
      return source;
    }
    const output = new Uint8ClampedArray(width * height * 4);
    for (let y = 0; y < height; y += 1) {
      const sourceY = Math.min(source.height - 1, Math.floor((y * source.height) / height));
      for (let x = 0; x < width; x += 1) {
        const sourceX = Math.min(source.width - 1, Math.floor((x * source.width) / width));
        const sourceIndex = ((sourceY * source.width) + sourceX) * 4;
        const destinationIndex = ((y * width) + x) * 4;
        copyPixel(source.data, sourceIndex, output, destinationIndex);
      }
    }
    return makeRaster(width, height, output);
  }

  function paintContain(raster, width, height, background) {
    const scale = Math.min(width / raster.width, height / raster.height);
    const scaledWidth = Math.max(1, Math.round(raster.width * scale));
    const scaledHeight = Math.max(1, Math.round(raster.height * scale));
    const scaled = resizeNearest(raster, scaledWidth, scaledHeight);
    const output = new Uint8ClampedArray(width * height * 4);
    const color = background || [255, 255, 255];
    for (let index = 0; index < output.length; index += 4) {
      output[index] = color[0];
      output[index + 1] = color[1];
      output[index + 2] = color[2];
      output[index + 3] = 255;
    }
    const left = Math.floor((width - scaledWidth) / 2);
    const top = Math.floor((height - scaledHeight) / 2);
    for (let y = 0; y < scaledHeight; y += 1) {
      const sourceStart = y * scaledWidth * 4;
      const destinationStart = (((top + y) * width) + left) * 4;
      output.set(scaled.data.subarray(sourceStart, sourceStart + (scaledWidth * 4)), destinationStart);
    }
    return makeRaster(width, height, output);
  }

  function fitRaster(raster, width, height, fit, background) {
    const source = makeRaster(raster.width, raster.height, raster.data);
    assertInteger(width, "target width");
    assertInteger(height, "target height");
    if (!FIT_MODES.includes(fit)) {
      throw new RangeError(`fit must be one of: ${FIT_MODES.join(", ")}`);
    }
    if (fit === "fill") {
      return resizeNearest(source, width, height);
    }
    if (fit === "contain") {
      return paintContain(source, width, height, background);
    }
    if (fit === "cover") {
      const scale = Math.max(width / source.width, height / source.height);
      const scaled = resizeNearest(
        source,
        Math.max(width, Math.round(source.width * scale)),
        Math.max(height, Math.round(source.height * scale)),
      );
      return cropCenter(scaled, width, height);
    }
    const targetAspect = width / height;
    const sourceAspect = source.width / source.height;
    let cropWidth = source.width;
    let cropHeight = source.height;
    if (sourceAspect > targetAspect) {
      cropWidth = Math.max(1, Math.round(source.height * targetAspect));
    } else if (sourceAspect < targetAspect) {
      cropHeight = Math.max(1, Math.round(source.width / targetAspect));
    }
    return resizeNearest(cropCenter(source, cropWidth, cropHeight), width, height);
  }

  function processRaster(raster, options) {
    const settings = options || {};
    const targetWidth = settings.targetWidth;
    const targetHeight = settings.targetHeight;
    assertInteger(targetWidth, "target width");
    assertInteger(targetHeight, "target height");
    let output = flattenOnWhite(raster, settings.background);
    output = orientExif(output, settings.exifOrientation == null ? 1 : settings.exifOrientation);
    output = mirror(output, Boolean(settings.mirrorX), Boolean(settings.mirrorY));
    if (settings.rotate90Cw) {
      output = rotate90Cw(output);
    }
    return fitRaster(
      output,
      targetWidth,
      targetHeight,
      settings.fit || "contain",
      settings.background,
    );
  }

  return Object.freeze({
    FIT_MODES,
    ORIENTATION_PROFILES,
    makeRaster,
    flattenOnWhite,
    orientExif,
    mirror,
    rotate90Cw,
    cropCenter,
    resizeNearest,
    fitRaster,
    processRaster,
  });
});
