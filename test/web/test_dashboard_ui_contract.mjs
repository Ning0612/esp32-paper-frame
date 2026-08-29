import assert from "node:assert/strict";
import fs from "node:fs";

const html = fs.readFileSync("data/web/index.html", "utf8");
const js = fs.readFileSync("data/web/ui.js", "utf8");

// ui.js reaches into the DOM through $("#id") (a querySelector wrapper) and
// assigns .textContent directly. A selector that no longer matches yields
// null, and the resulting TypeError aborts the rest of the render function --
// so every field *after* the stale one silently stops updating. Every id ui.js
// looks up must therefore exist in the served markup.
//
// This assumes ui.js only queries elements that are present in index.html; it
// does not build any id-addressed element at runtime. If that ever changes,
// exempt the dynamically created ids explicitly rather than deleting the check.
const selectorIds = [...js.matchAll(/\$\("#([A-Za-z0-9_-]+)"\)/g)].map(
  (match) => match[1],
);
assert.ok(
  selectorIds.length > 0,
  "expected ui.js to query elements through $(\"#id\")",
);
const markupIds = new Set(
  [...html.matchAll(/id="([A-Za-z0-9_-]+)"/g)].map((match) => match[1]),
);
for (const id of new Set(selectorIds)) {
  assert.ok(
    markupIds.has(id),
    `ui.js queries #${id} but index.html has no such element`,
  );
}

// The dashboard's runtime cards must not mix hard-coded prose in with values
// ui.js keeps current: the two drift apart silently. This card previously
// carried a literal "尚未接入" for SNTP while #dashboard-sntp -- on the same
// screen -- reported the real "synced" state, so the page contradicted itself.
const followUpCardStart = html.indexOf("後續模組");
assert.ok(followUpCardStart > 0, "expected the follow-up modules card");
const followUpCardEnd = html.indexOf("</article>", followUpCardStart);
assert.ok(followUpCardEnd > followUpCardStart, "card must be closed");
const followUpCard = html.slice(followUpCardStart, followUpCardEnd);
const definitions = followUpCard.match(/<dd[^>]*>/g) ?? [];
assert.ok(definitions.length > 0, "expected definitions in the card");
for (const definition of definitions) {
  assert.ok(
    / id="/.test(definition),
    `follow-up module card has a hard-coded value (${definition}); ` +
      "give it an id and update it from the runtime snapshot instead",
  );
}

// Sensor readings are optional by contract: a missing or disabled sensor must
// surface as an explicit state, never as a fabricated value.
for (const id of ["sensor-state", "light-sensor-state"]) {
  assert.ok(markupIds.has(id), `expected #${id} in the dashboard`);
  assert.ok(
    js.includes(`$("#${id}")`),
    `expected ui.js to update #${id} from the runtime snapshot`,
  );
}
assert.ok(
  js.includes("labelSensorStatus((data.sensors || {}).light_status)"),
  "light sensor state must come from the snapshot's light_status",
);
for (const status of [
  "disabled",
  "online",
  "not_detected",
  "low_clipped",
  "high_clipped",
  "error",
]) {
  assert.ok(
    new RegExp(`\\b${status}:`).test(js),
    `labelSensorStatus must label the firmware's "${status}" light status`,
  );
}

console.log("dashboard ui contract: ok");
