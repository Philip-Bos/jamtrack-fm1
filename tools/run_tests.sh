#!/bin/sh
# SPDX-License-Identifier: MIT
# Every check that runs without an FM-1: the engine (with ASan and UBSan: an out-of-bounds read in a delay line is
# exactly what they catch), the engine at -O2 (timing), the app in fm1-base's host simulator (panel, saving, the user
# kit, MIDI clock). Needs cc and python3 (the venv for the simulator's fonts).
set -e
cd "$(dirname "$0")/.."
[ -x .venv/bin/python ] && PATH=$PWD/.venv/bin:$PATH
mkdir -p build/test
SAN="-fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all -fno-omit-frame-pointer -g"
W="-Wall -Wextra -Wno-unused-parameter -Wno-unused-function"
echo "== engine (ASan, UBSan)"
cc -O1 $SAN $W -DPLAT_HOST -Iapp/dsp -Ibase/include -DJT_TEST_NOTES=dsp_test_note app/dsp/engine.c app/tests/dsp_test.c -lm -o build/dsp_test_san
build/dsp_test_san | tail -1
echo "== engine (-O2)"
cc -O2 $W -DPLAT_HOST -Iapp/dsp -Ibase/include -DJT_TEST_NOTES=dsp_test_note app/dsp/engine.c app/tests/dsp_test.c -lm -o build/dsp_test
build/dsp_test build/test | tail -2
echo "== the companion's theory against the firmware's"
cc -DPLAT_HOST -Iapp/dsp -Iapp -Ibase/include app/dsp/engine.c tools/dump_tables.c -lm -o build/dump_tables
build/dump_tables > build/tables_full.json
node -e 'const fs=require("fs");const t=JSON.parse(fs.readFileSync("build/tables_full.json"));fs.writeFileSync("app/tests/chords.json",JSON.stringify(t.chordTests));fs.writeFileSync("app/tests/modes.json",JSON.stringify(t.modeTests));delete t.chordTests;delete t.modeTests;fs.writeFileSync("companion/lib/tables.json",JSON.stringify(t,null,1));'
node app/tests/theory_test.mjs
for f in companion/*.js companion/lib/*.js companion/flasher/*.js; do node --check "$f"; done
echo "== the app in the host simulator"
python3 base/tools/build.py app --host | tail -1
for t in basic clock; do
  app/build/host/jamtrack_host app/tests/$t.txt build/test > build/test/$t.log 2>&1 || true
  if grep -q FAIL build/test/$t.log; then grep FAIL build/test/$t.log; exit 1; fi
  echo "  $t: ok"
done
echo "all tests passed"
