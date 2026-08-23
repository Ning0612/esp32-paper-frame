#!/usr/bin/env bash
# Mirror of the CI and release workflow verification steps, in order.
# `pio run` builds only the firmware; the embedded test sources are compiled
# by `pio test --without-uploading --without-testing`, which is why running
# `pio run` alone let a -Werror failure through to CI.
set -u
cd /d/Workspace/repos/esp32-paper-frame || exit 1
PIO=./.venv/Scripts/pio.exe
PY=./.venv/Scripts/python.exe
fail=0

step() {
  local name="$1"; shift
  printf '%-52s' "$name"
  if out=$("$@" 2>&1); then
    echo "OK"
  else
    echo "FAIL"
    echo "$out" | tail -12
    fail=1
  fi
}

step "native tests" $PIO test -e native
step "embedded: runtime coordinator" $PIO test --project-conf platformio-embedded.ini -e paperframe-s3-embedded-test --without-uploading --without-testing
step "embedded: display task" $PIO test --project-conf platformio-embedded.ini -e paperframe-s3-display-test --without-uploading --without-testing
step "embedded: panel driver" $PIO test --project-conf platformio-embedded.ini -e paperframe-s3-embedded-test -f test_epd7in3e_driver --without-uploading --without-testing

printf '%-52s' "WebUI JavaScript syntax"
js_fail=0
for f in data/web/*.js; do node --check "$f" >/dev/null 2>&1 || { echo "FAIL $f"; js_fail=1; fail=1; }
done
[ "$js_fail" -eq 0 ] && echo "OK"

printf '%-52s' "WebUI contract tests"
web_fail=0
for t in test/web/*.mjs; do node "$t" >/dev/null 2>&1 || { echo "FAIL $t"; web_fail=1; fail=1; }
done
[ "$web_fail" -eq 0 ] && echo "OK"

printf '%-52s' "repository contract tests"
repo_fail=0
for t in test/*.mjs; do node "$t" >/dev/null 2>&1 || { echo "FAIL $t"; repo_fail=1; fail=1; }
done
[ "$repo_fail" -eq 0 ] && echo "OK"

printf '%-52s' "PlatformIO tooling tests"
if PYTHONPATH=. $PY test/test_active_ota_upload.py >/dev/null 2>&1; then echo "OK"; else echo "FAIL"; fail=1; fi

step "firmware build" $PIO run -e paperframe-s3

echo
[ "$fail" -eq 0 ] && echo "ALL CI-EQUIVALENT STEPS PASSED" || echo "SOME STEPS FAILED"
exit "$fail"
