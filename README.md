# Amber Folio

A low-level emulator for the SSI Gold Box games.

Amber Folio is a purpose-built emulator for the machine the Gold Box CRPGs
(Pool of Radiance and its family) ran on: real-mode x86, EGA graphics,
PC-speaker sound. It runs in the browser via WebAssembly and natively on
Windows, macOS and Linux. You bring your own legally-owned copy of a game;
the emulator runs the unmodified original program. Quality-of-life
enhancements are opt-in runtime patches to the machine's memory
("seams"), off by default, leaving the bytes on disk untouched.

**Status: early development.** `v0.5.0` is the current tag. Pool of
Radiance plays end to end on all four targets from a player-supplied copy,
and the six v1 enhancements work and toggle independently on desktop and
in the browser: the code-wheel bypass (it asks once), the Encamp Fix, the
automap, the journal, fog of war on the overworld, and the debug cheats.
The 8086 core passes all 323 files of the
[SingleStepTests/8088](https://github.com/SingleStepTests/8088) v2 set in
CI on every push. The current milestone is M6: onboarding and a real web
shell. [PLAN.md](PLAN.md) is the plan of record;
[`docs/enhancements.md`](docs/enhancements.md) is what each enhancement
does for a player.

**Try it in a browser:** <https://amberfolio.vercel.app>, published from
`main` on every push. It is a developer page rather than a shell: point it
at a directory holding your own copy and it boots it.

## Building from source

CMake 3.25+, Ninja, git, and a compiler with C++23 (C++20 is accepted as a
fallback). Every target is a preset, and the presets are exactly what CI
runs.

```sh
git clone https://github.com/amberfolio/amberfolio.git
cd amberfolio
cmake --preset linux-gcc          # or linux-clang, macos, windows-msvc
cmake --build --preset linux-gcc
ctest --preset linux-gcc          # unit tests + programs + host smoke checks
```

Each preset also has `-debug` and `-release` variants; the bare name is
Debug. Build trees land in `build/<preset>/`.

### Per platform

- **Windows**: Visual Studio 2022 or later with the *Desktop development
  with C++* workload; run the commands from a developer shell. Clone
  somewhere short, near the drive root: the fetched dependencies nest
  deeply enough that a long path trips the 260-character limit during
  configure.
- **macOS**: `xcode-select --install`, then `brew install cmake ninja`.
  The `macos` preset builds a universal binary (arm64 + x86_64).
- **Linux**: `cmake`, `ninja-build`, GCC or Clang, plus SDL3's backend
  development headers (X11/Wayland/ALSA/PulseAudio). The `build` job in
  [`.github/workflows/ci.yml`](.github/workflows/ci.yml) has the exact
  package list. A missing backend is dropped silently at configure time,
  so install the full list.

**SDL3** and **GoogleTest** are fetched and built at configure time;
`-DAMBERFOLIO_USE_SYSTEM_SDL3=ON` and `-DAMBERFOLIO_USE_SYSTEM_GTEST=ON`
use installed ones. `-DAMBERFOLIO_BUILD_TESTS=OFF` skips the tests.

### Tests

```sh
ctest --preset linux-gcc                    # everything
ctest --preset linux-gcc -L unit            # unit tests only
ctest --preset linux-gcc -L bench           # the 8086 programs, timed
ctest --preset linux-gcc -L smoke           # the hosts, headless
ctest --preset linux-gcc -R '^Version\.'    # by name
```

[`tests/programs/`](tests/programs) holds self-written 8086 programs run
to HLT through the whole machine; their answers and exact step counts are
asserted. `amberfolio-bench` times the same list and is the one piece of
apparatus that builds under Emscripten, so `ctest --preset wasm` runs the
interpreter under node.

The `linux-asan-ubsan` preset runs the tests under AddressSanitizer and
UndefinedBehaviorSanitizer, core and tests only, no host.

### CPU conformance

The interpreter is checked against SingleStepTests/8088: one file per
opcode, about ten thousand cases each, registers, flags and memory
compared after every instruction. The vectors are 726 MB and never
committed; fetch them once into a cache outside the tree:

```sh
python3 scripts/fetch-conformance-vectors.py     # ~726 MB, once
ctest --preset linux-gcc -L conformance
```

Without them every conformance case reports SKIPPED. Knobs:

| Variable | What it does |
| --- | --- |
| `AMBERFOLIO_CONFORMANCE_VECTORS` | where the condensed vectors live (`--print-dir` says the default) |
| `AMBERFOLIO_CONFORMANCE_LIMIT` | run only the first N vectors of each file |
| `AMBERFOLIO_CONFORMANCE_REQUIRED` | missing vectors fail rather than skip (CI sets this) |

`--stems 00 90 80.0` fetches only named files;
`-DAMBERFOLIO_BUILD_CONFORMANCE=OFF` drops the suite and its two fetched
libraries. The manifest
([`tests/conformance/vector-files.txt`](tests/conformance/vector-files.txt))
is checked against the pin at configure time.

### Gates

Formatting, static analysis, shell linting, the content guard, the DCO
check and the host-time guard run in CI on every push; each is a script
under `scripts/` you can run yourself.
[CONTRIBUTING.md](CONTRIBUTING.md#checks-and-gates) lists them.

### WebAssembly

The wasm build needs an activated [emsdk](https://emscripten.org/) of the
version pinned in [`.emscripten-version`](.emscripten-version):

```sh
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install  "$(cat /path/to/amberfolio/.emscripten-version)"
./emsdk activate "$(cat /path/to/amberfolio/.emscripten-version)"
source ./emsdk_env.sh        # sets EMSDK, which the wasm preset needs
```

```sh
cmake --preset wasm
cmake --build --preset wasm
ctest --preset wasm          # loads the module under node and runs the 8086 programs
```

That leaves the module, its glue and the JS host in
`build/wasm/hosts/web/Debug/`. For anything you mean to run rather than
debug, use `--preset wasm-release` (about 9x faster, 33x smaller; it is
what CI deploys). Serve the directory rather than opening the file:

```sh
python3 scripts/serve-web.py        # then open http://localhost:8000/
```

Use that rather than `python3 -m http.server`: on Windows the standard
library serves `.mjs` as `text/plain`, which browsers refuse to execute.
`--config`, `--port` and `--build-dir` are there when the defaults are
wrong.

The page is deployed to <https://amberfolio.vercel.app> by CI on every
push to `main`, and to a preview URL for every same-repo pull request.
[`deploy/vercel/README.md`](deploy/vercel/README.md) has the details. The
page links to the source it was built from, which is what AGPL-3.0 §13
requires of a program served over a network.

## Documentation

| Topic | Where |
| --- | --- |
| Plan of record | [PLAN.md](PLAN.md) |
| The CPU core and adding an instruction | [`docs/cpu-implementation.md`](docs/cpu-implementation.md) |
| The machine, devices and services | [`docs/machine.md`](docs/machine.md) |
| Seams: the engine and every seam this build carries | [`docs/seams.md`](docs/seams.md) |
| Recording and replaying a run | [`docs/replay.md`](docs/replay.md), [`tests/sessions/README.md`](tests/sessions/README.md) |
| Booting and playing the game headlessly | [`docs/first-light.md`](docs/first-light.md), [`docs/playable.md`](docs/playable.md) |
| The hosts, the wasm ABI, the speaker | [`docs/hosts.md`](docs/hosts.md) |
| The journal | [`docs/journal.md`](docs/journal.md), [`docs/journal-test-plan.md`](docs/journal-test-plan.md) |
| The explored overlay | [`docs/explored-overlay.md`](docs/explored-overlay.md) |
| The enhancements, for a player | [`docs/enhancements.md`](docs/enhancements.md) |

## Principles

- **Bring your own game.** Amber Folio ships none of the original games'
  code, data or artwork, ever. It runs your own copies.
- **Nothing derived ships.** No original code, no disassembly, no
  translated routines. The full public history exists to make that
  verifiable; a content guard scans every commit in CI on every push.
- **Fidelity first.** The real machine, faithful to the original's
  behaviour, with enhancements strictly opt-in.

## License

Amber Folio is free software under the **GNU Affero General Public License,
version 3.0 only** (`AGPL-3.0-only`); see [LICENSE](LICENSE).

- Contributions are accepted under an Apache-2.0 inbound rule with a DCO
  sign-off, no CLA. See [CONTRIBUTING.md](CONTRIBUTING.md).
- Commercial licensing outside the AGPL is available; see
  [COMMERCIAL.md](COMMERCIAL.md).
- The project's releases will always remain available under an
  OSI-approved open-source license.
- Third-party dependencies, the CPU conformance oracle, and the published
  reverse-engineering work this project has learned from are acknowledged
  in [NOTICE.md](NOTICE.md). Nothing third-party is committed here.

## Trademarks

"Amber Folio" names this project and its official builds; see
[TRADEMARK.md](TRADEMARK.md). Amber Folio is an independent project, not
affiliated with or endorsed by Wizards of the Coast, Hasbro, SNEG, or any
rights holder of the referenced games. "Gold Box", "Dungeons & Dragons",
"Forgotten Realms", and the game titles are used only nominatively, to
describe compatibility; all trademarks are the property of their
respective owners.
