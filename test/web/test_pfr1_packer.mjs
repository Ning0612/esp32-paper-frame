import assert from "node:assert/strict";
import zlib from "node:zlib";
import pipeline from "../../data/web/image_pipeline.js";
import quantizer from "../../data/web/image_quantizer.js";
import pfr1 from "../../data/web/image_pfr1.js";

function readU16(bytes, offset) {
  return bytes[offset] | (bytes[offset + 1] << 8);
}

function readU32(bytes, offset) {
  return (bytes[offset] | (bytes[offset + 1] << 8) |
    (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24)) >>> 0;
}

function whiteRaster(width, height) {
  const data = new Uint8ClampedArray(width * height * 4);
  data.fill(255);
  return pipeline.makeRaster(width, height, data);
}

// A raster whose quantized codes are pseudo-random across all six palette
// entries: adversarial input for deflate, since it has none of the runs or
// repetition that make PaperFrame's typical (mostly flat-color) images
// compressible.
function noiseRaster(width, height) {
  const data = new Uint8ClampedArray(width * height * 4);
  const colors = [
    [0, 0, 0], [255, 255, 255], [255, 255, 0],
    [255, 0, 0], [0, 0, 255], [0, 255, 0],
  ];
  let state = 0x2545F491;
  for (let pixel = 0; pixel < width * height; pixel += 1) {
    state = (state * 1103515245 + 12345) >>> 0;
    const color = colors[state % colors.length];
    const offset = pixel * 4;
    data[offset] = color[0];
    data[offset + 1] = color[1];
    data[offset + 2] = color[2];
    data[offset + 3] = 255;
  }
  return pipeline.makeRaster(width, height, data);
}

async function testGoldenLandscapeMatchesCrcAndPackedNibbles() {
  const source = whiteRaster(800, 440);
  const quantized = quantizer.quantize(source, "nearest");
  const file = await pfr1.packPfr1(quantized, {
    filename: "golden.pfr1",
    orientation: "landscape",
    flags: 0x0001,
    dithering: "nearest",
    compress: false,
  });
  assert.equal(file.length, 176043);
  assert.equal(readU16(file, 8), 800);
  assert.equal(readU16(file, 10), 440);
  assert.equal(readU32(file, 16), 176000);
  assert.equal(readU16(file, 20), 11);
  assert.equal(readU32(file, 24), 0xAF00B5BD);
  assert.equal(readU32(file, 28), 0xC96F698B);
  const payloadOffset = 32 + 11;
  assert.equal(file[payloadOffset], 0x11);
  assert.equal(file[file.length - 1], 0x11);
}

async function testPortraitAndFilenameValidation() {
  const source = whiteRaster(480, 760);
  const quantized = quantizer.quantize(source, "bayer-4x4");
  const file = await pfr1.packPfr1(quantized, {
    filename: "相框.pfr1",
    orientation: "portrait",
    dithering: "bayer-4x4",
  });
  assert.equal(readU16(file, 8), 480);
  assert.equal(readU16(file, 10), 760);
  assert.equal(file[14], 3);
  await assert.rejects(
    () => pfr1.packPfr1(quantized, { filename: "../escape", orientation: "portrait" }),
    RangeError,
  );
  await assert.rejects(
    () => pfr1.packPfr1(quantized, { filename: "bad\nname", orientation: "portrait" }),
    RangeError,
  );
}

async function testPackerRejectsCallerSuppliedCompressedFlag() {
  const source = whiteRaster(800, 440);
  const quantized = quantizer.quantize(source, "nearest");
  // FLAG_COMPRESSED must only ever be set by packPfr1 itself based on
  // whether it actually compressed the payload -- a caller asserting it
  // regardless (e.g. when compression won't be used) would otherwise
  // produce a header claiming a compressed payload over raw nibble bytes.
  await assert.rejects(
    () => pfr1.packPfr1(quantized, {
      filename: "x",
      orientation: "landscape",
      flags: pfr1.FLAG_COMPRESSED,
    }),
    RangeError,
  );
}

async function testPackerRejectsWrongRasterAndPaletteIndex() {
  const source = whiteRaster(800, 440);
  const quantized = quantizer.quantize(source, "nearest");
  await assert.rejects(
    () => pfr1.packPfr1(quantized, { filename: "x", orientation: "portrait" }),
    RangeError,
  );
  const bad = { ...quantized, codes: new Uint8Array(quantized.codes) };
  bad.codes[0] = 99;
  await assert.rejects(
    () => pfr1.packPfr1(bad, { filename: "x", orientation: "landscape" }),
    RangeError,
  );
}

