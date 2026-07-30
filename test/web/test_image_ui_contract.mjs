import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const html = await readFile("data/web/index.html", "utf8");
const ui = await readFile("data/web/ui.js", "utf8");
const css = await readFile("data/web/style.css", "utf8");

assert.ok(html.includes('<script src="/image_pipeline.js" defer></script>'));
assert.ok(html.includes('<script src="/image_quantizer.js" defer></script>'));
assert.ok(html.includes('<script src="/image_pfr1.js" defer></script>'));
assert.ok(html.includes('data-view="image"'));
for (const id of [
  "image-source", "image-orientation", "image-fit", "image-dither", "image-filename",
  "image-mirror-x", "image-mirror-y", "image-rotate", "preview-original",
  "preview-processed", "preview-sixcolor", "preview-frame", "download-pfr1",
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
assert.ok(!html.includes('id="image-mirror-x" type="checkbox"'));
assert.ok(css.includes(".sidebar[hidden] + .content { grid-column: 1 / -1; }"));
assert.ok(!ui.includes("/api/v1/images"));
console.log("image_ui_contract: 2 tests passed");
