/* global importScripts, self, PaperFrameImage, PaperFrameQuantizer */

importScripts("/image_pipeline.js", "/image_quantizer.js");

self.onmessage = function (event) {
  const request = event.data || {};
  try {
    if (!request.data || !Number.isInteger(request.width) ||
        !Number.isInteger(request.height)) {
      throw new RangeError("worker request must include raster dimensions and data");
    }
    const raster = PaperFrameImage.makeRaster(
      request.width,
      request.height,
      new Uint8ClampedArray(request.data),
    );
    const result = PaperFrameQuantizer.quantize(raster, request.mode || "nearest");
    self.postMessage(
      {
        id: request.id,
        ok: true,
        width: result.width,
        height: result.height,
        data: result.data.buffer,
        codes: result.codes.buffer,
      },
      [result.data.buffer, result.codes.buffer],
    );
  } catch (error) {
    self.postMessage({
      id: request.id,
      ok: false,
      error: error instanceof Error ? error.message : String(error),
    });
  }
};