// Compression helps: an all-white raster's packed nibble payload is a
// single repeated byte, which deflate shrinks drastically. The packer must
// set the compressed flag, produce a smaller file than the golden
// (uncompressed) vector above, and the compressed bytes must round-trip
// via raw inflate back to the exact original nibble payload.
async function testCompressionAppliesWhenItShrinksThePayload() {
  const source = whiteRaster(800, 440);
  const quantized = quantizer.quantize(source, "nearest");
  const file = await pfr1.packPfr1(quantized, {
    filename: "compressed.pfr1",
    orientation: "landscape",
    dithering: "nearest",
  });
  assert.equal(readU16(file, 6) & pfr1.FLAG_COMPRESSED, pfr1.FLAG_COMPRESSED);
  const payloadLength = readU32(file, 16);
  assert.ok(payloadLength < 176000, "compressed payload should be smaller than uncompressed");
  const payloadOffset = 32 + "compressed.pfr1".length;
  const payload = file.subarray(payloadOffset, payloadOffset + payloadLength);
  assert.equal(readU32(file, 24), pfr1.crc32(payload));
  assert.equal(readU32(file, 28), pfr1.crc32(file.subarray(0, 24)));
  const inflated = zlib.inflateRawSync(Buffer.from(payload));
  assert.equal(inflated.length, 176000);
  assert.ok(inflated.every((byte) => byte === 0x11), "inflated payload should be all-white nibbles");
}

// Compression unavailable (e.g. an older runtime without
// CompressionStream("deflate-raw") support): the packer must fall back to
// exactly the pre-compression uncompressed output (byte-identical, flag
// clear) rather than throwing or producing a corrupt file. Constructing a
// PFR1 payload where compression genuinely never helps isn't really
// possible for this format: it's only 6 valid palette symbols packed into
// 4-bit nibbles, so plain Huffman coding alone (no LZ77 redundancy needed)
// almost always shrinks it below the raw 4-bits-per-pixel packing -- the
// runtime-unavailable case is the realistic fallback trigger.
async function testFallsBackToUncompressedWhenCompressionIsUnavailable() {
  const source = noiseRaster(800, 440);
  const quantized = quantizer.quantize(source, "nearest");
  const originalCompressionStream = globalThis.CompressionStream;
  let fallbackFile;
  try {
    delete globalThis.CompressionStream;
    fallbackFile = await pfr1.packPfr1(quantized, {
      filename: "noise.pfr1",
      orientation: "landscape",
      dithering: "nearest",
    });
  } finally {
    globalThis.CompressionStream = originalCompressionStream;
  }
  assert.equal(readU16(fallbackFile, 6) & pfr1.FLAG_COMPRESSED, 0);
  assert.equal(readU32(fallbackFile, 16), 176000);
  assert.equal(fallbackFile.length, 32 + "noise.pfr1".length + 176000);

  const directFile = await pfr1.packPfr1(quantized, {
    filename: "noise.pfr1",
    orientation: "landscape",
    dithering: "nearest",
    compress: false,
  });
  assert.deepEqual(fallbackFile, directFile);
}

// Regenerates docs/formats/PFR1.md's "Cross-language golden vector
// (compressed)" fixture (same construction used to produce the documented
// bytes: zlib.deflateRawSync level 9 over 176,000 bytes of 0x11) and checks
// it against the exact length/CRC32 documented there and asserted by
// test_pfr1_validator's matching C++ test. Compressed bytes are only ever
// produced by the browser, never the firmware, so there's no requirement
// that packPfr1 itself reproduce this exact encoder output -- what this
// proves is decode-side interop: Node's zlib (standing in for the
// browser's CompressionStream/DecompressionStream, both raw-DEFLATE-
// compliant) and the C++ side agree on what these bytes decode to.
function testCrossLanguageGoldenCompressedVectorDecodesCorrectly() {
  const rawPayload = Buffer.alloc(176000, 0x11);
  const compressed = zlib.deflateRawSync(rawPayload, { level: 9 });
  assert.equal(compressed.length, 188);
  assert.equal(pfr1.crc32(compressed), 0xBFA93827);
  const inflated = zlib.inflateRawSync(compressed);
  assert.deepEqual(inflated, rawPayload);
}

async function main() {
  await testGoldenLandscapeMatchesCrcAndPackedNibbles();
  await testPortraitAndFilenameValidation();
  await testPackerRejectsWrongRasterAndPaletteIndex();
  await testPackerRejectsCallerSuppliedCompressedFlag();
  await testCompressionAppliesWhenItShrinksThePayload();
  await testFallsBackToUncompressedWhenCompressionIsUnavailable();
  testCrossLanguageGoldenCompressedVectorDecodesCorrectly();
  console.log("pfr1_packer: 7 tests passed");
}

await main();
