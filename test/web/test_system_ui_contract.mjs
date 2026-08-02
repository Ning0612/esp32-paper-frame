import assert from "node:assert/strict";
import fs from "node:fs";

const html = fs.readFileSync("data/web/index.html", "utf8");
const js = fs.readFileSync("data/web/ui.js", "utf8");

assert.ok(html.includes('data-view="system"'));
assert.ok(!html.includes('class="nav-link future"'), "system nav must no longer be the disabled placeholder");
assert.ok(html.includes('id="system-view"'));
for (const id of [
  "system-refresh",
  "system-display-state",
  "system-display-outcome",
  "system-reboot-reason",
  "system-wifi-state",
  "system-internet-state",
  "system-sntp-state",
  "system-firmware-version",
  "system-uptime",
  "system-flash-capacity",
  "system-psram-capacity",
  "system-webfs-capacity",
  "system-imagefs-capacity",
  "system-ota-check-state",
  "system-ota-latest-version",
  "system-ota-update-state",
  "system-ota-progress",
  "system-ota-error",
  "system-ota-check",
  "system-ota-update",
  "system-ota-status",
  "system-password-reset-form",
  "system-new-password",
  "system-confirm-password",
  "system-password-reset",
  "system-password-reset-status",
  "system-reboot",
  "system-reboot-status",
  "system-events-list",
  "dashboard-current-image",
  "dashboard-next-refresh",
]) {
  assert.ok(html.includes(`id="${id}"`), `missing ${id}`);
}

for (const route of [
  "/api/v1/system/reboot",
  "/api/v1/system/ota/status",
  "/api/v1/system/ota/check",
  "/api/v1/system/ota/update",
  "/api/v1/events",
  "/api/v1/auth/password",
]) {
  assert.ok(js.includes(route), `missing fetch of ${route}`);
}
assert.ok(js.includes('"X-CSRF-Token": csrfToken'));
assert.ok(js.includes("loadSystemStatus"));
assert.ok(html.includes('href="https://github.com/Ning0612/esp32-paper-frame"'));
assert.ok(html.includes("Ning0612/esp32-paper-frame repo source"));

// Removed 2026-08-01: on-hardware crash inside Espressif's WiFi blob when
// forcing AP+STA combo mode while STA is already connected (see
// docs/hardware/VALIDATION.md), and it didn't serve a real need -- the
// pre-existing automatic STA-unreachable-fallback-AP path already covers
// the actual use case.
assert.ok(!html.includes('id="system-recovery-ap"'), "recovery-ap button must stay removed");
assert.ok(!js.includes("/api/v1/system/recovery-ap"), "recovery-ap fetch must stay removed");

console.log("system_ui_contract: checks passed");
