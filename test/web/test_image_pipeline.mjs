import assert from "node:assert/strict";
import pipeline from "../../data/web/image_pipeline.js";

function pixel(raster, x, y) {
  const index = ((y * raster.width) + x) * 4;
  return Array.from(raster.data.slice(index, index + 4));
}

function makeLabeledRaster(width, height) {
  const data = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const index = ((y * width) + x) * 4;
      data[index] = (x + 1) * 10;
      data[index + 1] = (y + 1) * 20;
      data[index + 2] = 30;
      data[index + 3] = 255;
    }
  }
  return pipeline.makeRaster(width, height, data);
}

function testExifOrientationSixRotatesClockwise() {
  const source = makeLabeledRaster(2, 3);
  const result = pipeline.orientExif(source, 6);
  assert.deepEqual([result.width, result.height], [3, 2]);
  assert.deepEqual(pixel(result, 0, 0), pixel(source, 0, 2));
  assert.deepEqual(pixel(result, 2, 0), pixel(source, 0, 0));
  assert.deepEqual(pixel(result, 0, 1), pixel(source, 1, 2));
}

function testMirrorAndRotateAreSeparateOperations() {
  const source = makeLabeledRaster(2, 1);
  const mirrored = pipeline.mirror(source, true, false);
  assert.deepEqual(pixel(mirrored, 0, 0), pixel(source, 1, 0));
  const rotated = pipeline.rotate90Cw(mirrored);
  assert.deepEqual([rotated.width, rotated.height], [1, 2]);
  assert.deepEqual(pixel(rotated, 0, 0), pixel(source, 1, 0));
  assert.deepEqual(pixel(rotated, 0, 1), pixel(source, 0, 0));
}

function testRepeatedButtonOperationsApplyOneTransformPerClick() {
  const source = makeLabeledRaster(2, 2);
  const rotatedOnce = pipeline.rotate90Cw(source);
  const rotatedTwice = pipeline.rotate90Cw(rotatedOnce);
  assert.deepEqual([rotatedOnce.width, rotatedOnce.height], [2, 2]);
  assert.deepEqual([rotatedTwice.width, rotatedTwice.height], [2, 2]);
  assert.deepEqual(pixel(rotatedTwice, 0, 0), pixel(source, 1, 1));
  assert.deepEqual(pixel(rotatedTwice, 1, 1), pixel(source, 0, 0));
}

function testTransparentPixelsFlattenToWhite() {
  const source = pipeline.makeRaster(1, 2, new Uint8ClampedArray([
    0, 0, 0, 0,
    100, 150, 200, 128,
  ]));
  const result = pipeline.flattenOnWhite(source);
  assert.deepEqual(pixel(result, 0, 0), [255, 255, 255, 255]);
  assert.deepEqual(pixel(result, 0, 1), [177, 202, 227, 255]);
}

function testFitModesAreExactlyContainCoverFill() {
  assert.deepEqual(pipeline.FIT_MODES, ["contain", "cover", "fill"]);
}

function testEveryFitModeProducesTheRequestedDimensions() {
  const source = makeLabeledRaster(3, 2);
  for (const fit of pipeline.FIT_MODES) {
    const result = pipeline.fitRaster(source, 5, 7, fit);
    assert.deepEqual([result.width, result.height], [5, 7], fit);
  }
  const contain = pipeline.fitRaster(source, 5, 7, "contain");
  assert.deepEqual(pixel(contain, 0, 0), [255, 255, 255, 255]);
  const fill = pipeline.fitRaster(source, 5, 7, "fill");
  assert.notDeepEqual(pixel(fill, 0, 0), [255, 255, 255, 255, 255]);
}

function testCoverAnchorSelectsTheExpectedVerticalWindow() {
  const source = makeLabeledRaster(2, 4);
  const top = pipeline.fitRaster(source, 2, 2, "cover", undefined, { x: 0.5, y: 0 });
  const bottom = pipeline.fitRaster(source, 2, 2, "cover", undefined, { x: 0.5, y: 1 });
  assert.deepEqual(pixel(top, 0, 0), pixel(source, 0, 0));
  assert.deepEqual(pixel(bottom, 0, 0), pixel(source, 0, 2));
}

