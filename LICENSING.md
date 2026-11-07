# Licensing

| What | Licence | Where |
| --- | --- | --- |
| JAMTRACK FM-1: the app, the engine, the tools, the tests | MIT | `app/`, `tools/`, `docs/` |
| The factory drum samples (synthesized by `tools/make_kits.py`; nothing recorded) | CC0-1.0 | `app/dsp/kits_data.h` |
| The companion app's own code | MIT | `companion/*.js`, `companion/lib/`, `companion/*.html`, `*.css` |
| From ECHOFIELD (MIT, the same author): the sample codec, the stylesheet, the flasher, the screen page, the kit's bank design | MIT | `companion/lib/efcodec.js`, `companion/app.css`, `companion/flasher/`, `companion/screen.js`, `app/kit.c`, `app/dsp/adpcm.h` |
| fm1-base (platform, simulator, build) and felucca-bsp (chip, board, update loader) | MPL-2.0 | `base/` (git submodule, pinned) |
| fm1-base's installer, package, port and mirror code, unchanged | MPL-2.0 | `companion/vendor/fm1-base/` (`UPSTREAM_COMMIT`, `LICENSE-MPL-2.0`) |
| JieLi AC79 SDK files the package build takes | Apache-2.0 | fetched by the build, never committed |
| Fonts in the firmware (fm1-base's: Barlow) | SIL OFL 1.1 | `base/assets/fonts/` |

MPL-2.0 applies file by file: changes to those files are published under MPL-2.0; JAMTRACK's own files stay MIT. A
firmware package (`.fwsc`) contains all of the above compiled together; its licence texts are these files.

No recorded drum sample, no other firmware's code and no other product's patterns are in JAMTRACK. Kit names that
recall classic drum machines (LM-80, RHYTHM BOX, 808) describe a character; the sounds are made from scratch.
