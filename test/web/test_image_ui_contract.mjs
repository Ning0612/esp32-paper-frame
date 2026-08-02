import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const html = await readFile("data/web/index.html", "utf8");
const ui = await readFile("data/web/ui.js", "utf8");
const css = await readFile("data/web/style.css", "utf8");

assert.ok(html.includes('<script src="/image_pipeline.js" defer></script>'));
assert.ok(html.includes('<script src="/image_quantizer.js" defer></script>'));
assert.ok(html.includes('<script src="/image_pfr1.js" defer></script>'));
assert.ok(html.includes('data-view="image"'));
assert.ok(html.includes('id="top-navigation"'));
for (const id of [
  "image-source", "image-orientation", "image-fit", "image-dither", "image-filename",
  "image-mirror-x", "image-mirror-y", "image-rotate", "preview-original",
  "preview-processed", "preview-sixcolor", "preview-frame", "download-pfr1",
  "upload-pfr1",
  "image-library-refresh", "image-library-status", "image-library-list",
  "image-processed-card", "image-crop-hint", "image-carousel-form",
  "image-carousel-random", "image-carousel-save", "image-carousel-status",
]) {
  assert.ok(html.includes(`id="${id}"`), id);
}
for (const id of ["image-mirror-x", "image-mirror-y", "image-rotate"]) {
  assert.ok(html.includes(`<button id="${id}"`), `${id} button`);
}
assert.ok(ui.includes("readExifOrientation"));
assert.ok(ui.includes("image_quantize_worker.js"));
assert.ok(ui.includes("PaperFramePfr1.packPfr1"));
assert.ok(ui.includes('applyImageTransform("mirror-x")'));
assert.ok(ui.includes('applyImageTransform("mirror-y")'));
assert.ok(ui.includes('applyImageTransform("rotate-90-cw")'));
assert.ok(ui.includes("imageSelectionRevision"));
assert.ok(ui.includes("selectionRevision !== imageSelectionRevision"));
assert.ok(ui.includes("cancelQuantizeWorker"));
assert.ok(html.includes('class="image-transform-buttons"'));
assert.ok(html.includes('id="image-library"'));
assert.ok(html.includes('class="top-navigation"'));
assert.ok(!html.includes('id="image-mirror-x" type="checkbox"'));
assert.ok(css.includes(".top-navigation .nav-link { width: auto; min-width: 88px; text-align: center; }"));
assert.ok(ui.includes('fetch("/api/v1/images"'));
assert.ok(ui.includes("renderImageLibrary"));
assert.ok(ui.includes("encodeURIComponent(image.name)"));
assert.ok(ui.includes("imageLibraryRevision"));
assert.ok(ui.includes('"X-CSRF-Token": csrfToken'));
assert.ok(ui.includes("uploadImagePfr1"));
assert.ok(ui.includes("mutateImageLibrary"));
assert.ok(ui.includes("reorderImage"));
assert.ok(ui.includes("/api/v1/images/order"));
assert.ok(ui.includes("normalizeCropPosition"));
assert.ok(ui.includes("pointerdown"));
assert.ok(ui.includes("pointerup"));
assert.ok(ui.includes("pointercancel"));
assert.ok(ui.includes('fetch("/api/v1/config"'));
assert.ok(ui.includes('fetch("/api/v1/config", {'));
assert.ok(ui.includes("64 * 1024 * 1024"));
assert.ok(html.includes('value="floyd-steinberg"'));
assert.ok(html.includes('value="atkinson"'));
assert.ok(!html.includes('value="nearest"'));
assert.ok(!html.includes('value="bayer-4x4"'));
assert.ok(!ui.includes('imageSourceRaster'));
assert.ok(ui.includes("data-image-action"));
assert.ok(css.includes(".image-library-actions .danger-button"));
console.log("image_ui_contract: 13 tests passed");
