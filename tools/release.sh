#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Philip Bos
# A release: every test, the firmware built and checked, the package put in the companion with its SHA-256 (the
# flasher installs it), and dist/ with the companion (one folder to serve or put on a site) and the package alone.
#   tools/release.sh 0.1        (needs the JieLi toolchain and the AC79 SDK: base/README.md)
set -e
V=${1:?usage: tools/release.sh VERSION}
cd "$(dirname "$0")/.."
[ -x .venv/bin/python ] && PATH=$PWD/.venv/bin:$PATH
export JIELI_TOOLCHAIN=${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain} AC79_SDK=${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}
tools/run_tests.sh
python3 base/tools/build.py app --release "$V" | grep -E "^  ok|^  FAIL|ERROR|^package"
PKG=app/build/jamtrack-$V.fwsc
[ -f "$PKG" ] || PKG=$(ls -t app/build/jamtrack-*.fwsc | head -1)
tools/companion_firmware.sh "$PKG" "$V"
rm -rf dist && mkdir -p dist
cp "$PKG" dist/
(cd dist && shasum -a 256 "$(basename "$PKG")" > SHA256SUMS)
cp -R companion "dist/jamtrack-companion-$V"
(cd dist && zip -qr "jamtrack-companion-$V.zip" "jamtrack-companion-$V" && rm -rf "jamtrack-companion-$V")
echo "dist/: $(ls dist | tr '\n' ' ')"
