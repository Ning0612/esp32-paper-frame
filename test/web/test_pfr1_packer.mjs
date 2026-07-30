import assert from "node:assert/strict";
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

function testGoldenLandscapeMatchesCrcAndPackedNibbles() {
  const source = whiteRaster(800, 440);
  const quantized = quantizer.quantize(source, "nearest");
  const file = pfr1.packPfr1(quantized, {
    filename: "golden.pfr1",
    orientation: "landscape",
    flags: 0x0001,
    dithering: "nearest",
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

function testPortraitAndFilenameValidation() {
  const source = whiteRaster(480, 760);
  const quantized = quantizer.quantize(source, "bayer-4x4");
  const file = pfr1.packPfr1(quantized, {
    filename: "相框.pfr1",
    orientation: "portrait",
    dithering: "bayer-4x4",
  });
  assert.equal(readU16(file, 8), 480);
  assert.equal(readU16(file, 10), 760);
  assert.equal(file[14], 3);
  assert.throws(() => pfr1.packPfr1(quantized, { filename: "../escape", orientation: "portrait" }), RangeError);
  assert.throws(() => pfr1.packPfr1(quantized, { filename: "bad\nname", orientation: "portrait" }), RangeError);
}

function testPackerRejectsWrongRasterAndPaletteIndex() {
  const source = whiteRaster(800, 440);
  const quantized = quantizer.quantize(source, "nearest");
  assert.throws(() => pfr1.packPfr1(quantized, { filename: "x", orientation: "portrait" }), RangeError);
  const bad = { ...quantized, codes: new Uint8Array(quantized.codes) };
  bad.codes[0] = 99;
  assert.throws(() => pfr1.packPfr1(bad, { filename: "x", orientation: "landscape" }), RangeError);
}

testGoldenLandscapeMatchesCrcAndPackedNibbles();
testPortraitAndFilenameValidation();
testPackerRejectsWrongRasterAndPaletteIndex();
console.log("pfr1_packer: 3 tests passed");
