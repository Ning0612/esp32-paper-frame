import assert from "node:assert/strict";
import pipeline from "../../data/web/image_pipeline.js";
import quantizer from "../../data/web/image_quantizer.js";

function makeGradient(width, height) {
  const data = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const index = ((y * width) + x) * 4;
      data[index] = Math.round((x * 255) / Math.max(1, width - 1));
      data[index + 1] = Math.round((y * 255) / Math.max(1, height - 1));
      data[index + 2] = Math.round(((x + y) * 255) / Math.max(1, width + height - 2));
      data[index + 3] = 255;
    }
  }
  return pipeline.makeRaster(width, height, data);
}

function assertValidCodes(result) {
  const validCodes = new Set(quantizer.PALETTE.map((color) => color.code));
  for (const paletteIndex of result.codes) {
    assert.ok(paletteIndex < quantizer.PALETTE.length);
    assert.ok(validCodes.has(quantizer.PALETTE[paletteIndex].code));
  }
  assert.equal(result.data.length, result.width * result.height * 4);
}

function testNearestUsesE6PaletteAndTransparentWhite() {
  const source = pipeline.makeRaster(3, 1, new Uint8ClampedArray([
    255, 0, 0, 255,
    0, 0, 0, 0,
    0, 255, 0, 255,
  ]));
  const result = quantizer.quantize(source, "nearest");
  assert.deepEqual(Array.from(result.codes), [3, 1, 5]);
  assert.deepEqual(Array.from(result.data.slice(0, 4)), [255, 0, 0, 255]);
  assert.deepEqual(Array.from(result.data.slice(4, 8)), [255, 255, 255, 255]);
}

function testAllModesAreDeterministicAndProduceOnlyPalettePixels() {
  const source = makeGradient(17, 11);
  for (const mode of quantizer.MODES) {
    const first = quantizer.quantize(source, mode);
    const second = quantizer.quantize(source, mode);
    assertValidCodes(first);
    assert.deepEqual(Array.from(first.codes), Array.from(second.codes), mode);
    assert.deepEqual(Array.from(first.data), Array.from(second.data), mode);
  }
}

function testDitherModesCanPreserveLocalContrast() {
  const source = makeGradient(8, 8);
  const nearest = quantizer.quantize(source, "nearest");
  for (const mode of ["floyd-steinberg", "atkinson", "bayer-4x4"]) {
    const result = quantizer.quantize(source, mode);
    assertValidCodes(result);
    assert.notDeepEqual(Array.from(result.codes), Array.from(nearest.codes), mode);
  }
}

function testInvalidModeFailsClosed() {
  assert.throws(() => quantizer.quantize(makeGradient(1, 1), "rgb"), RangeError);
}

testNearestUsesE6PaletteAndTransparentWhite();
testAllModesAreDeterministicAndProduceOnlyPalettePixels();
testDitherModesCanPreserveLocalContrast();
testInvalidModeFailsClosed();
console.log("image_quantizer: 4 tests passed");
