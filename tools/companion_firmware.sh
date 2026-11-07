#!/bin/sh
# SPDX-License-Identifier: MIT
# A package into the companion's flasher: companion/firmware/PACKAGE and manifest.json (its identity and SHA-256, which
# the flasher checks before installing).   tools/companion_firmware.sh app/build/jamtrack.fwsc dev
set -e
PKG=${1:?package}; V=${2:-dev}
cd "$(dirname "$0")/.."
mkdir -p companion/firmware
rm -f companion/firmware/*.fwsc
cp "$PKG" companion/firmware/
python3 - "$PKG" "$V" <<'PY'
import hashlib, json, sys, pathlib
p = pathlib.Path(sys.argv[1]); d = p.read_bytes()
blocks = [d[i * 0x30 + 0x2F] for i in range(20)]
product = "".join(chr((m - i - 1) & 0xFF) for i, m in enumerate(blocks) if m != 0x7D)
json.dump({"name": "JAMTRACK", "version": sys.argv[2], "file": p.name, "product": product,
           "sha256": hashlib.sha256(d).hexdigest(), "size": len(d)},
          open("companion/firmware/manifest.json", "w"), indent=1)
print(f"companion/firmware: {p.name} {product} sha256 {hashlib.sha256(d).hexdigest()[:16]}...")
PY
