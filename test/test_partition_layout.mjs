import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";

const path = "partitions/paperframe-dev.csv";
const bytes = await readFile(path);
const hash = createHash("sha256").update(bytes).digest("hex").toUpperCase();
assert.equal(
  hash,
  "427FD4144CAF3D8A2F0F0622317F42D6236EA2D7997BAFC42AF13C9CA8565870",
);

const rows = bytes.toString("utf8")
  .split(/\r?\n/)
  .map((line) => line.replace(/#.*/, "").trim())
  .filter(Boolean)
  .map((line) => line.split(",").map((field) => field.trim()).filter(Boolean));

const expected = [
  ["nvs", "data", "nvs", 0x9000, 0x4000],
  ["otadata", "data", "ota", 0xD000, 0x2000],
  ["phy_init", "data", "phy", 0xF000, 0x1000],
  ["ota_0", "app", "ota_0", 0x10000, 0x280000],
  ["ota_1", "app", "ota_1", 0x290000, 0x280000],
  ["webfs", "data", "spiffs", 0x510000, 0x100000],
  ["coredump", "data", "coredump", 0x610000, 0x20000],
  ["imagefs", "data", "spiffs", 0x630000, 0x9D0000],
];

assert.equal(rows.length, expected.length);
const parsed = rows.map((row, index) => {
  assert.equal(row.length, 5, `partition row ${index}`);
  const [name, type, subtype, offset, size] = row;
  return [name, type, subtype, Number.parseInt(offset, 16), Number.parseInt(size, 16)];
});
assert.deepEqual(parsed, expected);
assert.equal(new Set(parsed.map(([name]) => name)).size, parsed.length);
for (let index = 1; index < parsed.length; index += 1) {
  const previous = parsed[index - 1];
  const current = parsed[index];
  assert.equal(current[3], previous[3] + previous[4], `gap/overlap before ${current[0]}`);
}
const last = parsed.at(-1);
assert.equal(last[3] + last[4], 0x1000000);
console.log("partition_layout: 3 tests passed");
