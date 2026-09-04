# Amber Folio

A low-level emulator for the SSI Gold Box games.

**Status: early development — but the game plays, and it plays
enhanced.** M5's exit criterion was met and tagged `v0.3.0`: all six of
the v1 enhancements work and toggle independently on desktop and in the
browser — the code-wheel bypass, the Encamp Fix, the automap, the
journal, fog of war on the overworld, and the debug cheats. Each is an
opt-in seam, each is **off by default**, and each is built to feel like
something the game shipped with rather than a layer over it.
`docs/enhancements.md` is the set as a player meets them, one entry each,
ending with what it is not yet.

`v0.4.0` is the current tag, and it is an interim one rather than a
milestone's: the **code-wheel bypass changed shape** under it (#290).
It used to be gated on a PDF of the wheel, and the releases sold today
ship a code generator application instead of that file — so the proof
moved from the artifact to the act. The game now asks its question
**once**: answer it off whatever form of the wheel you own, and it is
never drawn again. Beside it are six fixes and a good deal of walking
about that M5's follow-ups called for.

M4's criterion still holds underneath: the full loop runs on a
player-supplied copy on all four targets. A party is made and added, the
city and its opening story event play, movement works, a random encounter
fights to a finish with and without the debug cheats seam, a game saves
and loads, a shop sells, a temple heals, a gem is sold, and the map's
edge is walked across — and the same loop runs in a browser, from the
same core. `docs/playable.md` is that procedure, leg by leg, with the
keystrokes that drive it and — its last section — what it has *not*
covered.

Underneath it are the two instruments M4 needed: a **replay harness**
(`docs/replay.md`) that makes a run into keys, ticks and hashes, so a
139-million-step recording of a real game made on the desktop host
verifies checkpoint for checkpoint on the WebAssembly one; and the
**seam engine** (`docs/seams.md`), the only mechanism by which anything
but the program may touch this machine. Every seam is off by default,
and with all of them off a run's state hash is the hash of the same run
on a build with no engine at all — a test, not a promise.

M3's exit criterion — the boot — stands where it was: a player-supplied
copy runs its own unpacker and overlay manager, renders its title
sequence, answers its menus, and reaches the party roster.
`docs/first-light.md` is that procedure.

