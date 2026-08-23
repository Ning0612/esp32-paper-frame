import assert from "node:assert/strict";
import fs from "node:fs";

// scripts/verify-like-ci.sh exists so a change can be verified locally the
// way CI will verify it. That only holds while the two agree, and nothing
// makes them agree: they are separate files edited for separate reasons.
//
// The failure this guards against already happened once. On 2026-08-23 the
// v0.10.1 release build failed on -Werror in test_embedded/, because the
// local check had been `pio run` -- which compiles the firmware and not the
// embedded test sources. The script was written to close that gap; this
// test is what stops the gap reopening the next time a workflow step is
// added and the script is not.

// The script reaches PlatformIO through a $PIO variable and CI through
// `python -m platformio`; expand the assignments so both sides compare on
// the same subcommands rather than on how the binary is spelled.
const rawScript = fs.readFileSync("scripts/verify-like-ci.sh", "utf8");
const script = [...rawScript.matchAll(/^(\w+)=(\S+)$/gm)].reduce(
  (text, [, name, value]) => text.replaceAll(`$${name}`, value),
  rawScript,
);
const workflows = ["ci.yml", "release.yml"].map((name) => ({
  name,
  text: fs.readFileSync(`.github/workflows/${name}`, "utf8"),
}));

// Commands are folded across lines in YAML (`run: >-`) and split across
// shell continuations, so compare on whitespace-normalised text.
const flatten = (text) => text.replace(/\s+/g, " ");

// Every verification action either side performs, reduced to a comparable
// label. Anything a workflow does and the script does not is a gap.
function actions(text) {
  const flat = flatten(text);
  const found = new Set();

  // Host suites: `platformio test -e native`, no project conf.
  for (const m of flat.matchAll(
    /(?:platformio|pio)(?:\.exe)? test (?!--project-conf)-e ([\w-]+)/g,
  )) {
    found.add(`host-test:${m[1]}`);
  }

  // Embedded suites, which are the ones that only get compiled here.
  // The filter is part of the identity: the panel driver suite is reached
  // only by an explicit -f, so a run without it does not cover it.
  for (const m of flat.matchAll(
    /(?:platformio|pio)(?:\.exe)? test --project-conf (\S+) -e ([\w-]+)((?: -f [\w-]+)?)/g,
  )) {
    const filter = m[3].trim().replace(/^-f /, "") || "*";
    found.add(`embedded:${m[1]}:${m[2]}:${filter}`);
  }

  for (const m of flat.matchAll(/(?:platformio|pio)(?:\.exe)? run -e ([\w-]+)/g)) {
    found.add(`firmware:${m[1]}`);
  }

  if (/node --check/.test(flat)) found.add("node-syntax");
  if (/for \w+ in test\/web\/\*\.mjs/.test(flat)) found.add("web-contract");
  if (/for \w+ in test\/\*\.mjs/.test(flat)) found.add("repo-contract");
  if (/test\/test_active_ota_upload\.py/.test(flat)) found.add("ota-tooling");

  return found;
}

const scriptActions = actions(script);

// Sanity: a regex that matches nothing would make this test vacuous.
assert.ok(
  scriptActions.size >= 8,
  `expected verify-like-ci.sh to perform several checks, parsed ${
    scriptActions.size
  }: ${[...scriptActions].join(", ")}`,
);

for (const { name, text } of workflows) {
  const wanted = actions(text);
  assert.ok(
    wanted.size > 0,
    `parsed no verification steps out of ${name}; the regexes have gone stale`,
  );
  for (const action of wanted) {
    assert.ok(
      scriptActions.has(action),
      `${name} runs "${action}" but scripts/verify-like-ci.sh does not. ` +
        `Add it to the script, or local verification will keep passing on ` +
        `something CI rejects.`,
    );
  }
}

// The whole point is that `pio run` alone is not enough: the embedded test
// sources are compiled by `platformio test`, and that environment builds
// with -Werror. Assert the script actually reaches them.
const embedded = [...scriptActions].filter((a) => a.startsWith("embedded:"));
assert.ok(
  embedded.length >= 2,
  `verify-like-ci.sh must compile the embedded test sources, found: ${
    embedded.join(", ") || "none"
  }`,
);

console.log(
  `ci parity: ${scriptActions.size} checks, covering both workflows`,
);
