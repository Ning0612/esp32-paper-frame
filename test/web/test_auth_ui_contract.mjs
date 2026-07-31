import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const ui = await readFile("data/web/ui.js", "utf8");

assert.ok(ui.includes("/api/v1/auth/login"));
assert.ok(!ui.includes("/api/v1/auth/login/status"));
console.log("auth_ui_contract: 1 test passed");
