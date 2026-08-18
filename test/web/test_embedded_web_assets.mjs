// The WebUI is compiled into the app image so that one OTA download updates
// firmware and frontend together (docs/adr/0016-embed-webui-assets-in-firmware.md).
// tools/generate_web_assets.py embeds every file in data/web/ automatically,
// but wiring each asset to a StaticAsset and an HTTP route is still manual --
// so adding a frontend file without registering it would ship a 404 that no
// build step catches. These checks close that gap, and pin the response
// headers the embedded path depends on.

import assert from "node:assert/strict";
import fs from "node:fs";

const server = fs.readFileSync(
    "components/pf_web/health_server.cpp", "utf8");

const assets = fs.readdirSync("data/web")
    .filter((name) => !name.startsWith("."))
    .sort();

assert.ok(assets.length > 0, "data/web must contain assets");

// Mirrors to_identifier() in tools/generate_web_assets.py: every run of
// non-alphanumeric characters becomes a word boundary, each word is
// capitalised. index.html -> IndexHtml, image_pfr1.js -> ImagePfr1Js.
const toIdentifier = (name) => name
    .replace(/[^0-9A-Za-z]+/g, " ")
    .trim()
    .split(" ")
    .map((word) => word.slice(0, 1).toUpperCase() + word.slice(1))
    .join("");

for (const name of assets) {
    const symbol = `k${toIdentifier(name)}Gz`;

    assert.ok(
        server.includes(`web_assets::${symbol},`),
        `${name} is embedded but never wired into a StaticAsset`);
    assert.ok(
        server.includes(`&web_assets::${symbol}Size,`),
        `${name} has no size wired into its StaticAsset`);

    const uri = name === "index.html" ? "/" : `/${name}`;
    assert.ok(
        server.includes(`.uri = "${uri}"`),
        `${name} has no HTTP route (${uri})`);
}

// Assets are stored gzipped and never decompressed on the device, so the
// response must say so or browsers receive unreadable bytes.
assert.ok(
    server.includes('"Content-Encoding"') && server.includes('"gzip"'),
    "static assets must be served with Content-Encoding: gzip");

// Content-Encoding belongs to the asset handler alone: set_common_headers is
// shared with send_json() and every JSON API response, none of which are
// compressed.
const commonHeaders = server.slice(
    server.indexOf("esp_err_t set_common_headers("),
    server.indexOf("esp_err_t send_json("));
assert.ok(
    !commonHeaders.includes("Content-Encoding"),
    "Content-Encoding must not be set for every response");
assert.ok(
    commonHeaders.includes('"no-store"'),
    "Cache-Control: no-store must stay");
assert.ok(
    commonHeaders.includes('"nosniff"'),
    "X-Content-Type-Options: nosniff must stay");

// The assets no longer come from a filesystem; these would be leftovers of
// the retired webfs partition.
assert.ok(
    !server.includes("webfs_unavailable"),
    "the webfs_unavailable error path must be gone");
assert.ok(
    !server.includes('"/web/'),
    "no webfs mount paths may remain");

console.log(
    `embedded_web_assets: ${assets.length} assets, checks passed`);
