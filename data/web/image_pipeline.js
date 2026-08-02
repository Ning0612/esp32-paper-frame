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
  const DEFAULT_CROP_POSITION = Object.freeze({ x: 0.5, y: 0.5 });
  const DEFAULT_CROP_ZOOM = 1;

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

  function validateRaster(raster) {
    if (!raster || !Number.isInteger(raster.width) || raster.width <= 0 ||
        !Number.isInteger(raster.height) || raster.height <= 0 ||
        !raster.data || raster.data.length !== raster.width * raster.height * 4) {
      throw new RangeError("invalid raster");
    }
    return raster;
  }

  function copyPixel(source, sourceIndex, destination, destinationIndex) {
    destination[destinationIndex] = source[sourceIndex];
    destination[destinationIndex + 1] = source[sourceIndex + 1];
    destination[destinationIndex + 2] = source[sourceIndex + 2];
    destination[destinationIndex + 3] = source[sourceIndex + 3];
  }

  function readExifOrientation(input) {
    const bytes = input instanceof Uint8Array
      ? input
      : new Uint8Array(input || new ArrayBuffer(0));
    if (bytes.length < 4 || bytes[0] !== 0xFF || bytes[1] !== 0xD8) {
      return 1;
    }
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    let offset = 2;
    while (offset + 4 <= bytes.length) {
      if (bytes[offset] !== 0xFF) {
        offset += 1;
        continue;
      }
      const marker = bytes[offset + 1];
      offset += 2;
      if (marker === 0xD9 || marker === 0xDA) {
        break;
      }
      if (offset + 2 > bytes.length) {
        return 1;
      }
      const segmentLength = view.getUint16(offset, false);
      if (segmentLength < 2 || offset + segmentLength > bytes.length) {
        return 1;
      }
      const segmentStart = offset + 2;
      const segmentEnd = offset + segmentLength;
      if (marker === 0xE1 && segmentLength >= 8 &&
          bytes[segmentStart] === 0x45 && bytes[segmentStart + 1] === 0x78 &&
          bytes[segmentStart + 2] === 0x69 && bytes[segmentStart + 3] === 0x66 &&
          bytes[segmentStart + 4] === 0x00 && bytes[segmentStart + 5] === 0x00) {
        const tiffStart = segmentStart + 6;
        if (tiffStart + 8 > segmentEnd) {
          return 1;
        }
        const littleEndian = bytes[tiffStart] === 0x49 && bytes[tiffStart + 1] === 0x49;
        const bigEndian = bytes[tiffStart] === 0x4D && bytes[tiffStart + 1] === 0x4D;
        if (!littleEndian && !bigEndian) {
          return 1;
        }
        const read16 = (position) => view.getUint16(position, littleEndian);
        const read32 = (position) => view.getUint32(position, littleEndian);
        if (read16(tiffStart + 2) !== 42) {
          return 1;
        }
        const ifdOffset = read32(tiffStart + 4);
        const ifdStart = tiffStart + ifdOffset;
        if (ifdStart + 2 > segmentEnd) {
          return 1;
        }
        const entryCount = read16(ifdStart);
        if (ifdStart + 2 + (entryCount * 12) > segmentEnd) {
          return 1;
        }
        for (let index = 0; index < entryCount; index += 1) {
          const entry = ifdStart + 2 + (index * 12);
          if (read16(entry) !== 0x0112 || read16(entry + 2) !== 3 || read32(entry + 4) !== 1) {
            continue;
          }
          const orientation = read16(entry + 8);
          return orientation >= 1 && orientation <= 8 ? orientation : 1;
        }
        return 1;
      }
      offset = segmentEnd;
    }
    return 1;
  }

  function flattenOnWhite(raster, background) {
    const source = validateRaster(raster);
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
    return { width: source.width, height: source.height, data: output };
  }

  function orientExif(raster, orientation) {
    const source = validateRaster(raster);
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
    return { width, height, data: output };
  }

  function mirror(raster, mirrorX, mirrorY) {
    const source = validateRaster(raster);
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
    return { width: source.width, height: source.height, data: output };
  }

  function rotate90Cw(raster) {
    const source = validateRaster(raster);
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
    return { width, height, data: output };
  }

  function normalizeCropPosition(position) {
    const value = position || DEFAULT_CROP_POSITION;
    const x = value.x == null ? DEFAULT_CROP_POSITION.x : value.x;
    const y = value.y == null ? DEFAULT_CROP_POSITION.y : value.y;
    if (!Number.isFinite(x) || !Number.isFinite(y) ||
        x < 0 || x > 1 || y < 0 || y > 1) {
      throw new RangeError("crop position must contain x/y values from 0 to 1");
    }
    return { x, y };
  }

  function normalizeCropZoom(zoom) {
    const value = zoom == null ? DEFAULT_CROP_ZOOM : Number(zoom);
    if (!Number.isFinite(value) || value < DEFAULT_CROP_ZOOM) {
      throw new RangeError("crop zoom must be a finite number greater than or equal to 1");
    }
    return value;
  }

  function cropGeometry(raster, width, height, cropZoom) {
    const source = validateRaster(raster);
    assertInteger(width, "crop target width");
    assertInteger(height, "crop target height");
    const zoom = normalizeCropZoom(cropZoom);
    const scale = Math.max(width / source.width, height / source.height) * zoom;
    const scaledWidth = Math.max(width, Math.round(source.width * scale));
    const scaledHeight = Math.max(height, Math.round(source.height * scale));
    return Object.freeze({
      zoom,
      scale,
      scaledWidth,
      scaledHeight,
      overflowX: scaledWidth - width,
      overflowY: scaledHeight - height,
    });
  }

  function cropWindow(raster, width, height, cropZoom) {
    const source = validateRaster(raster);
    assertInteger(width, "crop target width");
    assertInteger(height, "crop target height");
    const zoom = normalizeCropZoom(cropZoom);
    const targetAspect = width / height;
    const sourceAspect = source.width / source.height;
    let cropWidth = source.width;
    let cropHeight = source.height;
    if (sourceAspect > targetAspect) {
      cropWidth = Math.max(1, Math.round(source.height * targetAspect));
    } else if (sourceAspect < targetAspect) {
      cropHeight = Math.max(1, Math.round(source.width / targetAspect));
    }
    return Object.freeze({
      width: Math.max(1, Math.round(cropWidth / zoom)),
      height: Math.max(1, Math.round(cropHeight / zoom)),
    });
  }

  function cropRaster(raster, width, height, position) {
    const source = validateRaster(raster);
    assertInteger(width, "crop width");
    assertInteger(height, "crop height");
    if (width > source.width || height > source.height) {
      throw new RangeError("crop cannot exceed source dimensions");
    }
    const anchor = normalizeCropPosition(position);
    const left = Math.floor((source.width - width) * anchor.x);
    const top = Math.floor((source.height - height) * anchor.y);
    const output = new Uint8ClampedArray(width * height * 4);
    for (let y = 0; y < height; y += 1) {
      const sourceStart = (((top + y) * source.width) + left) * 4;
      const destinationStart = y * width * 4;
      output.set(source.data.subarray(sourceStart, sourceStart + (width * 4)), destinationStart);
    }
    return { width, height, data: output };
  }

  function resizeCropNearest(raster, width, height, geometry, position) {
    const source = validateRaster(raster);
    assertInteger(width, "crop target width");
    assertInteger(height, "crop target height");
    const anchor = normalizeCropPosition(position);
    const left = Math.floor(geometry.overflowX * anchor.x);
    const top = Math.floor(geometry.overflowY * anchor.y);
    const output = new Uint8ClampedArray(width * height * 4);
    for (let y = 0; y < height; y += 1) {
      const scaledY = top + y;
      const sourceY = Math.min(
        source.height - 1,
        Math.floor((scaledY * source.height) / geometry.scaledHeight),
      );
      for (let x = 0; x < width; x += 1) {
        const scaledX = left + x;
        const sourceX = Math.min(
          source.width - 1,
          Math.floor((scaledX * source.width) / geometry.scaledWidth),
        );
        copyPixel(
          source.data,
          ((sourceY * source.width) + sourceX) * 4,
          output,
          ((y * width) + x) * 4,
        );
      }
    }
    return { width, height, data: output };
  }

  function cropCenter(raster, width, height) {
    return cropRaster(raster, width, height, DEFAULT_CROP_POSITION);
  }

  function resizeNearest(raster, width, height) {
    const source = validateRaster(raster);
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
    return { width, height, data: output };
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
    return { width, height, data: output };
  }

  function fitRaster(raster, width, height, fit, background, cropPosition, cropZoom) {
    const source = validateRaster(raster);
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
    const anchor = normalizeCropPosition(cropPosition);
    const zoom = normalizeCropZoom(cropZoom);
    if (fit === "cover") {
      const geometry = cropGeometry(source, width, height, zoom);
      return resizeCropNearest(source, width, height, geometry, anchor);
    }
    const cropWindowSize = cropWindow(source, width, height, zoom);
    return resizeNearest(
      cropRaster(source, cropWindowSize.width, cropWindowSize.height, anchor),
      width,
      height,
    );
  }

  function processRaster(raster, options) {
    const settings = options || {};
    const targetWidth = settings.targetWidth;
    const targetHeight = settings.targetHeight;
    assertInteger(targetWidth, "target width");
    assertInteger(targetHeight, "target height");
    let output = settings.skipFlatten
      ? validateRaster(raster)
      : flattenOnWhite(raster, settings.background);
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
      settings.cropPosition,
      settings.cropZoom,
    );
  }

  return Object.freeze({
    FIT_MODES,
    ORIENTATION_PROFILES,
    DEFAULT_CROP_POSITION,
    DEFAULT_CROP_ZOOM,
    makeRaster,
    readExifOrientation,
    flattenOnWhite,
    orientExif,
    mirror,
    rotate90Cw,
    normalizeCropPosition,
    normalizeCropZoom,
    cropGeometry,
    cropWindow,
    cropRaster,
    cropCenter,
    resizeNearest,
    fitRaster,
    processRaster,
  });
});
