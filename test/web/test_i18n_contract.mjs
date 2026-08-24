import assert from "node:assert/strict";
import fs from "node:fs";

const i18nSrc = fs.readFileSync("data/web/i18n.js", "utf8");
const html = fs.readFileSync("data/web/index.html", "utf8");
const js = fs.readFileSync("data/web/ui.js", "utf8");

// i18n.js is a plain IIFE with no exports, so this reads its dict object as
// text rather than importing it -- same approach as
// test_sensor_form_contract.mjs takes with ui.js's submit handler.
function extractTable(lang) {
  // Object keys that are valid bare identifiers (e.g. `en`) don't need
  // quoting, so i18n.js writes `en: {` but `"zh-Hant": {` (a hyphen isn't
  // a valid identifier character) -- check both forms.
  const quoted = `"${lang}": {`;
  const bare = `${lang}: {`;
  let start = i18nSrc.indexOf(quoted);
  let markerLength = quoted.length;
  if (start < 0) {
    start = i18nSrc.indexOf(bare);
    markerLength = bare.length;
  }
  assert.ok(start > 0, `expected a "${lang}" table in data/web/i18n.js`);
  const bodyStart = start + markerLength;
  let depth = 1;
  let i = bodyStart;
  while (depth > 0 && i < i18nSrc.length) {
    if (i18nSrc[i] === "{") depth++;
    else if (i18nSrc[i] === "}") depth--;
    i++;
  }
  assert.equal(depth, 0, `unbalanced braces reading the "${lang}" table`);
  const body = i18nSrc.slice(bodyStart, i - 1);
  return new Set([...body.matchAll(/"([a-zA-Z0-9_.]+)":\s*"/g)].map((m) => m[1]));
}

const zhKeys = extractTable("zh-Hant");
const enKeys = extractTable("en");
assert.ok(zhKeys.size > 0, "expected a non-empty zh-Hant dictionary");

// Every key must have both a zh-Hant and an en translation -- a language
// toggle that silently falls back to the wrong language for some UI text is
// worse than an obviously-missing string.
const missingFromEn = [...zhKeys].filter((k) => !enKeys.has(k));
assert.deepEqual(
  missingFromEn,
  [],
  `zh-Hant keys missing an "en" translation: ${missingFromEn.join(", ")}`,
);
const missingFromZh = [...enKeys].filter((k) => !zhKeys.has(k));
assert.deepEqual(
  missingFromZh,
  [],
  `en keys missing a "zh-Hant" translation: ${missingFromZh.join(", ")}`,
);

// Every data-i18n / data-i18n-aria-label / data-i18n-placeholder /
// data-i18n-title attribute in index.html must resolve to a real dictionary
// key, or the language toggle silently shows the raw key string instead of
// translated text. applyI18n() in i18n.js only ever queries these four
// attribute forms, so also reject any other data-i18n-* suffix outright: it
// would have a valid-looking key but never actually get applied by the
// runtime, which this loop alone couldn't tell apart from a real miss.
const knownAttributes = new Set([
  "data-i18n",
  "data-i18n-aria-label",
  "data-i18n-placeholder",
  "data-i18n-title",
]);
const htmlKeys = new Set();
for (const m of html.matchAll(/(data-i18n(?:-[a-z-]+)?)="([a-zA-Z0-9_.]+)"/g)) {
  const [, attribute, key] = m;
  assert.ok(
    knownAttributes.has(attribute),
    `index.html uses "${attribute}", which applyI18n() never queries -- ` +
      `typo, or a new attribute form that needs wiring into i18n.js first`,
  );
  htmlKeys.add(key);
}
assert.ok(htmlKeys.size > 0, "expected data-i18n attributes in index.html");
const htmlMissing = [...htmlKeys].filter((k) => !zhKeys.has(k));
assert.deepEqual(
  htmlMissing,
  [],
  `index.html references data-i18n key(s) missing from the dictionary: ${htmlMissing.join(", ")}`,
);

// Every t("key", ...) call site in ui.js must also resolve. This is only
// possible to check with a static regex because t() is only ever called
// with a static string-literal key -- never a computed/template-literal key
// -- by convention (see i18n.js's own comment on this constraint). A few
// call sites pick the key with a ternary (e.g.
// `t(display.random ? "a.b" : "a.c", { ... })`), so this scans each call's
// whole balanced-paren argument list rather than just the text right after
// "t(" -- restricted to dotted key-shaped strings (translation keys are
// always namespaced with a dot; plain fallback literals passed as vars,
// like "unknown" or "—", never contain one) so it doesn't also sweep up
// unrelated string arguments as if they were keys.
const jsKeys = new Set();
for (const call of js.matchAll(/\bt\(/g)) {
  let depth = 1;
  let i = call.index + call[0].length;
  while (depth > 0 && i < js.length) {
    if (js[i] === "(") depth++;
    else if (js[i] === ")") depth--;
    i++;
  }
  const args = js.slice(call.index + call[0].length, i - 1);
  for (const m of args.matchAll(/"([a-zA-Z0-9_]+(?:\.[a-zA-Z0-9_]+)+)"/g)) {
    jsKeys.add(m[1]);
  }
}
assert.ok(jsKeys.size > 0, "expected t(\"key\") call sites in ui.js");
const jsMissing = [...jsKeys].filter((k) => !zhKeys.has(k));
assert.deepEqual(
  jsMissing,
  [],
  `ui.js calls t() with key(s) missing from the dictionary: ${jsMissing.join(", ")}`,
);

console.log(
  `i18n contract: ${zhKeys.size} keys, ${htmlKeys.size} data-i18n refs, ${jsKeys.size} t() call sites, all resolved`,
);
