import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const html = await readFile("data/web/index.html", "utf8");
const ui = await readFile("data/web/ui.js", "utf8");
const css = await readFile("data/web/style.css", "utf8");
const previewBlocks = html.match(/<article[^>]*class="panel preview-card[^>]*>[\s\S]*?<\/article>/g) || [];

assert.ok(html.includes('<script src="/image_pipeline.js" defer></script>'));
assert.ok(html.includes('<script src="/image_quantizer.js" defer></script>'));
assert.ok(html.includes('<script src="/image_pfr1.js" defer></script>'));
assert.ok(html.includes('data-view="image"'));
assert.ok(html.includes('id="top-navigation"'));
assert.equal(previewBlocks.length, 3);
assert.ok(html.indexOf("<h3>原圖</h3>") < html.indexOf("<h3>處理後</h3>"));
assert.ok(html.indexOf("<h3>處理後</h3>") < html.indexOf("<h3>面板畫面</h3>"));
for (const id of [
  "image-source", "image-source-dropzone", "image-source-drop-hint", "image-orientation", "image-fit", "image-dither", "image-filename",
  "image-mirror-x", "image-mirror-y", "image-rotate", "preview-original",
  "preview-processed", "preview-frame", "download-pfr1",
  "upload-pfr1",
  "image-library-refresh", "image-library-status", "image-library-list",
  "image-processed-card", "image-crop-hint", "image-crop-controls", "image-crop-zoom",
  "image-crop-zoom-value", "image-crop-zoom-hint", "image-carousel-form",
  "image-carousel-random", "image-carousel-refresh-minutes", "image-carousel-save", "image-carousel-status",
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
assert.ok(css.includes(".preview-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr));"));
assert.ok(html.includes('role="button" tabindex="0"'));
assert.ok(ui.includes("dataTransfer.files"));
assert.ok(ui.includes('addEventListener("dragover"'));
assert.ok(ui.includes('addEventListener("drop"'));
assert.ok(ui.includes("is-drag-over"));
assert.ok(css.includes(".image-source-dropzone"));
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
assert.ok(ui.includes("normalizeCropZoom"));
assert.ok(ui.includes("cropZoom"));
assert.ok(ui.includes("viewportWidth"));
assert.ok(ui.includes("Math.min(1, Math.max(0, next.x))"));
assert.ok(ui.includes("updateCropInteraction();\n    renderProcessedPreview();"));
assert.ok(ui.includes("pointerdown"));
assert.ok(ui.includes("pointerup"));
assert.ok(ui.includes("pointercancel"));
assert.ok(ui.includes('fetch("/api/v1/config"'));
assert.ok(ui.includes('fetch("/api/v1/config", {'));
assert.ok(html.includes('id="image-carousel-refresh-minutes" type="number" min="10" max="1440"'));
assert.ok(ui.includes('refresh_minutes: String(refreshMinutes)'));
assert.ok(ui.includes('const defaultCarouselRefreshMinutes = 30;'));
assert.ok(ui.includes("64 * 1024 * 1024"));
assert.ok(html.includes('value="floyd-steinberg"'));
assert.ok(html.includes('value="atkinson"'));
assert.ok(!html.includes('value="nearest"'));
assert.ok(!html.includes('value="bayer-4x4"'));
assert.ok(!html.includes('id="preview-sixcolor"'));
assert.ok(!ui.includes("previewSixColor"));
assert.ok(!html.includes("方向鍵微調"));
assert.ok(!ui.includes("handleCropKeydown"));
assert.ok(!ui.includes("previewProcessed.addEventListener(\"keydown\""));
assert.ok(!ui.includes('imageSourceRaster'));
assert.ok(ui.includes("data-image-action"));
assert.ok(css.includes(".image-library-actions .danger-button"));
assert.ok(ui.includes("config_read_only"),
  "a refused save must be distinguished from a storage fault");

console.log("image_ui_contract: 30 tests passed");
