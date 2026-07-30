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
  "weather-interval",
  "weather-location",
  "weather-units",
  "weather-language",
  "weather-ntp-server",
]) {
  assert.ok(html.includes(`id="${id}"`), `missing ${id}`);
}
assert.ok(js.includes("/api/v1/weather/config"));
assert.ok(js.includes('"X-CSRF-Token": csrfToken'));
console.log("weather_ui_contract: 3 checks passed");
