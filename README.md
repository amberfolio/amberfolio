# Amber Folio

A low-level emulator for the SSI Gold Box games.

**Status: early development.** There is no emulator yet. What builds today
is the skeleton — an empty core library, a stub desktop host, and a wasm
module that reports its version — on all four targets. The shape of the
work is in the [project plan](PLAN.md).

**Try it in a browser:** <https://amberfolio.vercel.app> — the wasm host,
published automatically from `main` on every push. Today it reports the
core version and nothing more; that is the point of having the pipeline
in place before there is anything to see.

## What this will be

Amber Folio is a purpose-built, low-level emulator for the machine the SSI
Gold Box CRPGs (Pool of Radiance and its family) ran on — real-mode x86,
EGA graphics, PC-speaker sound — running in the browser via WebAssembly and
natively on desktop. You bring your own legally-owned copy of a game; the
emulator runs the unmodified original program. Quality-of-life enhancements
are applied as opt-in runtime patches to the machine's memory ("seams"),
leaving the original bytes on disk untouched.

## Building

CMake 3.25+, Ninja, and a compiler with C++23 (C++20 is accepted as a
fallback for lagging toolchains). Every target is a preset, and those
presets are exactly what CI runs — nothing here is a CI-only incantation.

```sh
cmake --preset linux-gcc          # or linux-clang, macos, windows-msvc
cmake --build --preset linux-gcc
ctest --preset linux-gcc          # unit tests + host smoke checks
```

Each preset also has `-debug` and `-release` build and test variants; the
bare name is Debug. Build trees land in `build/<preset>/`.

- **Windows**: run from a Visual Studio developer shell so `cl` and `ninja`
  are on `PATH`.
- **macOS**: the `macos` preset builds a universal binary (arm64 +
  x86_64) in one pass.
- **SDL3** is fetched and built at configure time — nothing third-party is
  committed here. `-DAMBERFOLIO_USE_SYSTEM_SDL3=ON` uses an installed one
  instead. On Linux, install the X11/Wayland/ALSA/PulseAudio development
  headers first (see the `build` job in `.github/workflows/ci.yml` for the
  exact package list), or SDL3 will build without those backends.

### Tests

The unit tests live in [`tests/`](tests) and run under GoogleTest, which is
fetched at configure time like SDL3 (`-DAMBERFOLIO_USE_SYSTEM_GTEST=ON`
uses an installed one). Each test is registered with CTest individually, so
`ctest` names the ones that fail:

```sh
ctest --preset linux-gcc                    # everything
ctest --preset linux-gcc -L unit            # unit tests only
ctest --preset linux-gcc -R '^Version\.'    # by name
```

`-DAMBERFOLIO_BUILD_TESTS=OFF` skips the tests, and with them the
GoogleTest fetch. They are a native-target thing: the wasm build has its
own check, below.

On Linux with Clang there is a sanitizer preset — AddressSanitizer and
UndefinedBehaviorSanitizer over the same tests, with every finding fatal:

```sh
cmake --preset linux-asan-ubsan
cmake --build --preset linux-asan-ubsan
ctest --preset linux-asan-ubsan
```

It builds the core and the tests but no host, so nothing in the report
comes from SDL3. CI runs it on every push.

Formatting (`clang-format`), static analysis (`clang-tidy`) and shell
linting (`shellcheck`) are gates in CI too; the configs are in the repo
root and the commands are in
[CONTRIBUTING.md](CONTRIBUTING.md#formatting-and-linting).

### WebAssembly

The wasm build needs an activated [emsdk](https://emscripten.org/), pinned
in [`.emscripten-version`](.emscripten-version) so a given commit builds
with a known toolchain. Install that exact version:

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
ctest --preset wasm          # loads the module under node, checks its ABI
                             # (the unit tests are native-only)
```

That leaves the module, its glue, and the JS host together in
`build/wasm/hosts/web/Debug/`. ES modules need a real origin, so serve the
directory rather than opening the file:

```sh
python3 scripts/serve-web.py        # then open http://localhost:8000/
```

Use that rather than `python3 -m http.server`: browsers apply a strict
MIME check to module scripts, and on Windows the standard library takes
its MIME types from the registry, where `.mjs` is commonly `text/plain` —
which a browser refuses to execute, leaving the page stuck on "loading…".
The script sets the types itself, so one command behaves the same on all
three platforms. `--config`, `--port` and `--build-dir` are there when the
defaults are wrong.

The placeholder page prints the core version to the page and the console.
It is scaffolding: canvas, audio, input and persistence arrive in M2, and
the reference shell in M6.

The same page is deployed to <https://amberfolio.vercel.app> by CI on
every push to `main`, and to a preview URL for every pull request from
this repository — built by the wasm job with the pinned emsdk, never on
the deployment host. [`deploy/vercel/README.md`](deploy/vercel/README.md)
has the details. The page carries a link to the source it was built from,
pinned to that commit: serving the program over a network is what
AGPL-3.0-only §13 attaches the source offer to, and the offer has to be
reachable from the page.

## Principles

- **Bring your own game.** Amber Folio ships none of the original games'
  code, data, or artwork — ever. It runs your own copies (the same titles
  sold today in the official archive collections).
- **Nothing derived ships.** No original code, no disassembly, no
  translated routines. The machine and the hooks are original work, and
  this repository's full public history — from its very first commit —
  exists to make that verifiable by anyone. A content guard scans every
  commit of that history in CI on every push.
- **Fidelity first.** The goal is the real machine, faithful to the
  original's behavior, with enhancements strictly opt-in.

## License

Amber Folio is free software under the **GNU Affero General Public License,
version 3.0 only** (`AGPL-3.0-only`) — see [LICENSE](LICENSE).

- Contributions are accepted under a lightweight Apache-2.0 inbound rule
  with a DCO sign-off — **no CLA**. See [CONTRIBUTING.md](CONTRIBUTING.md).
- Commercial licensing outside the AGPL is available — see
  [COMMERCIAL.md](COMMERCIAL.md).
- The project's releases will always remain available under an OSI-approved
  open-source license.

## Trademarks

"Amber Folio" names this project and its official builds; see
[TRADEMARK.md](TRADEMARK.md). Amber Folio is an independent project, not
affiliated with or endorsed by Wizards of the Coast, Hasbro, SNEG, or any
rights holder of the referenced games. "Gold Box", "Dungeons & Dragons",
"Forgotten Realms", and the game titles are used only nominatively, to
describe compatibility; all trademarks are the property of their respective
owners.
