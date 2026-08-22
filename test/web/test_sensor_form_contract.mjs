import assert from "node:assert/strict";
import fs from "node:fs";

const html = fs.readFileSync("data/web/index.html", "utf8");
const js = fs.readFileSync("data/web/ui.js", "utf8");
const parser = fs.readFileSync(
  "components/pf_web/include/pf_web/sensor_config_form.hpp",
  "utf8",
);

// The sensor settings form is a plain application/x-www-form-urlencoded
// POST, so the field names are a string contract between ui.js and the C++
// parser. Nothing in either language checks the other: a rename on one side
// makes the device answer 400 unknown_field (or silently drop a field) at
// runtime, with no compile or test failure anywhere. This test is that
// missing check.
//
// parse_sensor_config_form() rejects anything it does not recognise, so the
// two sets have to match exactly in both directions.
const parserFields = new Set(
  [...parser.matchAll(/matches_field\("([a-z0-9_]+)"\)/g)].map(
    (match) => match[1],
  ),
);
assert.ok(
  parserFields.size > 0,
  "expected the C++ parser to declare fields via matches_field()",
);

// ui.js builds the body from one object literal plus the optional checkbox
// assignments; both forms are picked up here.
const submitStart = js.indexOf("const values = {\n      light1_threshold");
assert.ok(submitStart > 0, "expected the sensor form submit handler");
const submitEnd = js.indexOf("environmentSave.disabled = true", submitStart);
assert.ok(submitEnd > submitStart, "submit handler must be bounded");
const submitBlock = js.slice(submitStart, submitEnd);
const submittedFields = new Set([
  ...[...submitBlock.matchAll(/^\s{6}([a-z0-9_]+):/gm)].map(
    (match) => match[1],
  ),
  ...[...submitBlock.matchAll(/values\.([a-z0-9_]+) = "on"/g)].map(
    (match) => match[1],
  ),
]);

for (const field of parserFields) {
  assert.ok(
    submittedFields.has(field),
    `the firmware accepts "${field}" but ui.js never submits it`,
  );
}
for (const field of submittedFields) {
  assert.ok(
    parserFields.has(field),
    `ui.js submits "${field}" but the firmware rejects it as unknown_field`,
  );
}

// Both photoresistor channels are configured independently (ADR-0018), so
// the page needs its own control for each: sharing one threshold input
// would silently calibrate both sensors to the same value.
for (const id of [
  "light1-enabled",
  "light1-threshold",
  "light2-enabled",
  "light2-threshold",
]) {
  assert.ok(
    new RegExp(`id="${id}"`).test(html),
    `expected #${id} in the environment form`,
  );
}

// The threshold inputs must stay inside the firmware's accepted ADC range;
// a browser that submits 5000 gets a 422 with no explanation on the page.
const thresholdInputs = [
  ...html.matchAll(/id="light[12]-threshold"[^>]*>/g),
].map((match) => match[0]);
assert.equal(thresholdInputs.length, 2, "expected two threshold inputs");
for (const input of thresholdInputs) {
  assert.ok(/min="0"/.test(input), `${input} must set min="0"`);
  assert.ok(/max="4095"/.test(input), `${input} must set max="4095"`);
  assert.ok(/required/.test(input), `${input} must be required`);
}

// Live readings come back per channel; the page has to show each one and
// say which channel drove the presence decision, otherwise calibrating two
// sensors against one combined number is guesswork.
for (const id of [
  "light1-reading-status",
  "light1-raw",
  "light2-reading-status",
  "light2-raw",
  "light-deciding-channel",
]) {
  assert.ok(new RegExp(`id="${id}"`).test(html), `expected #${id} in markup`);
  assert.ok(
    js.includes(`$("#${id}")`),
    `expected ui.js to update #${id} from GET /api/v1/sensors`,
  );
}

console.log("sensor form contract: ok");
