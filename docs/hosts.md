# Checking the hosts

`docs/machine.md` covers `core/`; this covers the two hosts. `--headless`
is what a runner with no display or speaker can check; the window's own
path (texture upload, integer scaling, the audio stream, a key event to a
posted scan code) is §1's windowed cases and §3's person.

---

## 1. What CI checks now

`ctest -L smoke` on any desktop preset runs the `sdl-host-*` cases in
`hosts/sdl/CMakeLists.txt`. The ones about the host itself:

| case | what it settles |
| --- | --- |
| `sdl-host-usage` | the binary starts, links and can talk |
| `sdl-host-smoke-disk` | the smoke disk's two programs are written |
| `sdl-host-runs-a-program` | headless: loader, CPU, DOS, console, exit code |
| `sdl-host-reports-a-stop` | headless: the report's shape, a bounded hang, a dumped frame |
| `sdl-host-demo-disk` | the demo disk's two programs are written |
| `sdl-host-presents-a-frame` | windowed: the picture on the render target, and a keystroke that reached the program |
| `sdl-host-sounds-a-tone` | windowed: a tone at the divisor the program asked for reached the audio device |
| `sdl-host-mutes-the-tone` | windowed: that program with `--mute` reports `sounded 0` (§4) |

The rest (`sdl-host-vfs-door`, `sdl-host-presents-a-document`,
`sdl-host-ingests-a-journal`, `sdl-host-records-and-replays`,
`sdl-host-verifies-a-session`) are headless and belong to the doors they
name.

The windowed cases run **without** `--headless`, under SDL's `dummy` video
and audio drivers with `SDL_RENDER_DRIVER=software`: SDL's real window,
renderer, texture, audio stream and event queue, pointed at no hardware.

- `--verify` reads the render target back after each frame is drawn and
  before it is presented, and compares every pixel against what the host
  uploaded: under nearest-neighbour integer scaling, target pixel `(x, y)`
  is source pixel `(x / scale, y / scale)`. It also counts SDL's audio
  callbacks and non-silent samples and prints §4's three counters, and
  fails if nothing was presented or the picture did not match.
- `--press KEY@FRAME` pushes a real SDL keyboard event onto SDL's own
  queue, so it returns from `SDL_PollEvent` through the real mapping table.
- `hosts/sdl/tests/keymap_test.cpp` checks the SDL-scancode to XT-scan-code
  table against core's `xt_keyboard::xt_table`.
- `hosts/sdl/tests/screen_keyboard_view_test.cpp` checks the on-screen
  keyboard's pixels (§7): every key inside the window at four window
  sizes, and a point in the middle of a key's own rectangle finding that
  key.

Rejected: SDL's `offscreen` video driver, because it creates windows
through EGL and a macOS runner has none. A system SDL3 without the `dummy`
drivers fails at window creation.

---

## 2. The boot driver

