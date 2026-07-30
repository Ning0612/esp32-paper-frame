import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const ui = await readFile("data/web/ui.js", "utf8");

assert.ok(ui.includes("Date.now() + 180000"));
console.log("auth_ui_contract: 1 test passed");
