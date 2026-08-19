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

// StaticAsset variable -> the embedded symbol it serves, and the size symbol
// paired with it.
const assetTable = new Map(
    [...server.matchAll(
        /constexpr StaticAsset (\w+)\{\s*web_assets::(\w+),\s*&web_assets::(\w+),/g)]
        .map(([, variable, data, size]) => [variable, { data, size }]));

// Route URI -> the StaticAsset variable its user_ctx points at.
const routeTable = new Map(
    [...server.matchAll(
        /\.uri = "([^"]+)",\s*\.method = HTTP_GET,\s*\.handler = static_asset_handler,\s*\.user_ctx = const_cast<StaticAsset\*>\(&(\w+)\)/g)]
        .map(([, uri, variable]) => [uri, variable]));

// Checking each link of the chain separately would pass even if a route were
// wired to the wrong asset, so follow it end to end: filename -> expected
// symbol -> StaticAsset -> route.
for (const name of assets) {
    const symbol = `k${toIdentifier(name)}Gz`;
    const uri = name === "index.html" ? "/" : `/${name}`;

    const variable = routeTable.get(uri);
    assert.ok(variable, `${name} has no static-asset route (${uri})`);

    const wired = assetTable.get(variable);
    assert.ok(wired, `route ${uri} points at unknown StaticAsset ${variable}`);
    assert.equal(
        wired.data, symbol,
        `route ${uri} serves ${wired.data}, expected ${symbol}`);
    assert.equal(
        wired.size, `${symbol}Size`,
        `${variable} pairs ${wired.data} with ${wired.size}`);
}

// A route serving an asset that no longer exists would 404 just as silently.
assert.equal(
    routeTable.size, assets.length,
    `${routeTable.size} static-asset routes for ${assets.length} assets`);

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
