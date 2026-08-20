import assert from "node:assert/strict";
import fs from "node:fs";

const html = fs.readFileSync("data/web/index.html", "utf8");
const js = fs.readFileSync("data/web/ui.js", "utf8");

assert.ok(html.includes('data-view="weather"'));
assert.ok(html.includes('id="weather-view"'));
for (const id of [
  "weather-form",
  "weather-api-key",
  "weather-latitude",
  "weather-longitude",
  "weather-units",
  "weather-ntp-server",
  "weather-map",
  "weather-map-tiles",
  "weather-map-canvas",
  "weather-map-pin",
  "weather-map-zoom-in",
  "weather-map-zoom-out",
  "weather-map-mode",
  "timezone-form",
  "device-timezone",
]) {
  assert.ok(html.includes(`id="${id}"`), `missing ${id}`);
}
for (const id of ["weather-interval", "weather-location", "weather-language"]) {
  assert.ok(!html.includes(`id="${id}"`), `stale field still present: ${id}`);
}
assert.ok(!html.includes("10⁶"), "stale e6-scaled lat/lon label still present");
assert.ok(js.includes("/api/v1/weather/config"));
assert.ok(js.includes('"X-CSRF-Token": csrfToken'));
assert.ok(js.includes("tile.openstreetmap.org"));
assert.ok(js.includes("drawGraticule"));
assert.ok(js.includes("loadDeviceTimezone"));
console.log("weather_ui_contract: 6 checks passed");
