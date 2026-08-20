import assert from "node:assert/strict";
import fs from "node:fs";

const html = fs.readFileSync("data/web/index.html", "utf8");
const css = fs.readFileSync("data/web/style.css", "utf8");
const js = fs.readFileSync("data/web/ui.js", "utf8");
const navigationStart = html.indexOf('<nav id="top-navigation"');
const navigationEnd = html.indexOf("</nav>", navigationStart);
const navigation = html.slice(navigationStart, navigationEnd);

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
  "system-imagefs-capacity",
  "system-ota-check-state",
  "system-ota-latest-version",
  "system-ota-update-state",
  "system-ota-progress",
  "system-ota-progress-bar",
  "system-ota-progress-fill",
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
const pageNumbers = [...html.matchAll(/<p class="eyebrow">(\d{2}) \/ /g)]
  .map(([, number]) => number)
  .sort();
assert.deepEqual(pageNumbers, ["01", "02", "03", "04", "05", "06"]);
assert.ok(html.includes("05 / ENVIRONMENT &amp; PRESENCE"));
assert.ok(html.includes("06 / SYSTEM &amp; FIRMWARE"));
const navigationPages = [...navigation.matchAll(/data-view="([^"]+)"[^>]*>([^<]+) <span>(\d{2})<\/span><\/button>/g)]
  .map(([, view, label, number]) => [view, label.trim(), number]);
assert.deepEqual(navigationPages, [
  ["dashboard", "總覽", "01"],
  ["wifi", "Wi‑Fi", "02"],
  ["weather", "天氣", "03"],
  ["image", "圖片", "04"],
  ["environment", "環境", "05"],
  ["system", "系統", "06"],
]);
for (const [label, headingMarkup] of [
  ["A", "<h3>電子紙與輪播</h3>"], ["B", "<h3>容量與服務</h3>"], ["C", "<h3>後續模組</h3>"],
  ["A", "<h3>附近網路</h3>"], ["B", "<h3>連線設定</h3>"],
  ["A", "<h3>OpenWeatherMap</h3>"], ["B", "<h3>拖曳地圖選點</h3>"], ["C", "<h3>時區</h3>"], ["D", "<h3>安全狀態</h3>"],
  ["A", "<h3>感測器設定</h3>"], ["B", "<h3>即時讀值</h3>"],
  ["A", "<h3>面板與刷新</h3>"], ["B", "<h3>網路</h3>"], ["C", "<h3>容量與版本</h3>"],
  ["D", "<h3>OTA 韌體更新</h3>"], ["E", "<h3>重設管理密碼</h3>"], ["F", "<h3>系統控制與最近事件</h3>"],
  ["A", "<h3>轉換設定</h3>"], ["B", "<h3>PFR1 輸出</h3>"],
  ["C", "<h3 id=\"image-carousel-settings-title\">輪播設定</h3>"],
  ["D", "<h3 id=\"image-library-title\">裝置圖片庫</h3>"],
]) {
  assert.ok(html.includes(`>${label}</span>${headingMarkup}`), `missing panel label ${label} for ${headingMarkup}`);
}
assert.ok(html.includes('class="panel-no auth-marker coral">AUTH</span>'));
assert.ok(css.includes(".panel-no.auth-marker { width: 54px;"));
assert.equal((html.match(/class="system-action-group"/g) || []).length, 2);
assert.ok(css.includes(".system-action-group {"));
assert.ok(css.includes(".system-action-group .primary-button { width: auto; margin-top: 0; }"));

// Removed 2026-08-01: on-hardware crash inside Espressif's WiFi blob when
// forcing AP+STA combo mode while STA is already connected (see
// docs/hardware/VALIDATION.md), and it didn't serve a real need -- the
// pre-existing automatic STA-unreachable-fallback-AP path already covers
// the actual use case.
assert.ok(!html.includes('id="system-recovery-ap"'), "recovery-ap button must stay removed");
assert.ok(!js.includes("/api/v1/system/recovery-ap"), "recovery-ap fetch must stay removed");

// The webfs partition is retired: the WebUI is compiled into the app image so
// that one OTA updates firmware and frontend together
// (docs/adr/0016-embed-webui-assets-in-firmware.md). Nothing may report the
// capacity of a filesystem that is no longer mounted.
assert.ok(!html.includes('id="system-webfs-capacity"'), "system webfs capacity must stay removed");
assert.ok(!html.includes('id="webfs-capacity"'), "dashboard webfs capacity must stay removed");
assert.ok(!js.includes("webfs_total_bytes"), "webfs storage fields must stay removed");

// The bar is decorative; the percentage must stay readable as text, and the
// bar has to carry its value for assistive tech rather than only visually.
assert.ok(css.includes(".progress-fill"), "progress bar needs a fill style");
assert.ok(html.includes('role="progressbar"'), "progress bar needs its role");
assert.ok(js.includes('setAttribute("aria-valuenow"'), "progress must update aria-valuenow");
// One helper owns the bar so the width, the label and aria-valuenow cannot
// drift apart, and an unknown percentage must stay unknown rather than 0.
assert.ok(js.includes("const setOtaProgress ="), "bar updates must go through one helper");
assert.ok(js.includes("Number.isFinite(otaPercentRaw)"), "a malformed percentage must not become 0");
assert.ok(js.includes('removeAttribute("aria-valuenow")'), "unknown progress must drop aria-valuenow");
assert.ok(!js.includes("Number(ota.progress_percent) || 0"), "progress must not fall back to 0");

console.log("system_ui_contract: checks passed");