function testCoverZoomMagnifiesTheSelectedTarget() {
  const source = makeLabeledRaster(2, 8);
  const normal = pipeline.fitRaster(source, 2, 2, "cover", undefined, { x: 0.5, y: 0 }, 1);
  const zoomed = pipeline.fitRaster(source, 2, 2, "cover", undefined, { x: 0.5, y: 0 }, 2);
  const zoomedThree = pipeline.fitRaster(source, 2, 2, "cover", undefined, { x: 0.5, y: 0 }, 3);
  assert.notDeepEqual(pixel(normal, 0, 0), pixel(normal, 0, 1));
  assert.deepEqual(pixel(zoomed, 0, 0), pixel(zoomed, 0, 1));
  assert.deepEqual(pixel(zoomedThree, 0, 0), pixel(zoomedThree, 0, 1));
}

function testCoverZoomKeepsNearestSamplingWithoutIntermediateRaster() {
  const source = makeLabeledRaster(5, 3);
  const position = { x: 0.3, y: 0.7 };
  const geometry = pipeline.cropGeometry(source, 4, 2, 2);
  const scaled = pipeline.resizeNearest(source, geometry.scaledWidth, geometry.scaledHeight);
  const expected = pipeline.cropRaster(scaled, 4, 2, position);
  const actual = pipeline.fitRaster(source, 4, 2, "cover", undefined, position, 2);
  assert.deepEqual(Array.from(actual.data), Array.from(expected.data));
}

function testProcessOrderNormalizesExifBeforeUserOperations() {
  const source = makeLabeledRaster(2, 3);
  const result = pipeline.processRaster(source, {
    exifOrientation: 6,
    mirrorX: true,
    rotate90Cw: false,
    fit: "fill",
    targetWidth: 3,
    targetHeight: 2,
  });
  const exif = pipeline.orientExif(source, 6);
  const expected = pipeline.mirror(exif, true, false);
  assert.deepEqual(Array.from(result.data), Array.from(expected.data));
}

function testInvalidInputsFailClosed() {
  assert.throws(() => pipeline.orientExif(makeLabeledRaster(1, 1), 9), RangeError);
  assert.throws(() => pipeline.fitRaster(makeLabeledRaster(1, 1), 2, 2, "unknown"), RangeError);
  assert.throws(() => pipeline.fitRaster(makeLabeledRaster(1, 1), 2, 2, "crop"), RangeError);
  assert.throws(() => pipeline.makeRaster(2, 2, new Uint8ClampedArray(3)), RangeError);
  assert.throws(() => pipeline.normalizeCropPosition({ x: 2, y: 0.5 }), RangeError);
  assert.throws(() => pipeline.normalizeCropPosition({ x: 0.5, y: -1 }), RangeError);
  assert.throws(() => pipeline.normalizeCropZoom(0.99), RangeError);
  assert.equal(pipeline.MAX_CROP_ZOOM, 4);
  assert.throws(() => pipeline.normalizeCropZoom(pipeline.MAX_CROP_ZOOM + 0.01), RangeError);
  assert.equal(pipeline.normalizeCropZoom(pipeline.MAX_CROP_ZOOM), pipeline.MAX_CROP_ZOOM);
}

function testExifReaderExtractsJpegOrientationAndFailsClosed() {
  const exif = new Uint8Array([
    0x45, 0x78, 0x69, 0x66, 0x00, 0x00,
    0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x01, 0x00,
    0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  ]);
  const jpeg = new Uint8Array(2 + 2 + 2 + exif.length + 2);
  jpeg.set([0xFF, 0xD8, 0xFF, 0xE1], 0);
  jpeg[4] = (exif.length + 2) >> 8;
  jpeg[5] = (exif.length + 2) & 0xFF;
  jpeg.set(exif, 6);
  jpeg.set([0xFF, 0xD9], 6 + exif.length);
  assert.equal(pipeline.readExifOrientation(jpeg), 6);
  assert.equal(pipeline.readExifOrientation(new Uint8Array([0x89, 0x50, 0x4E, 0x47])), 1);
  assert.equal(pipeline.readExifOrientation(jpeg.subarray(0, 9)), 1);
}

testExifOrientationSixRotatesClockwise();
testMirrorAndRotateAreSeparateOperations();
testRepeatedButtonOperationsApplyOneTransformPerClick();
testTransparentPixelsFlattenToWhite();
testFitModesAreExactlyContainCoverFill();
testEveryFitModeProducesTheRequestedDimensions();
testCoverAnchorSelectsTheExpectedVerticalWindow();
testCoverZoomMagnifiesTheSelectedTarget();
testCoverZoomKeepsNearestSamplingWithoutIntermediateRaster();
testProcessOrderNormalizesExifBeforeUserOperations();
testInvalidInputsFailClosed();
testExifReaderExtractsJpegOrientationAndFailsClosed();
console.log("image_pipeline: 12 tests passed");