The host takes a directory and a program (#83). The method (#94): run it,
read the line it stopped on, widen that service, run it again.

```sh
amberfolio <dir> <program.exe> [options...] [-- ARGUMENTS...]
```

A run prints the file's identity before anything executes and a report when
it ends:

```
amberfolio: load START.EXE sha256=<64 hex characters>
amberfolio: load psp=0050 image=0060 entry=0FD2:0012 stack=117A:0080 tail=0
...
amberfolio: stop reason=unimplemented_service steps=99172 ticks=396688 frames=20 cs=F000 ip=0121 at=0B5D2
amberfolio: stop call=INT21 ah=35 al=00 ax=3500 from=0B58:0052 outcome=handled
amberfolio: stop next=INT 21h AH=35h AL=00h
```

Every report line begins `amberfolio: stop `; `machine/report.h` is the
authority (`docs/machine.md` §5). The SHA-256 is the seam table's key
(PLAN.md §5).

### Flags

| option | what it is for |
| --- | --- |
| `--headless` | no window, no audio device; `--verify`, `--fast`, `--volume` and `--mute` are refused with it. |
| `--scale N` | integer scale of the window. |
| `--verify` | §1. |
| `--press KEY@FRAME[:down\|:up]` | post a real SDL key event at frame `FRAME`. `KEY` is any `SDL_GetScancodeFromName` name: `A`, `Escape`, `Left`, `Keypad 5`. Bare, the make and the break; `:down` the make only, so the key stays held (`Left Alt@7580:down`); `:up` the break only. `hosts/sdl/src/press_spec.h`. Repeatable. |
| `--pull ID@FRAME` | pull a seam's trigger at the top of frame `FRAME` (#161, `docs/seams.md` §3a). Needs `--seam ID`, works headless, refused with `--replay`. Repeatable. |
| `--steps N`, `--until TICKS` | bound the run, the only way to catch a hang. `--steps N` ends on step N exactly. |
| `--dump PREFIX` | at the end of the run write `PREFIX.ppm` (composed frame), `PREFIX.wav` (speaker rendered) and `PREFIX.edges` (§4). |
| `--dump-every N` | also `PREFIX-NNNNNN.ppm` every N frames, numbered in the frames `--press` counts. Needs `--dump`. |
| `--trace` | keep and print with the report the last 256 instructions, 64 service calls and 32 naming file calls; print each file the program names as it happens. |
| `--watch OFF[:N]` | print a data-segment word when it changes. `OFF` is a hex offset in the data segment, `N` is 1 or 2 bytes, default 1. Repeatable. Reads `memory_map::ram()`, not the bus, so it disturbs no EGA latch. Line format: the comment atop `hosts/sdl/src/main.cpp`. |
| `--seam ID` | turn one seam on (PLAN.md §5, `machine/seam.h`), refused unless the loaded program is the binary its addresses are facts about. Repeatable. |
| `--seams` | list every seam this build carries, and exit. |
| `--vfs-list`, `--vfs-get PATH`, `--vfs-remove PATH` | after the run: list the disk; print one file's size and SHA-256, never its bytes; delete one. |
| `--save-layer` | which of the disk's files are the player's (#208, §6): the loaded program's table beside the edition line, and after the run the files it names, each with the slot letter and party-member index its name carries. |
| `--document PATH` | present a document the player holds (`docs/seams.md`). |
| `--keyboard prompt\|name\|full` | open with the on-screen keyboard up, at that layout (#377, §7). Refused with `--headless` and with `--replay`. |
| `--record FILE`, `--record-every N`, `--replay FILE` | write the run down, checkpoint every N frames, replay a recording and check it (`docs/replay.md`). |
| `--wall now\|none\|YYYY-MM-DD[THH:MM[:SS[.CC]]]` | seed the wall clock (#320): this host's clock; unseeded (1 January 1980 plus uptime, which every recording in `tests/sessions/` was made on); or a stated date. Read once before the first instruction, recorded as a `wall` line. Refused with `--replay`. |
| `--speed xt\|turbo\|at\|386` | which machine to be (`machine/clock.h`): 4, 2, 1 or 51/256 ticks a step, `xt` by default. Not a fast-forward. |
| `--fast N\|max` | run virtual time N times faster than the wall, or unpaced. Only the loop's sleep changes (`platform.h`); the run is byte-identical. |
| `--save-sidecars` | keep what this playthrough has accumulated beside its saves (M5-E2c #173, #351): the automap's exploration in `\SAVE\AFMAP.DAT` and the journal's read log in `\SAVE\AFSEEN.DAT`, each with a snapshot per slot, never inside a save. Off by default. |
| `--code-wheel-answered`, `--code-wheel-store PATH`, `--forget-code-wheel` | say the code-wheel challenge has been answered on this copy; where answered copies are remembered; forget this one (#290). |
| `--journal PATH`, `--journal-store PATH`, `--journal-ocr PATH\|none`, `--journal-probe`, `--cite-all-journal` | ingest a journal; where its text lives; which OCR engine; add the synthetic probe edition; cite every entry onto the `Notes` log (`docs/journal.md`). |
| `--volume 0-100`, `--mute` | how loudly to play it (§4); a run at 25% is the same run as one at 100%, to the last edge. |
| `-- ARGUMENTS` | the program's command tail, with the leading space DOS leaves. |

### The keys the host takes for itself

| key | what it does |
| --- | --- |
| **F11** | toggle the mute (#148) |
| **F12** | step the volume through 25/50/75/100% and wrap (#148) |
| **Pause/Break** | pull the trigger of every triggered seam that is on (#161) |
| **middle mouse button** | step the on-screen keyboard on: hidden, then each layout in turn, then hidden again (#377, §7) |
| **left mouse button** | press the on-screen key under the pointer, while it is up |
| **the four arrows, Return** | move the on-screen keyboard's focus and commit, **while it is up**; they reach the machine as usual when it is not |

An 83-key XT board has no scan code for any of the first three, so
`sdl::xt_scancode()` answers 0; `keymap_test.cpp` pins that. **The mouse
needs no such argument**: this machine has no mouse at all, so a button is
a control the game can never want back. The arrows and Return are the one
exception to the rule, and they are borrowed rather than taken — only
while the keyboard is up, which is a state a person put it in and can step
out of. Rejected: the
keypad's `/` and Enter, which sit inside the game's movement cluster.
**Tab** (automap) is claimed by a seam inside
the machine, only while the seam is on (`docs/seams.md` §10). F11 and F12
work during a `--replay`; Pause does not, a pull being an input the
recording never had.

**Losing the window releases the keys.** On `SDL_EVENT_WINDOW_FOCUS_LOST`
the host posts a break code for every key it still holds a make for, in
ascending scan-code order, through the same path a key-up from the window
takes — counted and recorded like any other key, so a replay releases them
at the same tick — and never by writing the BDA shift flags (#313,
`hosts/common/include/amberfolio/host/held_keys.h`). The page does the
same on `blur` and on the tab going hidden.

### Traps

- **A frame boundary is taken off the machine's clock**,
  `box.time() + machine::ega::frame_period`, each time round: a step is
  atomic, so `run_until` overshoots by up to a step and the overshoot is
  carried. A driver wanting `--press KEY@FRAME` to name the same tick must
  do the same (§4, #273).
- **Two runs compared by hash or by pixel must be told the same date**; the
  seed is machine state and the program reads it (`docs/replay.md` §6).
  `scripts/visual-legs.py` states `--wall none` on both sides of every leg,
  and so does every recording in the session library (#293).
- **`--save-sidecars` changes the player's disk**, and every recorded
  session pins its disk by name, size and SHA-256.
- **A hard-disk install's config names absolute paths.** This host mounts
  its directory as the DOS root, so every path built from that config
  misses and the program asks for a floppy. A failed open is a legitimate
  DOS answer, so the trace's file lines are where this shows (#121).
- **`--dump`'s sound capture follows whoever pulls the timeline**, exactly
  one consumer (`platform.h`): SDL's audio thread when a device is open,
  the machine thread when headless. It holds a minute of virtual time, then
  `(truncated)`.
- **No test here ever runs the game.** CI's proof is the smoke disk
  (`hosts/sdl/tests/make_smoke_disk.cpp`) and `cmake/run-stop-report.cmake`;
  the exit criterion is a procedure against the maintainer's copy (#92).

### The file lines

`--trace` prints a line for every file the program names, live, and the
report carries the last thirty-two (#121):

```
amberfolio: file open \SAVE\CHARLIST.TXT handle=0006 none from=0B58:063B
amberfolio: file open \SAVE\CHRDATA1.ITM handle=0000 file_not_found from=0B58:1458
amberfolio: stop trace=on steps_seen=... kept=256 calls_seen=... kept=64 files_seen=... kept=32
amberfolio: stop trace file=open \POR\POOL.CFG handle=0000 path_not_found from=0B58:1458
amberfolio: stop trace file=open \ handle=0000 invalid_drive from=0B58:1458
```

A program asks whether a save slot exists by opening it, so the failures
matter. A path is never truncated: the name has been through
`canonicalize()` into a fixed-size `dos_path` or refused, and one that does
not resolve renders as `\`. `docs/machine.md` §5 has the channel's rules
and §7 why `--dump`, not a golden, is the instrument for "the title
renders".

---

## 3. What a person still has to check

No runner can close these. When you have done one, say so on the issue or
in the commit.

- [ ] Picture and sound on each desktop target: `DEMO.EXE` and
      `COMPOSIT.EXE` below.
- [ ] The code wheel's second launch does not ask (#290): `--seam code-wheel`,
      a correct answer typed, then the same command again, which goes from
      the titles to the menu. CI checks `SeamCodeWheel.*`, the skip and the
      store.
- [ ] Every journal entry proof-read off the game's screen against the scan:
      `--cite-all-journal` or *Cite them all (cheat)*, then `--seam journal`
      and the `Notes` log, Entry 1 first (`docs/journal.md` §10). CI checks
      only the probe edition's four rows (`run-journal.cmake` step 7,
      `smoke.mjs`).
- [ ] A cited row in the `Notes` listing carries today's date, not an uptime
      (#320): `amberfolio: wall clock 2026-09-06 08:07:10 (this host)`.
- [ ] The dev page, served by
      `python3 scripts/serve-web.py build/wasm/hosts/web/<config>`: the speed
      select; the readout `frames= steps= | audio underruns= resyncs= starved=`
      (#106); the worklet's hold-then-fade; a seam checkbox going `off`,
      `armed fired=0`, `armed fired=N`, where `armed fired=0` after a run
      that should have fired is the failure (#131); a dropped real
      installation, with `.EXE`s at the top of the listing (#158); the legs
      of `docs/playable.md` in the page.
- [ ] A resync produced on purpose: a stalled tab, a dragged window, or
      `--fast` past what a 48 kHz device can consume (§4).
- [ ] Whether the game sounds right, and whether 25% is a useful quarter.

### The demo disk

```sh
cmake --build --preset <preset> --target amberfolio-sdl amberfolio-sdl-demo-disk
./build/<preset>/hosts/sdl/<config>/amberfolio-sdl-demo-disk build/<preset>/demo-disk
```

Both are self-written; `hosts/sdl/tests/make_demo_disk.cpp` has the
listings.

**`DEMO.EXE`**, for the senses:

```sh
./build/<preset>/hosts/sdl/<config>/amberfolio build/<preset>/demo-disk DEMO.EXE --scale 3
```

1. A window, 960×600 for `--scale 3`, titled `amberfolio`.
2. Sixteen horizontal colour bars, twelve scanlines each, in EGA palette
   order, then eight black rows. Index 6 must be **brown**, not dark
   yellow: that shows the EGA's DAC is applied, not a 4-bit ramp.
3. A 440 Hz tone, continuous, from the moment the bars appear.
4. Typed characters echoing to the terminal, one per keystroke.
5. **Escape** stops the tone and exits 0; closing the window also exits 0.

A right picture with a silent tone is a fault between
`audio_timeline::render()` and the device; `--verify` says whether the
callback ran and handed over silence:

```sh
./build/<preset>/hosts/sdl/<config>/amberfolio \
    build/<preset>/demo-disk DEMO.EXE --verify --press Escape@120
```

**`COMPOSIT.EXE`**, the one the suite asserts (#56): byte for byte what
the unit suite hashes and the wasm dev page embeds.

```sh
./build/<preset>/hosts/sdl/<config>/amberfolio build/<preset>/demo-disk COMPOSIT.EXE --scale 3
```

1. A cyan band twenty scanlines deep across rows 40-59, on black, and eight
   alternating pixels in the top-left corner.
2. A twenty-millisecond blip.
3. Nothing further until a key is pressed: it is halted inside INT 16h with
   the timer running. Press one and it echoes the character, writes
   `\RUN.LOG`, prints `DONE` and exits **90**.

`amberfolio-dump` runs the same program with no host and writes a PPM, a
WAV and a `.edges` file (§4):

```sh
./build/<preset>/tests/programs/<config>/amberfolio-dump composite <dir>
```

A right PPM with a wrong window is the host; a wrong PPM is the machine,
and `ctest -L unit` says which pixel probe stopped agreeing. Likewise for
a right edge list with wrong noise.

---

## 4. The speaker, measured

`platform.h` makes the **edge list** ("at tick T the speaker output became
high") the canonical audio state; the float samples are a rendering of it
by `audio_timeline::render()`.

### The edge list

```sh
./build/<preset>/hosts/sdl/<config>/amberfolio <dir> DEMO.EXE \
    --verify --press Escape@60 --dump /tmp/tone
```

`/tmp/tone.edges` is text: a two-line header giving the tick rate, one
`tick level` line per transition, and a `# edges N dropped M` trailer that
tells a truncated dump from a quiet run:

```
# amberfolio audio edges
# pit-input-hz 1193182
# tick level
31488 1
32844 0
34200 1
```

`32844 - 31488` is 1356, half of 2712, the divisor `DEMO.EXE` writes to
channel 2: 1,193,182 / 2712 = 440.0 Hz. `sdl-host-sounds-a-tone` checks
that 857 of the file's 858 edges are 1356 ticks apart; the odd one is the
program clearing the gate on its way out.

The log is off unless `--dump` asks, drains to the host every frame, and is
read only on the machine thread. It is not machine state: a unit test
asserts samples and the state hash are equal watched and not.

Three writers, one format (#148):

| writer | how | what it is for |
| --- | --- | --- |
| `amberfolio --dump PREFIX` | streamed, drained every frame | the desktop host |
| `node drive.mjs … --dump PREFIX` | the same, over `af_machine_audio_read_edges` | the wasm module, headless (§5) |
| `amberfolio-dump <program> [<dir>]` | `machine_setup::log_edges` | no host (§3) |

The ABI door is four calls: `af_machine_audio_log_edges`,
`_logging_edges`, `_read_edges`, `_edges_dropped`/`_edges_pending`.
`abi_test.cpp` asserts equal state hashes watched and not; the wasm smoke
check asserts a tone's shape through the door.

**A run dumped on both hosts must produce identical `.edges` files**, which
needs the driver's frame boundary taken off the machine's clock (§2's first
trap, #273). `abi.h`'s absolute-schedule run loop is right for a paced
caller (`pacedAdvance()`, §5) and wrong for a driver.

### The box filter

`tests/core/machine/platform_test.cpp`'s `AudioFilter` suite measures the
reconstruction:

| measurement | value |
| --- | --- |
| mean of a 50% tone over whole periods | **0.125**, to eleven decimal places |
| duty recovered at 12.5 / 25 / 50 / 87.5% | the duty, to 1e-11 |
| a 1000.99 Hz tone at 44,100 | **1000.908 Hz**, mean 0.125124 |
| the same edge list at 48,000 | **1001.043 Hz**, mean 0.125125 |
| the two rates' disagreement | **0.013% in frequency, 1.5e-6 in mean** |
| a rising edge against the fitted period | within **0.82 samples** at 44,100, **0.75** at 48,000 (19 µs, 16 µs) |

- **The DC offset is real, 0.125 at 50% duty, and a property, not a
  defect.** Samples run 0.0 to 0.25, silence being exactly 0.0 (#49). A
  gate held on is not a tone: the game's combat hit is 19 ms of constant
  0.25. Rejected: a high-pass in `render()`, which would stop a sample
  being the exact integral of the edge list.
- **Rejected: a better-than-box filter in v1**: what separates the two pull
  rates is one sample of edge placement no filter removes.

### Underruns and resyncs

Every run prints a line when there is something to say; `--verify` prints
it always:

```
amberfolio: audio underruns=11 resyncs=0 dropped edges=0
```

- **underruns**: the next sample would have reached past the settled
  horizon. Policy: hold the last level, do not advance the cursor
  (`platform_test.cpp`'s `AnUnderrunHoldsTheLevelAndKeepsItsPlace`). A few
  at the start of a windowed run are SDL's device pulling before any
  virtual time has settled; a count that grows is a host that cannot keep
  up.
- **resyncs**: the horizon ran more than 200 ms ahead of playback. Policy:
  jump the cursor to 20 ms behind the horizon and drop the backlog. Expect
  these under `--fast`, a dragged window or a stalled tab.
- **dropped edges**: the ring overflowed. Should be zero; the smoke test
  asserts it.

### Volume and mute

Both hosts have them and core has nothing of them: no field in
`audio_timeline`, no ABI export, no line in a recording, because a sample
out of `render()` is the exact integral of the edge list
(`hosts/sdl/src/audio_gain.h`).

| | desktop | browser |
| --- | --- | --- |
| set by | `--volume 0-100`, `--mute`; **F11**, **F12** | a slider and a checkbox by the speed select |
| applied in | `audio_gain::apply()`, on SDL's audio thread, after `render()` | `audio-worklet.mjs`, as each output sample is written |
| crosses the thread boundary as | a lock-free `std::atomic<float>`, relaxed both ends | a `{ gain }` `postMessage`, drained between quanta |
| measured by | `hosts/sdl/tests/audio_gain_test.cpp`, `sdl-host-mutes-the-tone` | the worklet block in `hosts/web/tests/smoke.mjs` |

Four properties hold on both, each a test:

- **Unity is a no-op, not a multiply**: the same bits `render()` produced
  (`AudioGain.UnityLeavesTheDcOffsetAndTheDutyExactlyWhereTheyWere`).
- **Mute is arithmetic silence**, every sample exactly `0.0F`, including
  the level the worklet invents when it starves. Hence the gain sits inside
  the worklet: a mute on posted chunks would not silence a stalled tab.
- **A change glides** over six milliseconds; a step would be a click.
- **Neither host amplifies**: a request above 100% is clamped.

`--dump`'s WAV is captured before the gain, so a muted run still dumps its
tone; `--verify`'s `sounded` count is after it, so a muted run reports
`sounded 0`. The report line carries `volume=muted`, or the level, only
when it is not unity.

---

## 5. The wasm host

`ctest --preset wasm` runs the module under node, headless: the ABI's
export list, the embedded demo's framebuffer hash and key echo, the
filesystem path a player's directory travels (#84), the driver below and
the worklet's underrun policy (#108). The browser half is §3.

```sh
cmake --build --preset wasm
python3 scripts/serve-web.py
```

### The release bundle

`scripts/release-bundle.sh` stages seven files from the module and
`hosts/web/page/`, the OCR engine when the tree has one, plus `SHA256SUMS`
and `manifest.json`. `scripts/test-release-bundle.sh` is its self-test.

| file | what it is |
| --- | --- |
| `amberfolio.wasm` | the module |
| `amberfolio.mjs` | the Emscripten glue |
| `host.mjs` | the `Machine` façade over the ABI |
| `app.mjs` | the dev page's run loop |
| `audio-worklet.mjs` | the speaker worklet |
| `picker.mjs` | the directory picker |
| `journal.mjs` | the journal store and ingestion; `host.mjs` imports it (#229) |
| `vendor-tesseract.tar.gz` | the browser's OCR engine, when the tree has one (#287) |

Not `index.html`: the release is the emulator, not the page.

**The OCR engine rides as one tarball** (#287). `journal.mjs` refuses a CDN
and reads one library version's output shape, so which tesseract.js a page
serves is a fact about the bundle rather than a site's choice. The
`vendor/tesseract/` directory the wasm CI job stages
(`scripts/fetch-ocr-engine.py`, digests in `scripts/ocr-engine.sha256sums`)
is attached as `vendor-tesseract.tar.gz`, listed in `SHA256SUMS` and
described in `manifest.json` under `engine` — the asset's own digest and
size, the library and its pinned version, `unpacksTo`, and a digest per
file so a consumer can check what it is about to serve as well as what it
downloaded. Its entries are `vendor/tesseract/...`, so unpacking it beside
the bundle puts the engine where `ENGINE_URL` looks; a site serving under
`<origin>/emulator/<tag>/` then passes that path to
`loadEngine({ url })` and no request leaves the origin. About 13 MB as the
tarball and 32 MB unpacked, and a visitor who never picks a journal fetches
none of it — `loadEngine()` is called at an ingestion, not at page load. A
build tree that fetched no engine stages without one and says so, and the
manifest then has no `engine` key at all, the same way it has no `abi` key
for a tree older than the declaration.

```json
{
  "engine": {
    "name": "vendor-tesseract.tar.gz",
    "sha256": "…", "size": 0,
    "library": "tesseract.js", "version": "…",
    "unpacksTo": "vendor/tesseract/",
    "files": [ { "name": "vendor/tesseract/tesseract.min.js", "sha256": "…", "size": 0 } ]
  }
}
```

**Which `journal.mjs` exports are a page's** (#288). A consumer that pins a
tag transcribes the module's surface, so the file's own top comment splits
it into three: *a page's* — the ingestion and the reader's questions
(`loadEngine`, `ingestJournal`, the citation helpers, `journalText`,
`correctJournalEntry`, `clearStore`, `troubleName`, `JOURNAL_OK`); *the dev
page's drawer and panel* — how this repository's page happens to keep a
store in `localStorage`, plus the #301 cheat, which another host need not
copy; and *apparatus* — what `tests/smoke.mjs` and the tooling look inside
with, which is no promise. The store's own six are in none of them: they
are `Machine` methods (below).

### What a serving page has to know (#211)

- **Single-threaded, through 1.0.** No `-pthread`, no shared memory, no
  `SharedArrayBuffer`, no worker pool; the AudioWorklet receives each chunk
  by `postMessage()` with its buffer transferred (`audio-worklet.mjs`;
  `machine/platform.h`'s "one thread, any thread"), and
  `af_machine_run_until()` and `af_machine_render_audio()` are both called
  on the main thread. A route serving this module therefore needs neither
  `Cross-Origin-Opener-Policy: same-origin` nor
  `Cross-Origin-Embedder-Policy: require-corp`, and stays embeddable. If
  that changes both headers become mandatory.
- **The ABI states its version in the manifest, before anything is
  loaded.** `af_version()` is the build (`project(VERSION ...)`), not the
  contract; the contract is `AF_ABI_VERSION_MAJOR`/`AF_ABI_VERSION_MINOR`
  in `core/include/amberfolio/abi.h`. `scripts/release-bundle.sh` reads
  those, and the export list out of the `set()` block in
  `hosts/web/CMakeLists.txt` that feeds `-sEXPORTED_FUNCTIONS`, into
  `manifest.json`:

```json
{
  "version": "0.3.0",
  "abi": { "major": 1, "minor": 1 },
  "sourceCommit": "a827d9ea…",
  "files": [ { "name": "amberfolio.wasm", "sha256": "…", "size": 0 } ],
  "exports": [ "_main", "_malloc", "_free", "_af_version", "…" ]
}
```

**The bump rule:** `major` moves when an entry point is removed, renamed or
changes meaning; `minor` when entry points are added and nothing that was
there changed. A loader compares `major` against what it was written for
and refuses before fetching the module. The ABI is 1.5: 1.1 added the two
doors below (#228, #229), 1.2 added `af_machine_code_wheel_answered` and
`af_machine_set_code_wheel_answered` (#291), 1.3 added
`af_web_journal_part_begins_paragraph` (#361), which is how a fragment
boundary that is a paragraph break reaches the page that joins the pieces
(`docs/journal.md` §5), 1.4 added the ten `af_machine_save_layer_*` calls
(#208), which are where a host learns which of the files on the machine's
filesystem are the player's (§6), and 1.5 added the eighteen
`af_screen_keyboard_*` calls (#377), which are the layouts and the
navigation model of the keyboard both hosts paint on the screen (§7).
`v0.4.0` shipped 1.2 and `v0.5.0` shipped 1.3.

`scripts/test-release-bundle.sh` refuses a release whose header talks about
the version without defining one, whose export block has moved out from
under the parser, or whose `exports` lack `_af_version`. Deliberately not a
refusal: a tree with no `AF_ABI_VERSION_*` (`release.yml` bundles older
tags) stages with a notice and **no `abi` key**, read as "older than the
declaration", never as 1.0.

### The two doors (#228, #229)

**`af_machine_vfs_generation()`** is a monotonic counter, so a write-back
loop need not walk `\SAVE\` per frame. `Machine.vfsGeneration()` is the
page's spelling; `tools/drive.mjs` prints it beside `--vfs-list`. It moves
for a write that landed bytes, a create, a truncate, an unlink and a mkdir,
by the program through INT 21h or by a host through `vfsPut()`,
`vfsRemove()` or `vfsClear()`; not for an open, read, seek, close,
`exists`, `stat` or listing. Not an mtime, and one host call can move it
more than once. `machine/vfs.h` states the rule, those five being
non-virtual wrappers there.

**`af_web_journal_store_changed()` / `_clear_changed()`** say whether the
journal wants saving. Every write to `host::journal_store` raises the flag,
corrections included; reading a store in does not. **The lowering is the
caller's**, a store not knowing whether `localStorage` took the bytes.
Both, plus `journalStoreWrite`, `journalStoreRead` and `journalStoreStats`,
are `Machine` methods over `page/journal.mjs` — and so, since #288, is
`journalSeenRestore()`, the store's *read log* into the machine the reader
draws from. `journalStoreRead()` puts back only the text; before #288 the
log went back through `journal.mjs`'s module-level `restoreSeen(module,
handle)`, so a page built on the facade alone lost every `*` on reload.
Call it after `journalStoreRead()`; twice is harmless. Neither adds an
entry point, so the ABI version does not move.

### Running your own copy in a browser

**start** runs the embedded demo; **run your own copy** takes a dropped or
chosen directory, a program, and **boot**. Nothing is persisted; onboarding
and IndexedDB are M6's (#265).

- Paths are decided in core: every one crosses the ABI as the player's own
  text and goes through `machine::canonicalize()`. The picker keeps
  `webkitRelativePath` (#146), `/` and `\` are one separator here, and core
  makes the directories, so a copy with a `\SAVE\` arrives with its slots.
- A refused file says which kind (#158): a path DOS could never name is
  skipped, and one the filesystem had no room for is `AF_NO_ROOM` with its
  own console line, being a hole in the disk about to be booted. Both hosts
  use `describeSkip()` in `host.mjs`.
- The stop report is the desktop host's text (`machine/report.h`).
- Cursor keys: on an 83-key board the arrows, Home/End, Page and
  Insert/Delete are the keypad and map to its scancodes
  (`hosts/web/tests/smoke.mjs`).
- Losing the keyboard releases the keys (#313): on the window's `blur` and
  on the tab going hidden the page posts a break for every key it still
  holds a make for, in ascending scancode order, through `postKey()` like
  any `keyup` (`HeldKeys` in `host.mjs`, checked by `smoke.mjs`).
- The speed preset is a control, not a build option (#107, #108), with the
  same four names as `--speed`; the volume slider and mute box are the
  page's (#148), applied inside the worklet (§4).

### How the page keeps time

`requestAnimationFrame` fires at the display's refresh rate. Rejected: one
60 Hz frame of virtual time per callback, because it ran the machine at 4x
on a 240 Hz display (#157). `pacedAdvance()` in `host.mjs`, a pure
function, takes the callback's `DOMHighResTimeStamp` and answers where
elapsed real time says virtual time should be. A frame is drawn when
`frameGeneration()` reports a new one (`platform.h`'s pull contract).

- **The catch-up is clamped at a tenth of a second, and what is past the
  clamp is dropped, not banked.** Virtual time falls behind and stays
  behind, which is what makes a browser that cannot keep up settle rather
  than diverge. The readout adds `stalls=N` when it has happened.
- Rejected: a fast-forward control on this page; if one is added it is off
  by default.
- `hosts/web/tests/smoke.mjs` drives `pacedAdvance()` with a synthetic
  clock: 100 callbacks at 240 Hz advance the same virtual time as 25 at
  60 Hz, plus the clamp, a first callback, a backwards clock and the
  callback after a stall.

### What day the page says it is (#320)

The wall clock is a **seed and never a callout**
(`af_machine_set_wall_clock()`): a host says "at this tick the clock read
this", and every later read (INT 21h AH=2Ah and 2Ch, and so every journal
row's stamp) is that instant plus virtual time. `wallClockFields()` in
`host.mjs` is the conversion, pure so `tests/smoke.mjs` can drive it:
`Date` months from zero to DOS's from one, milliseconds floored to
hundredths, years outside 1980-2099 refused. **Local fields, not UTC.** No
control on the page; the desktop host has `--wall` because the seed is
machine state (`docs/replay.md` §6).

**Seeded twice, and the second one is the one that counts** (#343).
`ensureMachine()` seeds it after `reset()`, which carries a seed across,
and the run loop seeds it again at the tick it is about to start stepping
from — where the desktop host has always taken its one, before the first
instruction. A page makes its machine on whichever gesture comes first and
does not step until **start** or **boot**, so without the second seed every
second in between — choosing a directory, ingesting a journal, reading the
code wheel — is wall time the clock never sees. A seed is an origin, not a
correction, so that gap does not close later: the whole journal listing
reads that many minutes stale for the rest of the session, which is what
#343 reported. The first seed stays because *Cite them all* stamps rows off
this clock and can be pressed on a tab that has booted nothing (#352's
shape, in a browser).

### What a browser run says about itself

The diagnostics sink (`machine/diagnostics.h`) does not cross the ABI. What
crosses is formatted in core by `machine::format_diagnostic()`, one line
per record; `machine/log.h` keeps the last few kilobytes,
`af_machine_read_log()` hands them over, and the SDL host renders the same
function's output to stderr, so a line is identical on both hosts.

- The log is not machine state: `af_machine_reset` leaves it alone, and
  `af_machine_clear_log` is the host's broom.
- `af_machine_set_trace` owns the trace ring and the service-call and
  file-event streams, as `--trace` does; notices and seam transitions are
  always kept, and `af_machine_trace_report` renders the ring.
- A traced browser run is a sample plus a count: a line that will not fit
  is dropped whole and counted. Take full traces on the desktop.

### Driving it headlessly: `tools/drive.mjs`

```sh
cmake --build --preset wasm-release
node build/wasm/hosts/web/Release/drive.mjs <dir> <PROGRAM.EXE> [options]
```

The dev page's run loop with the browser taken out: the same `host.mjs`
`Machine`, audio pull and log drain, over a directory walked as the SDL
host walks one (#146). Source: `hosts/web/tools/`; the build places it
beside the module, so it imports `./host.mjs` with no path.

| option | what it is for |
| --- | --- |
| `--frames N`, `--until TICKS`, `--steps N` | bound the run; with none of them a program that never stops runs for ever. |
| `--press KEY@FRAME` | post a key at the top of frame `FRAME`, counting 60 Hz frames of virtual time. Repeatable. |
| `--pull ID@FRAME` | pull a seam's trigger at the top of frame `FRAME` (#161). Repeatable. |
| `--seam ID` | turn one seam on after the load, before the first step. Repeatable; a refusal **ends the run**, so a script never silently gets a plain machine. |
| `--seams` | list every seam this build carries, and exit. |
| `--save-sidecars` | the playthrough's sidecars, the same filenames and bytes as the desktop's, in this module's filesystem. Turn it on after the files are in and before the program is loaded: it reads the working exploration table back. The read log comes back with the journal store, which is this side's. |
| `--document PATH` | present a document the player holds; hashed and dropped. Repeatable. |
| `--code-wheel-answered` | say the challenge has been answered on this copy (#291). Without it the seam only watches, and a driven run sits at the challenge for ever. |
| `--code-wheel-store PATH` | where answered copies are remembered, in the desktop host's format. Read before the first step, written when somebody answers. |
| `--journal-store PATH` | the journal text the reader is answered out of, in the desktop host's format. |
| `--replay PATH` | be the run a recording describes (`af_machine_verify_recording`), and check it. The recording decides seams and speed, so `--seam` is refused beside it; a document or journal store it needs is still this side's. |
| `--speed xt\|turbo\|at\|386` | the governor, spelled as on the desktop host. |
| `--trace` | the trace ring and the service-call and file channels. |
| `--dump PREFIX` | `PREFIX.ppm`, `PREFIX.wav` and `PREFIX.edges`, in the SDL host's formats. |
| `--dump-every N` | also `PREFIX-NNNNNN.ppm` every N frames. |
| `--vfs-list` | every file on the disk after the run, in the SDL host's spelling. |
| `--vfs-get PATH` | one file read back after the run, as size and SHA-256, never bytes (#273). Repeatable. |
| `--save-layer` | which of the disk's files are the player's (#208, §6), in the SDL host's spelling: the table beside the edition line, the files it names after the run. |
| `--quiet` | only the report lines. |
| `-- ARGUMENTS` | the command tail, with DOS's leading space. |

Differences from the desktop host:

- **A frame is the same tick on both hosts** (#273): the boundary is
  `machine.time() + Math.trunc(ticksPerSecond() / 60)`, taken off the
  machine's clock each iteration as the SDL host's
  `box.time() + ega::frame_period` is, so `--press E@20400` names one tick.
- **It carries an empty directory** (#273): a put and the remove that
  leaves the name, the ABI having no `mkdir`; the disk line counts them
  with `dirs=`.
- **Key names take either spelling**: `--press KeyA@60` and `--press A@60`
  both resolve through `host.mjs`'s one scancode table.
- **`--steps N` is coarser**: `af_machine_run_until` takes a tick and the
  ABI has no step-bounded run, so the run ends on the first frame boundary
  at or past N. `--frames` and `--until` are exact.

A run prints the load line, the edition, every key posted, the log, and a
report block:

```
amberfolio: stop reason=tick_budget steps=298296 ticks=1193184 frames=61 ...
amberfolio: state hash=<64 hex characters>
amberfolio: audio underruns=0 resyncs=0
amberfolio: seams cheat-invulnerable unavailable wrong_binary - ...
amberfolio: throughput virtual=1.000s wall=0.008962s factor=111.59x steps=298296 steps/s=33286763
```

The first line is core's, character for character the desktop host's; the
`state hash` is a recording checkpoint's digest (`docs/replay.md` §2). Wall
time appears on the throughput line and nowhere near `core/`
(`scripts/check-host-time.sh`), and the loop is unpaced, so the factor is a
ceiling. Everything goes to stdout, including what the SDL host puts on
stderr, so:

```sh
amberfolio <dir> P.EXE --headless --until 40000000 2>desktop.txt
node .../drive.mjs <dir> P.EXE --until 40000000 >web.txt
diff <(grep '^amberfolio: stop' desktop.txt) <(grep '^amberfolio: stop' web.txt)
```

`--replay` is the only way a **game** session can reach the wasm module,
because its disk is a player's; `tests/sessions/README.md` keeps the
answers, 23 of 23. The driver reads nothing but the directory it is given,
and no game content may ever be in it (CONTRIBUTING.md). CI runs it against
`tests/sessions/spin/` from `hosts/web/tests/smoke.mjs`, which asserts the
whole report block above and that a seam keyed to another binary is refused
with an exit code. Neither host checks a seam being *accepted*: that needs
a program a seam's addresses fit.

### The audio path under load, and the underrun policy

`app.mjs` pulls, on the main thread, exactly the audio contained in the
virtual time each callback advanced, and posts each chunk to the worklet.
Two counters, shown on the page:

- **`underruns` / `resyncs`** are core's (`af_machine_audio_underruns`,
  `af_machine_audio_resyncs`): §4's policies.
- **`starved`** is the worklet's: a render quantum the audio thread had no
  chunk for, counted once per run of starvation.

Neither is machine state (`platform.h`) and nothing back-pressures into the
machine. Core's underrun is microseconds, so it holds the last level. The
worklet's is the main thread not posting in time, seconds for a
backgrounded tab, where a held non-zero level is a DC offset with a step at
both ends; so the worklet **holds and then fades to silence**, the last
real sample for three milliseconds, ramped to zero over six more, silence
thereafter (#108). `smoke.mjs` drives the processor directly, stubbing the
three `AudioWorkletGlobalScope` globals.

### The comparison first light rests on

PLAN.md §7 asks for first light on desktop **and** web; #84 is the test.
Compare

```sh
./build/<preset>/hosts/sdl/<config>/amberfolio <dir> <PROGRAM.EXE> --headless
```

against the page's console after **boot**, or against `tools/drive.mjs`,
which adds a state hash: the `amberfolio: stop ...` lines should be
identical field for field. Trap: `frames=` disagreeing and nothing else
means the machines were not powered on the same way, `reset()` blanking and
republishing the frame and advancing the generation counter. Both hosts
pull the line, `wired_machine`'s constructor and `ensureMachine()` in
`app.mjs`. Procedure: `docs/first-light.md`, then `docs/playable.md`.

---

## 6. The save layer

Which of the files on the machine's filesystem are the **player's**, and
which of those make up save slot `S`. `machine/save_layer.h` is the fact
table; `af_machine_save_layer_*` is how a host reads it (`abi.h`), and
both hosts show it under `--save-layer`.

A host that persists a playthrough has to draw this line. A browser keeps
the game's files one side of it and writes the other side back into its
own storage after a run; the desktop host is looking at a real directory
and can say what in it is the player's. Drawing it from a filename
heuristic — *anything under `\SAVE\`*, *anything written since the run
began* — is a guess at the one place where a guess is worst, with the
publisher's bytes on one side and the player's on the other. So it is a
table, keyed on the loaded program the way a seam's addresses are.

### The table this build carries

For the one edition it knows (`machine/edition.h`). Slots are the ten
letters **A** to **J**; a party's records are numbered **1** to **8**.

| pattern | kind | required | what it is |
|---|---|---|---|
| `\SAVE\SAVGAM<S>.DAT` | slot | yes | the saved game |
| `\SAVE\CHRDAT<S><N>.SAV` | member | yes | one party member's record |
| `\SAVE\CHRDAT<S><N>.ITM` | member | no | what that member carries |
| `\SAVE\CHRDAT<S><N>.SPC` | member | no | that member's memorized spells |
| `\SAVE\CHARLIST.TXT` | roster | no | the characters in no party, shared by every slot |
| `\SAVE\AFMAP<S>.DAT` | sidecar | no | **ours**: the automap's exploration, as slot `<S>` was written |
| `\SAVE\AFSEEN<S>.DAT` | sidecar | no | **ours**: the journal's read log, as slot `<S>` was written |
| `\SAVE\AFMAP.DAT` | sidecar | no | **ours**: the working exploration table |
| `\SAVE\AFSEEN.DAT` | sidecar | no | **ours**: the working read log |
| `POOL.CFG` | config | no | the program's settings — **a game file** |
| `\SAVE\<NAME>.CHA` | character | no | a character kept under a name the player chose |
| `\SAVE\<NAME>.ITM` | character | no | what that character carries |
| `\SAVE\<NAME>.SPC` | character | no | that character's memorized spells |

Three placeholders: `<S>` a slot letter, `<N>` a party-member index,
`<NAME>` a DOS name the player chose and this build cannot enumerate.
**Rows are tried in order and the first match wins** — `<NAME>.ITM` would
otherwise swallow a member's `CHRDAT<S><N>.ITM`, which is why the three
`<NAME>` rows are last.

**`required` means the slot is incomplete without it**: the program
writes it for every save and reads it back for every load. The optional
member rows are absent when there was nothing to put in them, and a save
that finds an old items file where the member now carries nothing
*unlinks* it — so the absence is written down rather than left to a stale
file to contradict.

**Two rows are not the program's**, and are in the table because a host
splitting a filesystem needs to be told where they go. The four `AF*`
sidecars are this build's own (`host::slot_store`) and belong on the
playthrough's side: they are what the automap and the reader learnt while
that party played. `POOL.CFG` belongs on the game's side: the program
reads it at boot and only the configuration program beside it ever writes
it. A boundary is as much about what is on the other side of it, which is
why the `--save-layer` summary prints two numbers.

### Where the table came from

By watching a real copy, which is how every other fact here was gathered
— no test in this repository runs the game (CLAUDE.md), so what follows
is a procedure and not a check.

- **Ten slots, `A` to `J`.** The load menu asks the directory about
  `SAVGAM<L>.DAT` for each letter in turn, and stops at `J`. `--trace`
  over a copy with five slots in it shows five opens answering `none` and
  five answering `file_not_found`.
- **Eight members.** Loading a slot reads `CHRDAT<S>1..8`, and no
  further, whatever is on the disk.
- **What a save writes.** From the camp bar, `SAVE` into an unused
  letter: `SAVGAM<S>.DAT`, then `.SAV` for every member, `.ITM` for
  members carrying something and `.SPC` for members who cast. A party of
  six with items and no casters wrote thirteen files; a party of four
  with three casters wrote ten; a party of three in the wilderness wrote
  six.
- **A save does not tidy up after a smaller party.** Saving three members
  over a slot that held six leaves members four to six where they were.
  So the file set does not say how big the party is, and a host writing a
  directory back should write back everything the table names rather than
  everything it thinks is current.
- **`CHARLIST.TXT` is not part of a save.** It is written when a
  character is made or taken into a party; a save leaves it alone.

The runs, for anyone repeating them (`docs/playable.md` has the method):

```sh
amberfolio <a copy> START.EXE --seam code-wheel --code-wheel-answered
  --wall none --fast max --trace --save-layer
  --press L\7550 --press A\7800      LOAD SAVED GAME, slot A
  --press E\8800 --press S\9400      ENCAMP, SAVE
  --press D\10000                    into slot D, which is empty
  --until 220000000
```

and the same script through `tools/drive.mjs` with `--frames 11100
--quiet --save-layer --vfs-list`, whose listing and whose save-layer
lines name the same thirteen files.

### What the table does not name

A file it does not claim is one no traced run of this edition reads or
writes. A player's `\SAVE\` may still hold some: a record past member
eight, left by nothing this program writes; and `MINIMAP.DAT` /
`MINIMAP<S>.DAT`, which some copies carry, which the program never names
in any run traced here — not a load, not a save, in the city or on the
wilderness map — and which are therefore some other tool's.

These belong with the files the player dropped, and stay wherever a host
put those. **The honest failure to watch for** is the other direction: a
file that *appears during a run* and that the table does not name.
`af_machine_vfs_generation()` says when the disk moved; a host that
notices such a file has found a gap in this table, and that is an issue
to file rather than a file to drop.

### Reading it

Both hosts take `--save-layer` and print the same two things: the table,
beside the edition line, because it is a fact about the program; and,
after the run, the disk read against it.

```
amberfolio: save-layer 13 row(s) slots=ABCDEFGHIJ members=8
amberfolio: save-layer \SAVE\SAVGAM<S>.DAT slot required - the saved game; ...
...
amberfolio: save-layer file \SAVE\SAVGAMF.DAT slot slot=F
amberfolio: save-layer file \SAVE\CHRDATF1.SAV member slot=F member=1
amberfolio: save-layer 79 of 205 file(s) named, 78 the playthrough's
```

A page uses `Machine.saveLayer()` for the table and
`Machine.saveLayerOf(path)` for one path — `{ row, kind, required, slot,
member }`, or `null` for a path the layer does not claim, which is the
answer for every game file and the one a page writing `\SAVE\` back is
asking for. Paths are spelled either way and canonicalized in core
(#146), so no host can reach a different answer by spelling a name
differently.

**No program loaded, or one this build has no table for, answers
nothing** — `null` from `saveLayer()`, zero from the counts,
`AF_SAVE_LAYER_NO_ROW` from `af_machine_save_layer_row_of`. That is the
same "I do not know this file" `af_machine_edition` answers for an
unrecognized binary (`machine/edition.h`), and a host that gets it should
persist nothing rather than persist a guess.

---

## 7. The on-screen keyboard (#377)

A phone has no keyboard and this game asks for a character's name,
answers Y or N a dozen times an hour, and takes single-letter commands
off its own bars. So M6's exit — *playable, text entry included, on a
device with no keyboard attached to it* — needs one painted on the
screen, and both hosts paint the same one.

**The model is core's, in `machine/screen_keyboard.h`.** Which keys are
on which layout, the legend on each, the make code it produces, where it
sits, which key the focus starts on, what moves the focus and what a
commit produces: all of it comes across the ABI, and neither host keeps a
second copy. A layout change is a change to that one file and to no host.

The header has the reasoning. What is here is the format a host reads.

### The keyboards here are reference implementations, not the interface

**Nothing in this section is obligatory.** A host's whole obligation for
input is `af_machine_post_key` — an XT set-1 make code and a direction —
and a host with a native on-screen keyboard, a chorded pad, a phone's own
IME or no screen at all is expected to post scan codes and ignore the
rest. The SDL host's rectangles and the page's buttons are *one* way of
spelling a keyboard, kept in the repository so that both hosts are usable
today and so that the model has two consumers rather than one.

What is offered is four separable layers. A host takes as many as suit it
and writes the rest; core holds no state about any of them, so they can be
mixed freely or stopped at:

| layer | calls | a host that skips it |
|---|---|---|
| 1. the wire | `af_machine_post_key` | cannot: this is the machine's whole input surface |
| 2. the contract | `af_screen_keyboard_commit_scancode`, `_latch_of`, `_release` | writes the tap-versus-latch rule and the event ordering itself |
| 3. the tables | `_layouts`, `_name`, `_about`, `_rows`, `_width`, `_keys`, `_key_*`, `_unit` | paints its own keys |
| 4. the navigation | `_focus`, `_move`, `_key_at` | has its own focus model, or none |

Layer 2 takes **no layout** — a host painting keys this build has never
heard of still gets the ordering right, which is the part that is easy to
get wrong and impossible to notice. Layer 3 needs no focus. A host that
renders the tables with its own toolkit and its own hit testing — the page
does exactly that — takes 3 and skips `_key_at`.

`tests/core/machine/keyboard_test.cpp`'s
`a_latched_shift_reaches_40_17_with_no_keyboard` is the claim, driven with
no layout, no host, no widget and no pixels: `commit_scancode()` and
`post_key()`, and the BIOS delivering the same shifted `'A'` a person
holding the key down gets.

### Three layouts

A layout is chosen by what the program is waiting for, and the three are
not the same size or the same shape:

| name | what it is for | keys |
| --- | --- | --- |
| `prompt` | the yes/no question, and the page of text waiting to be dismissed | `Y` `N` `Enter` `Esc` |
| `name` | free text: a character's name | the digits, the letters, a shift, a space, a backspace, a return |
| `full` | everything else | **all eighty-three keys this machine's keyboard has** |

The `full` layout carries scan codes `0x01` to `0x53` exactly once each,
and `screen_keyboard_test.cpp` derives that against
`xt_keyboard::xt_table` rather than trusting the list — a keyboard with a
missing key is a game with a missing command. The three sets were taken
from the screens `docs/playable.md` documents, whose legs press letters,
the digits, Return, Escape, Tab and the movement cluster.

**Which layout is up is the host's choice, and both hosts make it by
asking the player.** Nothing in core watches the program to see what it
is waiting for, because nothing outside the seam engine may look
(PLAN.md §4). A seam that knew the prompt could choose one later.

### The geometry, in quarter units

Every row is one unit tall. A key's width and its offset along its row
are in **quarter** units — `af_screen_keyboard_unit()` is that four — so
that a shift two keys wide and a return two-and-a-quarter are both
expressible without anybody inventing a fraction. A host picks what a
unit is worth in pixels and multiplies; nothing in core is in pixels.

`af_screen_keyboard_rows(layout)` and `_width(layout)` are what a host
sizes the whole keyboard from. The keys are ordered row by row and, in a
row, left to right, so a host draws them in one pass.

Nothing here is a picture of a physical board: the function keys are a
row where an XT has them in a two-by-five block, and the keypad is two
rows of seven carrying both of each key's legends, because Num Lock is
what decides between them and it decides inside the BIOS
(`machine/keyboard.h`).

### What a commit produces, and the held-key contract

`af_screen_keyboard_commit(layout, key, latched, events, max,
latched_after)` answers the key events a host must post, in order, packed
one to a `uint32_t` as `(scancode << 1) | down`. A host posts each
through `af_machine_post_key` and carries `latched_after` into the next
commit. Nothing new crosses the boundary — the BIOS cannot tell a key
committed on a painted keyboard from a key struck on a real one.

**There is no key repeat.** A finger held on a painted key is one
keystroke. Nothing in this machine repeats a key — there is no IRQ 1 and
no typematic timer — so a repeat would have to be invented by a host, and
a host inventing input is the same fault as a host inventing a port's
answer.

**A modifier latches; everything else taps.** A finger cannot hold Shift
and press A, so Shift, Ctrl and Alt stay down when committed and come up
behind the next ordinary key. `af_screen_keyboard_key_latch()` says which
bit a key is (`AF_LATCH_LEFT_SHIFT`, `_RIGHT_SHIFT`, `_CTRL`, `_ALT`) —
`af_screen_keyboard_latch_of()` is the same question about a bare scan
code — and a host lights the keys whose bit is in the mask. The order
matters and is core's: the shift's make has to reach 40:17 before the
letter's does, because a program reads that byte directly.
`af_screen_keyboard_release()` is the same events for closing the keyboard
or losing the window, so a latched Shift does not outlive the keyboard
that latched it.

**A latch is momentary, and 40:17 says so.** Because the shift comes up
behind the letter inside the same commit, a program that polls the
shift-flag byte afterwards reads it *clear* — nobody is holding a key. The
keystroke in the buffer is still the shifted one. That is the honest
answer and not a gap: a painted Shift is not a Shift wedged down.

The **lock** keys are not latching: Caps, Num and Scroll Lock toggle
inside the BIOS on the make code, so a tap is already what they want.

### Focus, and the two ways to drive it

One key is focused, and the focus is the *host's* — one integer, UI
state, and core keeps none of it. `af_screen_keyboard_focus(layout)` is
where it starts.

- **A pointer or a finger** lands on a key and commits it in one gesture.
  The page's keys are buttons, positioned from the model's own columns and
  widths, so the browser does the hit testing; the desktop host draws into
  a window where there is no widget under the pointer and asks
  `af_screen_keyboard_key_at(layout, row, column)` instead. Both put the
  keys in the same places, so a gap is a gap in both.
- **A four-way control** moves the focus with
  `af_screen_keyboard_move(layout, key, AF_NAV_*)` and commits separately.
  Left and right step within the row and wrap at its ends; up and down
  change row, wrap top to bottom, and land on the key whose span covers
  the *column* the focus left — so a wide space bar is reached from every
  key above it, and leaving it again arrives under the finger. Today that
  control is the arrow keys on both hosts; in M8 it is a gamepad (#210),
  and this is the path it will drive.

### What each host does with it

**The desktop host** draws the keyboard over the window, sitting on the
bottom edge, at whatever size the window allows — nearly its full width
and never more than half its height. The legends are the machine's own
character generator (`machine/font.h`) at one size for the whole board,
so the keyboard looks like the machine it is attached to. `--keyboard
NAME` opens with it up; the middle mouse button steps it on through the
layouts and off again; the left button presses the key under the pointer.

A committed key goes out through the same path a key struck at the window
takes — counted, recorded, and let go of at a focus loss — so a run
driven from the painted keyboard records and replays like any other.
It is drawn **after** `--verify`'s read-back, deliberately: `--verify` is
a claim about the machine's own pixels, and an overlay in the target
would make it a claim about this host's furniture instead. And it is
refused with `--replay` for the reason `--pull` is: a replay's keys are
the recording's.

**The page** renders the keys as buttons from the same numbers
(`readScreenKeyboard()` in `host.mjs`), with a checkbox to show it and a
select to choose the layout. `commitKey()`, `moveFocus()` and
`releaseLatched()` are the same three calls one layer up, and
`commitScancode()` / `latchOf()` are layer 2 for a page that paints its
own keys. The widget itself is `wireScreenKeyboard()` in `app.mjs`, which
is the dev page's and which a serving page is free to replace outright.