The 8086 core is exact: all 323
vector files of the
[SingleStepTests/8088](https://github.com/SingleStepTests/8088) v2 set —
captured from real silicon — pass in full in CI on every push, undefined
flag behaviour included, and stayed passing through every device built
around them.

Around that core there is a PC: a memory map, a virtual clock counted in
PIT ticks, an 8253 and a minimal 8259 that the machine's own power-on
self test programs the way a ROM does, an EGA with its planar write
pipeline, a raster its status register answers from, and a renderer, a
PC speaker, BIOS keyboard services, a virtual filesystem, an MZ loader
with relocations, and the INT 21h and INT 10h subsets the plan scopes
plus what a real boot and a real game turned out to ask for. Both hosts
run it — the SDL3 desktop host takes a directory and a program, and the
wasm dev page runs the same machine in a browser. Self-written real-mode
test programs drive all of it end to end on all four targets, one of
them shaped like a boot: it unpacks itself, loads a module off the
filesystem, far-calls into it, and calls every service the real one
needed.

Nothing is faked. An unimplemented service, register or port is a loud
log line and a clean stop, never a guessed answer — which is why a boot
log reads as a worklist and why the three notices a healthy run prints
are each a true statement about what this machine does not have.

That is M0 through M5 done, and M5 kept the promise it was planned on:
the five enhancements it added are handlers over primitives the seam
engine already had, and **not one new mechanism**. What they did need was
doors — a host service both hosts implement, a document gate, and a call
into the program so that text a seam puts on the game's screen is drawn
by the game, in the game's font.

Next is M6 — onboarding, shells and gamepad: a real web shell with
file-picker onboarding and a clear unrecognized-edition path, persistence
in the browser, a toggle panel instead of command-line flags, desktop
first-run polish, and gamepad and virtual-keyboard input on both hosts.
The worklist it starts from is
[#265](https://github.com/amberfolio/amberfolio/issues/265) — every door
and gate M5 built, inventoried against what each host actually shows a
person. The shape of the work is in the [project plan](PLAN.md).

**Try it in a browser:** <https://amberfolio.vercel.app> — the wasm host,
published automatically from `main` on every push. It is still a
developer page rather than a shell: point it at a directory holding your
own copy and it will boot it, report the same stop line the desktop host
does, and let you read it. Onboarding, persistence and a real interface
are M6.

## What this will be

Amber Folio is a purpose-built, low-level emulator for the machine the SSI
Gold Box CRPGs (Pool of Radiance and its family) ran on — real-mode x86,
EGA graphics, PC-speaker sound — running in the browser via WebAssembly and
natively on desktop. You bring your own legally-owned copy of a game; the
emulator runs the unmodified original program. Quality-of-life enhancements
are applied as opt-in runtime patches to the machine's memory ("seams"),
leaving the original bytes on disk untouched.

## Building from source

CMake 3.25+, Ninja, git, and a compiler with C++23 (C++20 is accepted as a
fallback for lagging toolchains). Every target is a preset, and those
presets are exactly what CI runs — nothing here is a CI-only incantation.

```sh
git clone https://github.com/amberfolio/amberfolio.git
cd amberfolio
cmake --preset linux-gcc          # or linux-clang, macos, windows-msvc
cmake --build --preset linux-gcc
ctest --preset linux-gcc          # unit tests + programs + host smoke checks
```

Each preset also has `-debug` and `-release` build and test variants; the
bare name is Debug. Build trees land in `build/<preset>/`.

### Per platform

- **Windows**: Visual Studio 2022 or later with the *Desktop development
  with C++* workload, which brings MSVC, CMake and Ninja along with it; run
  the commands from a developer shell so `cl` and `ninja` are on `PATH`.
  Clone somewhere short, near the drive root — the fetched dependencies
  build in deeply nested directories, and a long clone path pushes them
  past Windows' 260-character limit, which surfaces as
  `Filename longer than 260 characters` during configure.
- **macOS**: `xcode-select --install` for the toolchain, then
  `brew install cmake ninja`. The `macos` preset builds a universal binary
  (arm64 + x86_64) in one pass.
- **Linux**: `cmake`, `ninja-build`, and GCC or Clang from your package
  manager, plus SDL3's backend development headers — see below.

**SDL3** is fetched and built at configure time — nothing third-party is
committed here. `-DAMBERFOLIO_USE_SYSTEM_SDL3=ON` uses an installed one
instead. On Linux that fetched build needs the X11/Wayland/ALSA/PulseAudio
development headers present first; the `build` job in
[`.github/workflows/ci.yml`](.github/workflows/ci.yml) has the exact
package list, which is SDL3's own
([README-linux](https://wiki.libsdl.org/SDL3/README-linux#build-dependencies))
and worth installing in full. Skip them and you get one of two bad
outcomes rather than a warning: a backend whose main library is present
but whose extension headers are not fails configure outright
(`Couldn't find dependency package for ...`), and one that is missing
altogether is dropped silently — leaving a host that builds green here
and cannot open a window on someone else's machine.

### Tests

The unit tests live in [`tests/`](tests) and run under GoogleTest, which is
fetched at configure time like SDL3 (`-DAMBERFOLIO_USE_SYSTEM_GTEST=ON`
uses an installed one). Each test is registered with CTest individually, so
`ctest` names the ones that fail:

```sh
ctest --preset linux-gcc                    # everything
ctest --preset linux-gcc -L unit            # unit tests only
ctest --preset linux-gcc -L bench           # the 8086 programs, timed
ctest --preset linux-gcc -R '^Version\.'    # by name
```

`-DAMBERFOLIO_BUILD_TESTS=OFF` skips the tests, and with them the
GoogleTest fetch. They are a native-target thing: the wasm build has its
own check, below.

### Programs

[`tests/programs/`](tests/programs) is a different question from the rest
of the suite. Everything else asks whether one instruction, from a given
state, produces a given state; nothing there asks whether a thousand
instructions in a row still add up. So it holds three short 8086 programs,
written here and hand-encoded — a counted loop, a sieve of Eratosthenes to
100,000, and the string instructions over two 32 KiB buffers — run to HLT
against a flat megabyte of RAM.

Each one's answer is a fact about arithmetic rather than about this
emulator, which is what makes it worth asserting; so is its exact step
count, which is the only thing in the suite that would notice a change to
the step model itself. The unit suite asserts both, case by case.
`amberfolio-bench` runs the same list and times it:

```sh
cmake --build --preset linux-gcc-release
./build/linux-gcc/tests/programs/Release/amberfolio-bench
```

It needs neither GoogleTest nor a fetched anything, so unlike the rest of
the suite it builds under Emscripten too — `ctest --preset wasm` runs it
under node, which is what exercises the interpreter on the target a
browser gets rather than only compiling it for that target.

On Linux with Clang there is a sanitizer preset — AddressSanitizer and
UndefinedBehaviorSanitizer over the same tests, with every finding fatal:

```sh
cmake --preset linux-asan-ubsan
cmake --build --preset linux-asan-ubsan
ctest --preset linux-asan-ubsan
```

It builds the core and the tests but no host, so nothing in the report
comes from SDL3. CI runs it on every push.

### CPU conformance

The 8086 interpreter is checked against
[SingleStepTests/8088](https://github.com/SingleStepTests/8088) — MIT-licensed
test vectors captured from real silicon, one file per opcode, around ten
thousand cases in each. Registers, flags (bit for bit, undefined behaviour
included) and every byte of memory are compared after each instruction.

The vectors are 726 MB and are never committed here. Fetch them once — the
script takes them in a single sparse partial clone of the pinned commit,
so it needs `git` on PATH — and it strips the per-cycle bus trace this
emulator has no use for on the way in, which takes the set down to about
200 MB in a cache outside the source tree:

```sh
python3 scripts/fetch-conformance-vectors.py     # ~726 MB, once
ctest --preset linux-gcc -L conformance
```

Without them every conformance case reports SKIPPED rather than failing,
so `ctest` stays green if you have not run the script. Useful knobs:

| Variable | What it does |
| --- | --- |
| `AMBERFOLIO_CONFORMANCE_VECTORS` | where the condensed vectors live (default: your per-user cache directory; `--print-dir` says where) |
| `AMBERFOLIO_CONFORMANCE_LIMIT` | run only the first N vectors of each file |
| `AMBERFOLIO_CONFORMANCE_REQUIRED` | missing vectors are a failure, not a skip — what CI sets |

`--stems 00 90 80.0` fetches just the files you need, and
`-DAMBERFOLIO_BUILD_CONFORMANCE=OFF` drops the suite and the two libraries
it fetches (simdjson, libdeflate) from the build entirely.

Every file of the pin runs — the suite is exhaustive, and the manifest it
is generated from
([`tests/conformance/vector-files.txt`](tests/conformance/vector-files.txt))
is checked against the pin at configure time. A suite that lost a file
fails the build rather than reporting 100% of a smaller number.

[`docs/cpu-implementation.md`](docs/cpu-implementation.md) is the CPU
playbook — the architecture tour, the house style for an instruction
handler, the commands, and how to read a failing vector.
[`docs/hosts.md`](docs/hosts.md) is the other end: what the hosts' smoke
checks settle, the windowed and audio paths included, and the two things
about a host that only a person in front of a display and a speaker can
ever confirm. [`docs/enhancements.md`](docs/enhancements.md) is the enhancements as a
player meets them — what each one does, how to turn it on, what it will
not do without, and what it is not yet.
[`docs/journal.md`](docs/journal.md) is the journal's
ingestion: how a player's own Adventurer's Journal becomes text, what an
OCR engine is and is not asked to do on each host, and how an edition is
measured and added to the table.

Formatting (`clang-format`), static analysis (`clang-tidy`), shell linting
(`shellcheck`), the content guard, the DCO check and the host-time guard
— nothing under `core/` may read the host's clock, which is what keeps a
run replayable — are gates in CI too, each a script you can run yourself
before pushing:
[CONTRIBUTING.md](CONTRIBUTING.md#checks-and-gates) has the six commands
and how to install what they need.

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
ctest --preset wasm          # loads the module under node, checks its ABI,
                             # and runs the 8086 programs (the GoogleTest
                             # suite is native-only)
```

That leaves the module, its glue, and the JS host together in
`build/wasm/hosts/web/Debug/`.

**For anything you mean to run rather than debug, build `wasm-release`**
instead — same commands with `--preset wasm-release`, landing in
`.../web/Release/`. The optimised module is about nine times faster and
thirty-three times smaller (172 KB of wasm against 5.7 MB), and it is what
CI deploys. The Debug tree is worth keeping around: it is where a stack
overflow or an out-of-bounds shows up as itself rather than as a page that
quietly stops.

ES modules need a real origin, so serve the
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
- Third-party dependencies, the CPU conformance oracle, and the published
  reverse-engineering work this project has learned from are acknowledged
  in [NOTICE.md](NOTICE.md). Nothing third-party is committed here; it is
  all fetched at build time.

## Trademarks

"Amber Folio" names this project and its official builds; see
[TRADEMARK.md](TRADEMARK.md). Amber Folio is an independent project, not
affiliated with or endorsed by Wizards of the Coast, Hasbro, SNEG, or any
rights holder of the referenced games. "Gold Box", "Dungeons & Dragons",
"Forgotten Realms", and the game titles are used only nominatively, to
describe compatibility; all trademarks are the property of their respective
owners.
