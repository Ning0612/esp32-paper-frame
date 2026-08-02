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
  const FLAG_COMPRESSED = 0x0008;
  const DITHERING = Object.freeze({
    "floyd-steinberg": 1,
    atkinson: 2,
  });

  // Raw DEFLATE (no zlib/gzip wrapper), matching the firmware's ROM miniz
  // decoder (see docs/formats/PFR1.md "Payload 壓縮"). Returns null if the
  // runtime has no CompressionStream("deflate-raw") support or compression
  // otherwise fails, so callers can fall back to storing the payload
  // uncompressed rather than failing the whole upload over it.
  async function deflateRawOrNull(bytes) {
    if (typeof CompressionStream !== "function") {
      return null;
    }
    try {
      const stream = new Blob([bytes]).stream().pipeThrough(
        new CompressionStream("deflate-raw"),
      );
      return new Uint8Array(await new Response(stream).arrayBuffer());
    } catch (error) {
      return null;
    }
  }

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

  async function packPfr1(quantized, options) {
    const settings = options || {};
    const profile = orientationProfile(settings.orientation == null ? "landscape" : settings.orientation);
    if (!quantized || quantized.width !== profile.width || quantized.height !== profile.height ||
        !quantized.codes || quantized.codes.length !== profile.width * profile.height) {
      throw new RangeError("quantized raster dimensions do not match the orientation profile");
    }
    const filename = utf8Bytes(settings.filename);
    const flags = settings.flags == null ? 0 : settings.flags;
    if (!Number.isInteger(flags) || flags < 0 || flags > 0x000F) {
      throw new RangeError("flags contain an unsupported bit");
    }
    // FLAG_COMPRESSED is decided below from whether compression actually
    // shrank the payload, never by the caller: a caller-supplied 0x0008
    // combined with compression not being used (or not helping) would
    // otherwise produce a header claiming a compressed payload over bytes
    // that are actually raw nibbles, which the firmware would then fail to
    // inflate.
    if ((flags & FLAG_COMPRESSED) !== 0) {
      throw new RangeError("flags must not set FLAG_COMPRESSED; it is managed by packPfr1");
    }
    const ditheringName = settings.dithering || "floyd-steinberg";
    if (!Object.prototype.hasOwnProperty.call(DITHERING, ditheringName)) {
      throw new RangeError("unsupported dithering mode");
    }

    const uncompressedPayload = new Uint8Array((profile.width * profile.height) / 2);
    for (let index = 0; index < quantized.codes.length; index += 1) {
      const paletteIndex = quantized.codes[index];
      if (!Number.isInteger(paletteIndex) || paletteIndex < 0 || paletteIndex >= quantizer.PALETTE.length) {
        throw new RangeError("quantized result contains an invalid palette index");
      }
      const nativeCode = quantizer.PALETTE[paletteIndex].code;
      const byteOffset = Math.floor(index / 2);
      if ((index & 1) === 0) {
        uncompressedPayload[byteOffset] = nativeCode << 4;
      } else {
        uncompressedPayload[byteOffset] |= nativeCode;
      }
    }

    // Only store compressed when it actually shrinks the payload -- a
    // compressed file that isn't smaller has no benefit and would just cost
    // the firmware an extra inflate pass for nothing (see docs/formats/
    // PFR1.md "Payload 壓縮"), so fall back to the uncompressed bytes
    // whenever compression doesn't help or isn't available. Callers that
    // need the historical always-uncompressed output (e.g. reproducing the
    // documented cross-language golden vector) can pass compress: false.
    const compressedPayload = settings.compress === false
      ? null
      : await deflateRawOrNull(uncompressedPayload);
    const useCompressed =
      compressedPayload != null && compressedPayload.length < uncompressedPayload.length;
    const payload = useCompressed ? compressedPayload : uncompressedPayload;
    const finalFlags = useCompressed ? (flags | FLAG_COMPRESSED) : flags;

    const file = new Uint8Array(HEADER_SIZE + filename.length + payload.length);
    file.set([0x50, 0x46, 0x52, 0x31], 0);
    const view = new DataView(file.buffer);
    view.setUint8(4, 1);
    view.setUint8(5, HEADER_SIZE);
    view.setUint16(6, finalFlags, true);
    view.setUint16(8, profile.width, true);
    view.setUint16(10, profile.height, true);
    view.setUint8(12, profile.orientation);
    view.setUint8(13, 1);
    view.setUint8(14, DITHERING[ditheringName]);
    view.setUint8(15, 0);
    view.setUint32(16, payload.length, true);
    view.setUint16(20, filename.length, true);
    view.setUint16(22, 0, true);
    file.set(filename, HEADER_SIZE);

    const payloadOffset = HEADER_SIZE + filename.length;
    file.set(payload, payloadOffset);
    view.setUint32(24, crc32(file.subarray(payloadOffset)), true);
    view.setUint32(28, crc32(file.subarray(0, 24)), true);
    return file;
  }

  return Object.freeze({
    HEADER_SIZE,
    MAX_FILENAME_BYTES,
    FLAG_COMPRESSED,
    DITHERING,
    crc32,
    packPfr1,
  });
});
