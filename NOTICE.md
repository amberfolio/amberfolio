# Third-party notices

Amber Folio is distributed under AGPL-3.0-only
([LICENSE](LICENSE)). This file acknowledges the third-party work the
project depends on or has learned from, and states each item's licence.

Two things it is *not*. It is not the clean-content rule — that is about
material from the original games and lives in
[CONTRIBUTING.md](CONTRIBUTING.md); nothing on this page comes near it.
And it is not a list of vendored code: **no third-party source is
committed to this repository**. Everything below is either fetched at
build time by CMake, fetched by a script into a cache outside the source
tree, or was read as a reference and not copied at all.

Every licence listed here is compatible with AGPL-3.0-only outbound, which
is the standing requirement for anything this project links or ships
(CLAUDE.md, "License compatibility").

---

## Fetched at build time

Declared with `FetchContent` in `cmake/`, pinned to the tag shown, built
from source, never committed here.

| Component | Version | Licence | What it is for |
| --- | --- | --- | --- |
| [GoogleTest](https://github.com/google/googletest) | v1.18.0 | BSD-3-Clause | the unit-test and conformance harnesses |
| [SDL3](https://github.com/libsdl-org/SDL) | release-3.4.14 | zlib | the desktop host's window, audio and input |
| [simdjson](https://github.com/simdjson/simdjson) | v4.6.7 | Apache-2.0 | parsing the condensed CPU conformance vectors |
| [libdeflate](https://github.com/ebiggers/libdeflate) | v1.25 | MIT | decompressing them, and inflating the journal's image streams |

SDL3 and libdeflate have a bearing on a shipped binary; GoogleTest and
simdjson are test-only. SDL3 is linked into the desktop host. libdeflate
was test-only until M5-E3 (#174) gave the journal's extractor a use for
it, and it is now linked into both hosts — decompression only, so the
half of it that writes a stream is not built at all. The pins live in
`cmake/AmberfolioGoogleTest.cmake`, `cmake/AmberfolioSDL3.cmake`,
`cmake/AmberfolioLibdeflate.cmake` and `cmake/AmberfolioConformance.cmake`;
the version numbers above are those files' defaults and those files are
authoritative if the two ever disagree.

The WebAssembly host is built with
[Emscripten](https://emscripten.org/) (MIT / University of Illinois
NCSA), pinned in `.emscripten-version`. It is a toolchain rather than a
dependency, but the JavaScript runtime glue it generates is part of the
deployed wasm host, and that glue is Emscripten's own MIT-licensed code.

The format and lint gates use clang-format and clang-tidy from
[LLVM](https://llvm.org/) (Apache-2.0 with LLVM exceptions), pinned in
`.llvm-version` and installed from PyPI. They never become part of a
build.

---

## The OCR engines (M5-E3, #174)

Reading a player's own Adventurer's Journal needs an OCR engine.
[Tesseract](https://github.com/tesseract-ocr/tesseract) — **Apache-2.0**
— is the one, on both hosts, and neither host links it:

* the desktop host **runs the player's own installed `tesseract`** as a
  program. It is not fetched, not vendored and not linked, so nothing is
  combined with anything; `.tesseract-version` records the version this
  host is written against, and the version actually used is asked of the
  engine at ingestion. `hosts/sdl/src/tesseract_ocr.h` has the reasoning;
* the web host uses
  [tesseract.js](https://github.com/naptha/tesseract.js) — **Apache-2.0**,
  with `tesseract.js-core` and the
  [tessdata_fast](https://github.com/tesseract-ocr/tessdata) language
  data, both Apache-2.0 — pinned in `.tesseract-js-version` and fetched
  by `scripts/fetch-ocr-engine.py` into the served directory. Never
  committed here, and **never fetched from a CDN by the deployed page**:
  `docs/journal.md` §5 is that decision and its reasons.

Neither is required. A build with no engine locates and decodes every
entry, keeps no text, and says so.

---

## The CPU conformance oracle

[SingleStepTests/8088](https://github.com/SingleStepTests/8088) — **MIT** —
is the authority M1's CPU core is validated against: JSON test vectors
captured from real Intel 8088 silicon, one file per opcode, around ten
thousand cases each.

It is pinned to commit `aea84484abc79d09639d855b7b0ab32bc9e4dbeb` (suite
version 2.0.1, the `v2` set) and **never committed to this repository**.
`scripts/fetch-conformance-vectors.py` clones it, condenses each file, and
writes the result into a cache directory outside the source tree; the
clone is deleted afterwards. Neither the original vectors nor the
condensed form are part of any Amber Folio distribution.

The condenser drops the per-cycle bus trace this emulator has no use for,
keeping the initial and final architectural state — and, for the eight
port opcodes, the port transactions mined out of the trace. That
transformation is ours; the data it transforms is the suite's.

---

## Reference sources for 8086 behaviour

The rest of this page is about *facts*, not code. Several 8086 behaviours
that Intel documents as "undefined" are nothing of the kind on real
silicon — they are the residue of the microcode's own internal ALU steps,
and matching them bit-for-bit is a settled requirement of M1 (issue #35,
and `docs/cpu-implementation.md` §5). Working out what those steps are
meant reading published reverse-engineering work. The sources are
acknowledged here and in the header comment of each file that used one.

**[Ken Shirriff](https://www.righto.com/), "Reverse-engineering the
multiplication algorithm in the Intel 8086 processor" (righto.com, 2023)**
— a description, from the die, of what the 8086's multiply microcode does.
Used for `core/src/cpu/instructions/mul.cpp`, where it explains why
MUL and IMUL leave different values in the officially-undefined SF, ZF, PF
and AF: the two end with a different final ALU operation. Prose about
hardware, not code; nothing was copied, and the behavioural rule taken
from it was independently checked against all 40,000 vectors of F6.4,
F6.5, F7.4 and F7.5 before it was implemented.

**[MartyPC](https://github.com/dbalsom/martypc) — MIT** — a
cycle-accurate PC emulator whose `crates/marty_core/src/cpu_808x/muldiv.rs`
was read while writing `core/src/cpu/instructions/div.cpp`, as a
cross-check on the 8086 divide microcode's CORD, PREIDIV and POSTIDIV
routines. What those routines *do* is a fact about Intel's silicon and is
what this project took: the division loop here is written in C++ over this
codebase's own helpers, in this codebase's idiom, and is not a translation
of MartyPC's Rust. Acknowledged all the same, because reading somebody's
working implementation to understand a mechanism is a debt worth naming
whether or not the licence compels it — and MIT is in any case compatible
with this project's outbound AGPL-3.0-only.

If you believe any part of this project reproduces third-party
*expression* rather than fact, please open an issue. That is a bug, and it
will be treated as one.
