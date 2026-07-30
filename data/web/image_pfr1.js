(function (global, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory(
      require("./image_pipeline.js"),
      require("./image_quantizer.js"),
    );
  } else {
    global.PaperFramePfr1 = factory(global.PaperFrameImage, global.PaperFrameQuantizer);
  }
})(typeof globalThis === "object" ? globalThis : this, function (imagePipeline, quantizer) {
  "use strict";

  if (!imagePipeline || !quantizer) {
    throw new Error("PaperFrameImage and PaperFrameQuantizer must load before PFR1");
  }

  const HEADER_SIZE = 32;
  const MAX_FILENAME_BYTES = 96;
  const DITHERING = Object.freeze({
    nearest: 0,
    "floyd-steinberg": 1,
    atkinson: 2,
    "bayer-4x4": 3,
  });

  function crc32Update(crc, data) {
    for (const value of data) {
      crc ^= value;
      for (let bit = 0; bit < 8; bit += 1) {
        const mask = 0 - (crc & 1);
        crc = (crc >>> 1) ^ (0xEDB88320 & mask);
      }
    }
    return crc >>> 0;
  }

  function crc32(data) {
    return (~crc32Update(0xFFFFFFFF, data)) >>> 0;
  }

  function utf8Bytes(value) {
    if (typeof value !== "string" || value.length === 0) {
      throw new RangeError("filename must be a non-empty string");
    }
    for (let index = 0; index < value.length; index += 1) {
      const code = value.charCodeAt(index);
      if (code >= 0xD800 && code <= 0xDBFF) {
        const next = value.charCodeAt(index + 1);
        if (next < 0xDC00 || next > 0xDFFF) {
          throw new RangeError("filename contains an unpaired UTF-16 surrogate");
        }
        index += 1;
      } else if (code >= 0xDC00 && code <= 0xDFFF) {
        throw new RangeError("filename contains an unpaired UTF-16 surrogate");
      }
    }
    if (value.startsWith(".") || value.endsWith(".") || value.endsWith(" ") ||
        value === "." || value === ".." || /[\\/\u0000-\u001F\u007F]/u.test(value)) {
      throw new RangeError("filename is not a safe basename");
    }
    const bytes = new TextEncoder().encode(value);
    if (bytes.length === 0 || bytes.length > MAX_FILENAME_BYTES) {
      throw new RangeError("filename must be at most 96 UTF-8 bytes");
    }
    return bytes;
  }

  function orientationProfile(orientation) {
    if (orientation === "landscape" || orientation === 0) {
      return imagePipeline.ORIENTATION_PROFILES.landscape;
    }
    if (orientation === "portrait" || orientation === 1) {
      return imagePipeline.ORIENTATION_PROFILES.portrait;
    }
    throw new RangeError("orientation must be landscape or portrait");
  }

  function packPfr1(quantized, options) {
    const settings = options || {};
    const profile = orientationProfile(settings.orientation == null ? "landscape" : settings.orientation);
    if (!quantized || quantized.width !== profile.width || quantized.height !== profile.height ||
        !quantized.codes || quantized.codes.length !== profile.width * profile.height) {
      throw new RangeError("quantized raster dimensions do not match the orientation profile");
    }
    const filename = utf8Bytes(settings.filename);
    const flags = settings.flags == null ? 0 : settings.flags;
    if (!Number.isInteger(flags) || flags < 0 || flags > 0x0007) {
      throw new RangeError("flags contain an unsupported bit");
    }
    const ditheringName = settings.dithering || "nearest";
    if (!Object.prototype.hasOwnProperty.call(DITHERING, ditheringName)) {
      throw new RangeError("unsupported dithering mode");
    }
    const payloadLength = (profile.width * profile.height) / 2;
    const file = new Uint8Array(HEADER_SIZE + filename.length + payloadLength);
    file.set([0x50, 0x46, 0x52, 0x31], 0);
    const view = new DataView(file.buffer);
    view.setUint8(4, 1);
    view.setUint8(5, HEADER_SIZE);
    view.setUint16(6, flags, true);
    view.setUint16(8, profile.width, true);
    view.setUint16(10, profile.height, true);
    view.setUint8(12, profile.orientation);
    view.setUint8(13, 1);
    view.setUint8(14, DITHERING[ditheringName]);
    view.setUint8(15, 0);
    view.setUint32(16, payloadLength, true);
    view.setUint16(20, filename.length, true);
    view.setUint16(22, 0, true);
    file.set(filename, HEADER_SIZE);

    const payloadOffset = HEADER_SIZE + filename.length;
    for (let index = 0; index < quantized.codes.length; index += 1) {
      const paletteIndex = quantized.codes[index];
      if (!Number.isInteger(paletteIndex) || paletteIndex < 0 || paletteIndex >= quantizer.PALETTE.length) {
        throw new RangeError("quantized result contains an invalid palette index");
      }
      const nativeCode = quantizer.PALETTE[paletteIndex].code;
      const byteOffset = payloadOffset + Math.floor(index / 2);
      if ((index & 1) === 0) {
        file[byteOffset] = nativeCode << 4;
      } else {
        file[byteOffset] |= nativeCode;
      }
    }
    view.setUint32(24, crc32(file.subarray(payloadOffset)), true);
    view.setUint32(28, crc32(file.subarray(0, 24)), true);
    return file;
  }

  return Object.freeze({
    HEADER_SIZE,
    MAX_FILENAME_BYTES,
    DITHERING,
    crc32,
    packPfr1,
  });
});
