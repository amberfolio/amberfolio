# Checking the hosts

`docs/machine.md` is the tour of everything under `core/`. This is its
counterpart at the other end: the two hosts, what about them is checked by
a machine, and the part that is only ever settled by a person with a
machine in front of them.

It exists because of a distinction that was worth writing down (#80).
M2-H1 built the SDL3 desktop host and M2-H2 built the wasm one, and both
were developed and verified headless, on Windows. `--headless` is what a
CI runner with no display and no audio device can check, and for a long
time it was the only claim anybody had made: the machine wiring, the
directory VFS, the loader, the console sink, the exit code. Everything at
the window — the texture upload, the integer scaling, the audio stream and
its callback, the step from a key event to a posted scan code — had been
compiled on three desktop targets and run on none.

"It compiles" is not the same claim as "it works". Most of that gap is now
closed by machine, and this file is about both halves: what closed it, and
what a person still has to do.

---

## 1. What CI checks now

`ctest -L smoke` on any desktop preset runs seven cases. Four are
headless; three are not.

| case | what it settles |
| --- | --- |
| `sdl-host-usage` | the binary starts, links and can talk |
| `sdl-host-smoke-disk` | the smoke disk's two programs are written |
| `sdl-host-runs-a-program` | headless: loader, CPU, DOS, console, exit code |
| `sdl-host-reports-a-stop` | headless: the stop report's shape, a bounded hang, a dumped frame |
| `sdl-host-demo-disk` | the demo disk's two programs are written |
| `sdl-host-presents-a-frame` | windowed: the picture that reached the render target, and a keystroke that reached the program |
| `sdl-host-sounds-a-tone` | windowed: what reached the audio device was a tone, and the edge list behind it is the divisor the program asked for |

The last two run the host **without** `--headless`, under SDL's
`dummy` video and audio drivers. Those are not stubs of
ours and they are not a different code path: SDL's window, renderer,
texture, audio stream, audio thread and event queue are all the real ones,
pointed at no hardware. `SDL_RENDER_DRIVER=software` is asked for as well,
so the comparison below means the same thing on all three desktop
operating systems.

Two host options exist for this and are documented in
`hosts/sdl/src/main.cpp`:

- **`--verify`** reads the render target back after each frame is drawn
  and before it is presented, and compares every pixel of it against the
  bytes the host uploaded. The expectation is derived, not stored: with
  nearest-neighbour integer scaling, target pixel `(x, y)` *is* source
  pixel `(x / scale, y / scale)` and nothing else. A wrong stride, a
  swapped colour channel, a texture that never received the new frame and
  a scale that is not integer all come back as a mismatch count. It also
  tallies what SDL's audio thread did — callbacks, samples, and how many
  of those samples were not silence — and fails the process if nothing was
  ever presented or the picture did not match. Since M4-A1 (#106) it also
  prints the timeline's two pacing counters, underruns and resyncs, and
  the ring's dropped-edge count; §4 says what each means.
- **`--dump PREFIX`** writes three files, and the third arrived with the
  same issue: `PREFIX.edges`, the edge list the machine published, one
  line of `tick level` per output transition. §4 again.
- **`--press KEY@FRAME`** pushes a real SDL keyboard event onto SDL's own
  queue at frame `FRAME`, so it comes back out of `SDL_PollEvent` and
  travels the path a typed key travels, mapping table included. `KEY` is
  any name `SDL_GetScancodeFromName` accepts — `A`, `Escape`, `Left`,
  `Keypad 5`.

Separately, `hosts/sdl/tests/keymap_test.cpp` checks the SDL-scancode →
XT-scan-code table against core's own `xt_keyboard::xt_table`, which was
written from the same hardware facts for a different reason. Nothing there
restates the table; it derives three claims about it. That is the check
that would catch one transposed row, which is the failure a single
scripted keystroke never would.

`dummy` and not `offscreen`, which is the other headless video driver:
offscreen creates its windows through EGL, and a macOS runner has no EGL,
so window creation fails there before anything can be checked. `dummy`
wants no graphics library at all, which is what a software renderer
reading its own target back needs.

If a build uses a system SDL3 compiled without the `dummy` video or audio
driver, the two windowed cases fail at window creation and say so.

---

## 2. The boot driver

M3 points this host at the player's own copy (#83). The method (#94) is a
loop — run it, read the line it stopped on, widen the one service that
line names, run it again — so most of what was added is in service of
making that line worth reading.

```sh
amberfolio <dir> <program.exe> [--headless] [--scale N] [--verify]
                               [--press KEY@FRAME] [--pull ID@FRAME]
                               [--steps N]
                               [--until TICKS] [--dump PREFIX]
                               [--dump-every N] [--trace]
                               [--seam ID] [--speed NAME]
                               [--fast N|max] [--volume 0-100] [--mute]
                               [-- ARGUMENTS...]
```

A run prints the identity of the file before anything executes, and a
report when the run ends:

```
amberfolio: load START.EXE sha256=<64 hex characters>
amberfolio: load psp=0050 image=0060 entry=0FD2:0012 stack=117A:0080 tail=0
...
amberfolio: stop reason=unimplemented_service steps=99172 ticks=396688 frames=20 cs=F000 ip=0121 at=0B5D2
amberfolio: stop call=INT21 ah=35 al=00 ax=3500 from=0B58:0052 outcome=handled
amberfolio: stop next=INT 21h AH=35h AL=00h
```

Every line of the report begins `amberfolio: stop `, so the whole block
is one `grep` away in a log with other things in it, and the format is
fixed rather than approximate — `docs/machine.md` §5 has the reasoning
and `machine/report.h` is the authority. The last line is the worklist
entry: the one thing to widen next, named by the machine.

The SHA-256 is a *fact about the player's file* (PLAN.md §2), not
anything out of it, and it is the key M4's seam table will look a
fingerprint up by (PLAN.md §5).

| option | what it is for |
| --- | --- |
| `--steps N`, `--until TICKS` | bound the run. A hang is otherwise the one failure this host cannot report — the machine is running, nothing has refused anything, and the process just sits there. The budget is clamped into the run slice, so `--steps N` ends on step N exactly and the stop can be reproduced. |
| `--trace` | keep the last 256 instructions, 64 service calls and 32 naming file calls, and print them with the report. Off by default, at a cost of one branch per step. |
| `--dump PREFIX` | write `PREFIX.ppm` (the composed frame) and `PREFIX.wav` (the speaker) when the run ends. |
| `--dump-every N` | also write `PREFIX-NNNNNN.ppm` every N frames. A run is a film and one frame of it is a still; everything past the title happens over tens of virtual seconds, and "what did the screen *do*" is not a question a final frame answers. The number in the name is the one `--press KEY@FRAME` counts in, so a still and the keystroke that caused it are named in the same units. Needs `--dump`, whose prefix it shares. |
| `-- ARGUMENTS` | everything after `--` becomes the program's command tail, with the leading space DOS leaves in front of one. |
| `--speed xt\|turbo\|at\|386` | which machine to be (`machine/clock.h`): 4, 2, 1 or 51/256 ticks a step — the last of those is a machine that retires five instructions inside one tick, which is what the clock's subtick accumulator exists for. `xt` is the default and the machine the game was written for. Not a fast-forward — virtual time still governs every deadline, tone and tick, so a run at `at` is as deterministic as one at `xt`; what changes is how much of it fits in a second of yours. Printed when it is not the default. |
| `--fast N\|max` | run virtual time N times faster than the wall, or unpaced. The other way of going faster, and not the same one: `--speed` divides only the computation, `--fast` divides the pauses too. **Nothing inside the machine can tell** — same steps, same ticks, same frames, byte-identical framebuffer; only the sleep at the bottom of the loop changes, which is the one place wall time is allowed to appear (platform.h). Meaningless with `--headless`, which never paced, and refused there rather than ignored. |
| `--seam ID` | turn on one seam (PLAN.md §5, `machine/seam.h`). Off unless named, refused unless the loaded program is the binary the seam's addresses are facts about, and printed when it takes — a run with a seam on is not the same run as one without it. Repeatable. |
| `--pull ID@FRAME` | pull a seam's trigger at the top of frame `FRAME` (#161, `docs/seams.md` §3a). A trigger-driven seam does nothing until somebody asks; this is the scripted ask, and unlike `--press` it needs no window, so it works under `--headless`. The seam has to be on — `--seam ID` as well — and a refusal is printed rather than swallowed. Repeatable. Refused alongside `--replay`, which decides its own pulls. |
| `--automap-store` | keep what the automap seam has explored beside the save (M5-E2c, #173): `\SAVE\AFMAP.DAT` and a snapshot per save slot, this project's own files in the directory the program's saves are in and never inside one. Off by default, and asked for rather than assumed — this is a real directory of the player's, and every recorded session pins its disk by name, size and SHA-256, so a sidecar written by a verification run would make the next run's disk a different disk. Prints what it wrote and read at the end of the run, including when that was nothing. |
| `--volume 0-100`, `--mute` | how loudly to play it, and whether to play it at all (§4). Nothing to do with the machine: a run at 25% is the same run as one at 100%, down to the last edge. **F11** toggles the mute and **F12** steps the volume while the run is going. Refused with `--headless`, which opens no audio device. |

**The three keys this host takes for itself**, and the one argument all
three rest on: an 83-key XT board has no scan code for any of them, so
`sdl::xt_scancode()` answers 0 and the emulated program loses nothing.
`keymap_test.cpp` pins that, because it is the assumption the bindings
rest on.

| key | what it does |
| --- | --- |
| **F11** | toggle the mute (#148) |
| **F12** | step the volume through 25/50/75/100% and wrap (#148) |
| **Pause/Break** | pull the trigger of every triggered seam that is on (#161) |

F11 and F12 are the function keys an 83-key board has not got — it has
ten. Pause/Break it has not got at all: pausing on one was Ctrl and the
keypad's Num Lock, and the dedicated key arrived with the 101-key
Enhanced board as its one E1-prefixed sequence, which this machine's
set-1 wire has no room for. *Break* is also the right word for it — a
person interrupting the program from outside is what a debug trigger is.
The keypad's `/` and its Enter are the only other keys an XT board is
missing, and both were passed over for sitting inside the cluster this
game's movement keys are: a key you can hit by accident mid-fight is the
wrong key for a cheat.

**Every other key reaches the machine**, and two of the ten function keys
an 83-key board *does* have are now seams' rather than the program's: the
automap panel takes **Tab** and the journal reader takes **F1** (§`seams`,
`docs/seams.md` §10). Neither is a host key — a seam claims them inside
the emulated machine, out of the BIOS buffer, and only while it is on —
so nothing in this table changes and `xt_scancode()` still answers for
both. It is written here because a person looking up "which keys are not
the game's" should find all four in one place.

F11 and F12 work during a `--replay`, because how loudly a person plays a
recording back is not something a recording decides. Pause does not: it
reaches the machine, so a pull at the window during a replay would be an
input the recorded run never had, exactly as a keystroke would be.

`--trace` also prints a line for every file the program names — which one,
which handle, and what DOS answered:

```
amberfolio: file open \SAVE\CHARLIST.TXT handle=0006 none from=0B58:063B
amberfolio: file open \SAVE\CHRDATA1.ITM handle=0000 file_not_found from=0B58:1458
```

The failures are the interesting half: a program asks whether a save slot
exists by opening it, so a run's refusals are as much a record of what it
did as its successes. `docs/machine.md` §5 has the channel's rules.

Those lines are the *live* account, and a boot buries them in tens of
thousands of `INT 16h` polls. So the trace report at the end of the run
carries the last thirty-two of them too (#121):

```
amberfolio: stop trace=on steps_seen=... kept=256 calls_seen=... kept=64 files_seen=... kept=32
amberfolio: stop trace file=open \POR\POOL.CFG handle=0000 path_not_found from=0B58:1458
amberfolio: stop trace file=open \ handle=0000 invalid_drive from=0B58:1458
```

(The counts are elided; the three `..._seen` fields are the whole run and
the three `kept=` fields are the window the ring still has.)

That is the shape of the failure this facility was built for. A hard-disk
install carries a config naming absolute paths, this host mounts the
directory it is given *as the DOS root*, and every path the program builds
from that config then resolves under a subdirectory that does not exist.
The program's own answer to it is to ask for a floppy — and a failed
`INT 21h` open is a legitimate DOS answer, so before #121 the report said
nothing at all about the opens that had just failed. Two rules make
the block readable:

- **A path is never truncated.** By the time an entry exists the name has
  been through `canonicalize()`, which produces a fixed-size `dos_path` or
  refuses — so what the ring holds is what the machine acted on.
- **A name that does not resolve at all renders as `\`.** There is no
  canonical path for `A:\POOL.CFG` on a machine with one drive, so the
  error is what names the failure. It is the one naming refusal that
  happens before the filesystem is consulted, and the one this channel
  reported nowhere until #121.

`--dump` is the one to reach for when the claim is *"the title renders"*.
`docs/machine.md` §7 says why a golden is the wrong instrument for that,
and the answer is the same one `amberfolio-dump` gives for a self-written
program: produce the file and look at it.

A note on `--dump` and sound. Exactly one consumer may pull the speaker's
timeline (`platform.h`), so the capture follows whoever that is — SDL's
audio thread when a device is open, the machine thread when the run is
headless. It holds a minute of virtual time and says `(truncated)` if the
run outlasts it.

**No test in this repository ever runs the game.** CI's proof of all of
the above is the synthetic program on the smoke disk
(`hosts/sdl/tests/make_smoke_disk.cpp`) and the checks in
`cmake/run-stop-report.cmake`; the exit criterion itself is verified
locally against the maintainer's own copy and written down as a
procedure (#92).

---

## 3. What a person still has to check

Two things, and no runner anywhere can check either: **that a photon left
a display, and that a pressure wave left a speaker.** Everything up to the
device is now asserted; the device itself is not.

**And since M6-C1 (#290) there is a third, of a different kind: that the
second launch does not ask.** The code-wheel bypass now waits for a
person to answer the game's own question, so the one moment the whole
enhancement is about cannot be driven by a test — a correct answer needs
somebody holding a wheel, a manual or the code generator application
their copy came with. It is one run, with `--seam code-wheel` on and the
answer typed:

```
amberfolio: host-service code-wheel-answered calls=1 last=0 at=<tick>
amberfolio: code wheel answered - remembered for this copy
```

and then a second run of the same command, which should print

```
amberfolio: code wheel answered on this copy already - the challenge will not be drawn
```

and go from the titles to the menu. Everything under that is checked
without a person: the watch at the register level (`SeamCodeWheel.*`),
the skip driven on a real copy (#291), and the store out to a drawer and
back on both hosts. What is missing is a human being getting it right,
and it stays here until somebody has (#292).

It is worth doing on each desktop target you care about, and it takes two
commands.

```sh
cmake --build --preset <preset> --target amberfolio-sdl amberfolio-sdl-demo-disk
./build/<preset>/hosts/sdl/<config>/amberfolio-sdl-demo-disk build/<preset>/demo-disk
```

That writes a directory with two programs in it. Both are self-written and
in-tree; `hosts/sdl/tests/make_demo_disk.cpp` has the listings.

### `DEMO.EXE` — the one for the senses

```sh
./build/<preset>/hosts/sdl/<config>/amberfolio build/<preset>/demo-disk DEMO.EXE --scale 3
```

Expect, in this order:

1. **A window**, 960×600 for `--scale 3`, titled `amberfolio`.
2. **Sixteen horizontal colour bars**, twelve scanlines each, in EGA
   palette order: black, blue, green, cyan, red, magenta, **brown**, light
   grey, dark grey, light blue, light green, light cyan, light red, light
   magenta, yellow, white, and eight black rows at the bottom where the
   last bar ends. Brown rather than dark yellow is the thing to look at —
   it is the one entry that tells you the EGA's DAC is being applied
   rather than a plain 4-bit ramp.
3. **A 440 Hz tone**, continuous, from the moment the bars appear. Concert
   A, so it is a tone you can name rather than a noise you assume.
4. **Typed characters echoing** to the terminal you launched it from, one
   per keystroke, as you type.
5. **Escape** stops the tone and exits with code 0. Closing the window
   also exits, with code 0.

If the picture is right and the tone is silent, the fault is between
`audio_timeline::render()` and the device — start with whether the audio
stream opened at all. `--verify` will tell you whether the callback ran
and whether what it handed over was silence:

```sh
./build/<preset>/hosts/sdl/<config>/amberfolio \
    build/<preset>/demo-disk DEMO.EXE --verify --press Escape@120
```

### `COMPOSIT.EXE` — the one the suite asserts

```sh
./build/<preset>/hosts/sdl/<config>/amberfolio build/<preset>/demo-disk COMPOSIT.EXE --scale 3
```

This is M2-T1's composite program (#56), byte for byte the one the test
suite checks and the one the wasm dev page embeds — so what you are
looking at is the picture that has a hash and a set of hand-derived pixel
probes behind it. Expect:

1. A **cyan band** twenty scanlines deep across rows 40-59, on black, and
   eight alternating pixels in the very top-left corner.
2. A short **blip** — its tone is twenty milliseconds long, which is the
   reason `DEMO.EXE` exists.
3. Nothing further until you **press a key**: it is halted inside INT 16h
   with the timer still running. Press one and it echoes the character,
   writes `\RUN.LOG`, prints `DONE` and exits with code **90**.

If what the window shows is not what is described above, the question is
whether the host mangled a correct frame or the machine composed a wrong
one, and there is a tool for exactly that split:

```sh
./build/<preset>/tests/programs/<config>/amberfolio-dump composite <dir>
```

It runs the same program through the same machine with no host at all and
writes what came out where you can open it — the composed frame as a PPM,
the speaker's whole timeline as a WAV, and (since #148) the edge list the
speaker published as a `.edges` file in the same format both hosts'
`--dump` writes. If the PPM is right and the window is not, the fault is
in this half; if the PPM is wrong too, it is not, and `ctest -L unit`
will have a good deal more to say about which pixel probe stopped
agreeing. The same split works for the sound, and the `.edges` file is
what makes it work: a right list and a wrong noise is a host or a filter,
and a wrong list is the machine. §4 has the format.

### The dev page, in a browser — the list nobody has walked yet (#147)

**No browser has been opened on any of this.** Everything phase 3 added
to `hosts/web/page/` is checked by a node harness that stubs
`AudioWorkletGlobalScope`, and by reading. That is not the same claim, and
this section exists so the difference is written down rather than assumed
away — and so the session is cheap when somebody does sit down.

Serve the built page (`python3 scripts/serve-web.py build/wasm/hosts/web/<config>`)
and work down the list. Each line is one observation; write the answer
back into #147 or into the commit that touched the page, the way the
desktop list above asks.

- [ ] **The speed select.** Each of the four settings changes the pace of
      the embedded demo visibly, and the machine does not stop when it is
      changed mid-run.
- [ ] **The health readout** — `frames= steps= | audio underruns=
      resyncs= starved=`. It moves; the three audio numbers stay at or
      near zero on an idle machine and grow when the tab is backgrounded,
      which is the symptom they were added to name (#106).
- [ ] **The AudioWorklet's hold-then-fade.** Starve it deliberately — drag
      the window, background the tab — and the tone should hold at the
      last real sample and fade rather than click or buzz. This is the one
      item on the list that is a *pressure wave*, and it is the reason the
      node harness cannot close it.
- [ ] **The seam checkboxes against a real binary.** A recognized edition
      names itself on the edition line, the seams for it list as
      available, and toggling one shows the state beside its name going
      `off` → `armed fired=0` → `armed fired=N` as the run reaches its
      point. `armed fired=0` at the end of a run that should have fired is
      the failure to look for (#131) — and it is a browser-visible number
      only since #147, so this line is checking the new thing as well as
      the old one.
- [ ] **The directory drop of a real installation.** Drop the folder, not
      the files; the file count and the skipped list are plausible; the
      `.EXE`s sort to the top of the program list; a second drop replaces
      the first rather than merging. "Plausible" now means *counted*:
      #158 was found because it was not, and an installation that fits
      should report nothing at all under "out of room".
- [ ] **The legs of `docs/playable.md`, in the page.** None of them has
      been driven in a browser — only through `tools/drive.mjs`, which is
      node. Leg 1 (a party to the roster) is the one to do first, because
      everything after it depends on the keyboard path being right.

And one item that is not the page's and needs no browser, added at the M4
closeout because the instrument for it landed there (#148):

- [x] **The two hosts' edge lists, diffed** — done, against the game, and
      it failed the first time (#273). Legs 3, 4 and 5 of
      [`docs/playable.md`](playable.md) were driven on both hosts off one
      script each, and the `.edges` files disagreed on every tick in them
      while agreeing on every count and duration: the two hosts' frame *N*
      was not the same tick, so the same script pressed its keys at
      different moments. §4's "What the first diff of two hosts' edge
      lists found" has what differed, by how much, and why nothing else
      here could have shown it. All three legs' files are identical now.
      What is **not** done is §4's first open question — reading the
      divisors out of one of those files to say which tones this program
      programs. The files exist; nobody has run `awk` over them.

Nothing above is a blocker for anything: the wasm module itself is checked
continuously (§5), and the page is the thin layer over it. What is
unchecked is the layer where a browser API is used, and browsers are
exactly the thing a headless harness stubs.

**Who is coming for this list, as of the M5 closeout: #274.** It outlived
its own issues — every unticked line here belongs to #147 or #148, both
of which have had nothing else in them for two milestones — so #274 is
the one place that carries them forward, together with the two lines M5
added and could not tick: an entry read on a display, and the overworld's
fog walked rather than looked at in a still. Two of M5's own person-items
*were* ticked, and both changed an enhancement rather than confirming
one — the Encamp Fix's report box, and the automap panel in play, which
is why M5-E2d exists (#199). That is the argument for keeping the list:
it is the only kind of finding no runner here has ever made.

### Recording what you found

The last inch is the part that stays a person's word. When you have run it
on a target, say so where the next person will look — the issue, or a line
in the commit that touched the host. "Ran `DEMO.EXE` on macOS 14, arm64:
bars correct, tone audible, keys echo" is worth more than any number of
green runners, because it is the only claim any of them cannot make.

The same goes for the browser list above, and more so: a checkbox nobody
signs is worth less than an unticked one, because an unticked box at least
says what it is.

---

## 4. The speaker, measured

The paragraph above asks a person to listen for concert A. That is the
right check for "did a pressure wave leave the speaker", and it is the
wrong one for everything else — #106's own words for it are "it sounds
right in a quiet room to one person". This section is what replaced the
adjectives, and what is still owed.

### The two questions, and which artefact answers which

`platform.h` is emphatic that the **edge list** — "at tick T the speaker
output became high" — is the canonical audio state and the float samples
are not. That makes two separate questions, with two owners in the code:

1. **Is the machine producing the right edges at the right ticks?** The
   speaker, the PIT and the gate bit own that.
2. **Is the render of those edges right?** `audio_timeline::render()` owns
   that, and nothing else does.

Until M4-A1 only the second could be inspected: `--dump` wrote a WAV, and
a WAV is a rendering. The edge list had a count and a digest and no way to
read it. It has one now.

```sh
./build/<preset>/hosts/sdl/<config>/amberfolio <dir> DEMO.EXE \
    --verify --press Escape@60 --dump /tmp/tone
```

`/tmp/tone.edges` is a text file — a two-line header saying the tick rate,
then one `tick level` line per transition, then a `# edges N dropped M`
trailer so a truncated dump is told apart from a quiet run:

```
# amberfolio audio edges
# pit-input-hz 1193182
# tick level
31488 1
32844 0
34200 1
```

`32844 - 31488` is 1356, which is half of 2712, which is the divisor
`DEMO.EXE` writes to channel 2 — 1,193,182 / 2712 = 440.0 Hz. That is a
fact about the machine, checkable with `awk`, that no amount of listening
would have produced. `sdl-host-sounds-a-tone` now checks exactly it: over
the second the run lasts, 857 of the file's 858 edges are 1356 ticks apart
and the odd one out is the last — the program clearing the gate on its way
out, which lands wherever the keystroke did.

The log inside the machine is off unless `--dump` asks for it, drains to
the host every frame rather than accumulating, and is read only on the
machine thread — so it cannot perturb what the audio thread's `render()`
sees, and it is not part of machine state. A unit test asserts both: the
rendered samples are bit-identical whether or not somebody was reading,
and a machine being watched has the same state hash as one that is not.

**Three writers, one file** (#148). The list had one writer until the M4
closeout, and the gap that left is the one worth naming: the desktop host
could ask question 1 and neither the browser nor the host-free dump tool
could, so a wrong-sounding browser run had no way to say which half was
wrong. All three write the same file now, header and trailer included,
which is what makes them comparable rather than merely similar:

| writer | how | what it is for |
| --- | --- | --- |
| `amberfolio --dump PREFIX` | streamed, drained every frame | the desktop host, with a real disk in front of it |
| `node drive.mjs … --dump PREFIX` | the same, over `af_machine_audio_read_edges` | the browser's machine, headless — §5's driver |
| `amberfolio-dump <program> [<dir>]` | `machine_setup::log_edges` | no host at all, which is what §3 sends people here for |

The ABI door is four calls — `af_machine_audio_log_edges`,
`_logging_edges`, `_read_edges`, `_edges_dropped`/`_edges_pending` — and
they carry the same "not machine state" promise the C++ side does:
`abi_test.cpp` runs one tone twice, once watched and once not, and
asserts the two state hashes are equal. The wasm smoke check drives the
whole door on the embedded demo's tone, and asserts what a tone is: ticks
strictly increasing, levels alternating, nothing dropped, and a drained
log empty.

The three-writer rule has a use beyond tidiness. A run dumped on both
hosts produces two `.edges` files that must be **identical** — the same
ticks, the same levels — and that is a stronger statement than two WAVs
agreeing, because it compares the machines rather than two renderings of
them.

### What the first diff of two hosts' edge lists found (#273)

The paragraph above stood for two milestones as a thing that *must* be
true and had never been done against the game. Driving
[`docs/playable.md`](playable.md)'s legs 3 to 5 on both hosts is where it
finally was, and it failed — for a reason that was in neither speaker, and
that nothing else in this apparatus could have shown.

**What differed.** One script, one disk, one leg: leg 3's save. Both hosts
reported the same stop reason at the same tick and the same step count,
both wrote the same four files into `\SAVE\`, and three of the four had
equal digests. The fourth — the slot file itself — did not. The two runs
stopped on different instructions. The final stills differed by 36 pixels,
all of them inside the camp screen's animated fire. And the two `.edges`
files disagreed on **every tick in them**, by 65,536 at the first edge and
by varying amounts after, while agreeing exactly on how many edges there
were and what their levels and durations were. That last shape is the
tell: not a different sound, the *same* sound at a different moment.

**By how much, and why.** `--press KEY@FRAME` did not name the same tick
on the two hosts. `tools/drive.mjs` kept an absolute schedule — `next +=
af_ticks_per_second() / 60`, which is `abi.h`'s own run-loop snippet — and
the SDL host takes its boundary off the machine's clock each time round,
`box.time() + machine::ega::frame_period`. Those differ because **a slice
does not end where it was asked to**: a step is atomic, so `run_until`
finishes the one it is in and overshoots by a step's worth of ticks. The
SDL host carries that overshoot into the next boundary and the driver
threw it away, so the desktop's frame ran 19,888 ticks against the
driver's 19,886.37 and frame *N* drifted apart by about a tick and a half
a frame — **15,200 ticks by frame 7,600**, which is where leg 0 answers
the code wheel, and 8,000 more by the end of the leg. Three quarters of a
frame, at the end of a run twenty-three thousand frames long.

**Why it was invisible until now.** Nothing in the tree drove a program
long enough or key-heavy enough to feel it. The wasm smoke test's runs are
four frames; the cross-host comparison §5 ends on is a stop line, which
this does not move, because a tick budget decides the step count whatever
happened inside it — `steps=` matched exactly in the failing case and
proves nothing on its own. And the sessions all replay rather than drive:
`--replay` takes its key ticks out of the recording and never asks either
host what a frame is, so the twenty-one game sessions that went through
the wasm module at the M5 closeout (#177) were silent about it by
construction. It took a driven run, of a program with a clock in it, with
the `.edges` file beside the still.

**What it was not.** Not the machine. The same desktop run, recorded and
handed to the wasm module, verified at 183 checkpoints while its driven
twin was diverging — every byte of RAM, every device register, at every
one of them. The two hosts were told to press a key at frame 20,400 and
pressed it at two different moments; everything downstream is the program
answering an earlier or later keystroke, exactly as it would have on a
real machine.

The driver takes its frame off the machine's clock now, as the SDL host
does. With that one line changed, all three legs' final stills are
byte-identical, all three `.edges` files are identical, and every file the
program wrote has the same digest on both hosts. `abi.h`'s advice is not
wrong where it is aimed — an absolute schedule is what stops a *paced*
caller drifting against the wall clock, and that is the dev page through
`pacedAdvance()` (#157). It is wrong for a driver whose whole job is to be
the other host's twin, and which has no wall clock to drift against.

The lesson is the cheap one and the expensive one at once: **an invariant
that is only asserted in prose is not asserted.** This one had a tool, a
format and a paragraph saying the files must be identical, for two
milestones, and was false the first time somebody ran `diff`.

### What the box filter actually does

`tests/core/machine/platform_test.cpp`'s `AudioFilter` suite measures the
reconstruction rather than describing it. The numbers, all reproducible by
running that suite:

| measurement | value |
| --- | --- |
| mean of a 50% tone over whole periods | **0.125**, to eleven decimal places |
| duty recovered from the samples, at 12.5 / 25 / 50 / 87.5% | the duty, to 1e-11 |
| a 1000.99 Hz tone at 44,100 | **1000.908 Hz**, mean 0.125124 |
| the same edge list at 48,000 | **1001.043 Hz**, mean 0.125125 |
| the two rates' disagreement | **0.013% in frequency, 1.5e-6 in mean** |
| where a rising edge lands against the fitted period | within **0.82 samples** at 44,100, **0.75** at 48,000 (19 µs, 16 µs) |

Two findings follow.

**The DC offset is real, it is 0.125 at 50% duty, and it is a property to
document rather than a defect to fix in `render()`.** Samples run 0.0 to
0.25 and never go negative, by the design decision that makes silence
exactly 0.0 (#49) — so a tone carries a quarter-scale offset for as long
as it plays. The filter is not wrong: 0.125 *is* the average of a unipolar
square, and the real cone is displaced too. The caveat is the one #106's
third comment found: a *gate held on* is not a tone. The game's combat hit
is 19 ms of constant 0.25 with no sign change at all, and a sink handed
that gets a thump and a settle rather than a click. If anything is done
about this it should be a high-pass in a host's reconstruction, chosen
against that burst — and **not** in `render()`, because the samples would
stop being the exact integral of the edge list, which is the one thing
that filter is for.

**Nothing here argues for a better-than-box filter in v1.** The two rates
the two hosts actually pull at hear the same note to about a
two-thousandth of a semitone, and what separates them is a sample of edge
placement — quantisation the pull rate imposes, which no filter removes.

### Underruns and resyncs, and how a person sees them

`platform.h` states a policy for each, and until M4-A1 no host read either
counter, so both were specified and unreportable. Every run now prints a
line when there is something to say, and `--verify` prints it always:

```
amberfolio: audio underruns=11 resyncs=0 dropped edges=0
```

- **underruns** — calls where the next sample would have reached past the
  settled horizon. The policy is *hold the last level and do not advance
  the cursor*: nothing is lost, and playback resumes at exactly the tick
  it stopped at when the machine catches up. A windowed run almost always
  underruns a few times at the start, because SDL's device pulls before
  the machine has settled any virtual time at all; the eleven above are
  that, in a one-second run. A number that grows through a run is a host
  that cannot keep up.
- **resyncs** — calls where the horizon had run more than 200 ms ahead of
  playback. The policy is *jump the cursor to 20 ms behind the horizon and
  throw the backlog away*: latency is bounded by construction rather than
  by hope. Expect these under `--fast`, which produces sound faster than a
  48 kHz device can consume it, and under a dragged window or a stalled
  tab.
- **dropped edges** — the ring itself overflowed, which is the loud one:
  sound the machine made that no host ever got. It should be zero, and the
  smoke test asserts that it is.

### Volume and mute, and why neither is in core

#106's scope named them and phase 3 shipped neither; #148 item 4 is where
that was written down, and this is the answer to it. **Both hosts have
them and core has nothing of them at all** — no field in
`audio_timeline`, no ABI export, no line in a recording.

The reason is one sentence long and `platform.h` had already written it
about a different filter: a sample out of `render()` is the *exact
integral of the edge list* over its interval, and that identity is what
every number in this section measures. A gain inside `render()` would
make a sample the integral times a number nobody wrote down — so
`--dump`'s WAV, the 0.125 mean and the duty recovered to 1e-11 would
become statements about the listener's volume rather than about the
machine. The same paragraph that refuses a high-pass there refuses this.
Three more reasons, in `hosts/sdl/src/audio_gain.h`; the shortest is that
a level is a fact about the room the player is in, and nothing in the
machine may observe one.

So it is two implementations of one decision, the way the underrun policy
already is, and each side is measured on its own:

| | desktop | browser |
| --- | --- | --- |
| set by | `--volume 0-100`, `--mute`; **F11** mutes, **F12** steps | a slider and a checkbox beside the speed select |
| applied in | `audio_gain::apply()`, on SDL's audio thread, after `render()` | `audio-worklet.mjs`, on the audio thread, as each output sample is written |
| crosses the thread boundary as | a lock-free `std::atomic<float>`, relaxed both ends | a `{ gain }` `postMessage`, drained between quanta |
| measured by | `hosts/sdl/tests/audio_gain_test.cpp`, `sdl-host-mutes-the-tone` | the worklet block in `hosts/web/tests/smoke.mjs` |

Four properties hold on both, and each is a test rather than a sentence:

- **Unity is a no-op, not a multiply.** With the volume where it starts,
  what reaches the device is *the same bits* `render()` produced. That is
  the guarantee that keeps the table above true of what a player actually
  hears, and `AudioGain.UnityLeavesTheDcOffsetAndTheDutyExactlyWhere
  TheyWere` measures the mean and the duty on both sides of the gain and
  demands the same `double`, not a tolerance.
- **Mute is arithmetic silence** — every sample exactly `0.0F`, the value
  #49's representation reserves for it — and on the browser side that
  includes the level the worklet *invents* when it starves. That is why
  the gain is inside the worklet rather than applied to the chunks as
  they are posted: a mute that left the held level alone would not
  silence a stalled tab.
- **A change glides**, over six milliseconds, landing on the target
  rather than approaching it. A gain that stepped would put a
  discontinuity in the output at the moment the key was pressed — a click
  this host made, which the machine never generated. Same span and same
  reasoning as the worklet's fade out of an underrun.
- **Neither host amplifies.** 100% is what the machine made, and a
  request above it is clamped; the loudest thing a player can hear is
  `render()`'s own output.

Two things follow that a person reading a run's output will meet:

- **`--dump`'s WAV is captured before the gain, `--verify`'s `sounded`
  count is taken after it.** The WAV is the artefact §3 sends you to when
  the question is "machine fault or host fault", so it is what the
  machine made and not what you chose to hear — a muted run still dumps
  its tone. `sounded` is the opposite question, "what reached SDL's
  stream", so a muted run reports `sounded 0` truthfully. The `.edges`
  file was never anywhere near either.
- **The report says so when it is not unity**, and only then:
  `amberfolio: audio underruns=0 resyncs=0 dropped edges=0 volume=muted`.
  A default run's line is the line it has always been.

`sdl-host-mutes-the-tone` is the end-to-end half: the same `DEMO.EXE`,
the same window and device, `--mute` added, and `sounded 0` expected. It
means something only because the case beside it proves the same program
sounds — silence that was never going to be anything else is no check.
F11 and F12 are keys an 83-key XT board does not have, so
`sdl::xt_scancode()` answers 0 for both and the emulated program loses
nothing by the binding; `keymap_test.cpp` pins that, because it is the
assumption the binding rests on. Pause/Break is the third key on that
argument (§2's key table, #161).

### What this section does not settle

- **The game as the workload.** The measurements above are of self-written
  tones. Combat, doors and spell effects have been heard once, by one
  person, on one machine (#106's second and third comments); no capture of
  them exists in this repository and none ever will.
- **Whether it sounds right.** No measurement replaces §3's ear — and
  that now includes whether 25% is a useful quarter rather than an
  inaudible one, which is a judgement about a room and not a number.
- **A resync produced on purpose.** The *reporting* of one is tested and
  the path itself has only ever been walked by a unit test. A stalled
  tab, a dragged window, or `--fast` past what a 48 kHz device can
  consume should produce one, and nobody has run that experiment
  (#148 item 3).
- ~~**The browser's edge list.**~~ Closed at the M4 closeout: the ABI has
  the edge-log door, `tools/drive.mjs --dump` writes the file, and the
  smoke check asserts the tone through it (#148 item 5, and item 6 for
  `amberfolio-dump`). What remains of #148 is the three items above,
  every one of them a person's.

---

## 5. The wasm host

`ctest --preset wasm` runs the module under node, headless. It asserts the
ABI's export list, the embedded demo program's framebuffer hash and key
echo, the filesystem path a player's directory travels (M3-F2, #84), and
— since M4-W1 (#108) — the headless driver below and the speaker
worklet's underrun policy. The browser half (canvas, AudioWorklet,
keyboard) has the same shape of gap the desktop host had, and
`scripts/serve-web.py` plus a browser is how a person closes it.

```sh
cmake --build --preset wasm
python3 scripts/serve-web.py
```

### Two things a page serving this module has to know (#211)

Neither is checkable by a test here, and both are the kind of fact a
consumer would otherwise discover from a lockfile bump that fails to
instantiate. They are stated rather than checked, which is why they are
in this document and not in a script.

**Single-threaded, and that holds through 1.0.** The module is built with
no `-pthread` and no shared memory: there is no `SharedArrayBuffer`
anywhere in the glue or the page, no worker pool behind the machine, and
the one worker the page does run — the AudioWorklet — receives each chunk
by `postMessage()` with its buffer *transferred*, never shared
(`audio-worklet.mjs`'s own top comment is the long version, and
`machine/platform.h`'s "one thread, any thread" is the contract it
implements). `af_machine_run_until()` is called on the main thread and
`af_machine_render_audio()` beside it.

The consequence is the one a host cares about: **a route serving this
module does not need `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`.** Cross-origin isolation is
what threads would cost, and it is not free — it takes the page out of
third-party embeds and makes every asset on it prove it wants to be
there. This build does not ask for it.

That is a decision and not a gap waiting to be filled. The machine is an
interpreter with one instruction stream and a deadline scheduler
(`docs/machine.md`), so there is no second thing for a second thread to
do; and virtual time is the only clock, which is what the recordings rest
on (`docs/replay.md`) and what is easiest to keep true with one thread in
the picture. If it ever changes, those two headers become mandatory on
the route, that route stops being embeddable, and this paragraph is where
it will say so.

**The ABI states its own version, in the manifest, before anything is
loaded.** `af_version()` says which *build* this is — it tracks
`project(VERSION ...)` and moves every milestone, so it answers nothing
about whether a loader can drive the module. The ABI's own major/minor is
`AF_ABI_VERSION_MAJOR`/`_MINOR` in `core/include/amberfolio/abi.h`, with
the bump rule stated at the point of definition, and
`scripts/release-bundle.sh` reads it out of that header — along with the
module's whole export list, read out of the `set()` block in
`hosts/web/CMakeLists.txt` that feeds `-sEXPORTED_FUNCTIONS` — into the
release's `manifest.json`:

```json
{
  "version": "0.3.0",
  "abi": { "major": 1, "minor": 1 },
  "sourceCommit": "a827d9ea…",
  "files": [ { "name": "amberfolio.wasm", "sha256": "…", "size": 0 } ],
  "exports": [ "_main", "_malloc", "_free", "_af_version", "…" ]
}
```

**major** moves when an entry point is removed or renamed or changes what
it means; **minor** moves when entry points are added and nothing that
was there changed. So a loader compares `major` against what it was
written for, refuses a bundle it cannot drive before it fetches a
megabyte of wasm, and has `exports` to say which entry point it wanted
when it wants to be specific — all without instantiating anything.

Both are read rather than repeated, and `scripts/test-release-bundle.sh`
refuses a release whose header talks about the version without defining
one, whose export block has moved out from under the parser, or whose
export list comes back without `_af_version` in it. A manifest that
disagrees with the module it describes would be worse than one that said
nothing: it would be believed.

One case is deliberately *not* a refusal. `release.yml` runs the current
bundler against the tree of an older tag — current tool, historical
content — and `v0.1.0` and `v0.2.0` predate the declaration entirely. A
tree with no `AF_ABI_VERSION_*` in it stages with a notice and **no
`abi` key**, because saying nothing about a contract that did not exist
yet is the honest answer and inventing 1.0 for it is not. Its `exports`
are still that tree's own: re-staging `v0.2.0` today lists the
seventy-eight entry points it had, not the hundred and twenty-four this
branch has. A consumer reads an absent `abi` as "older than the
declaration" rather than as 1.0.

### Two doors a consuming site asked for (#228, #229)

Both landed together in M5-C1 and are the first change to move
`AF_ABI_VERSION_MINOR`: 1.0 to 1.1, once, because they were both in the
bundle before the tag. Each adds entry points and changes nothing that
was already there, which is exactly what minor is for.

**1.2 is the code wheel's once** (#291): `af_machine_code_wheel_answered`
and `af_machine_set_code_wheel_answered`, two more entry points and
nothing changed. The *behaviour* under them changed a good deal — no
seam is gated any more, and the one that was now waits for a person to
answer the program's own challenge (#290) — but behaviour is what a
build's own version says, not what an ABI's does: a consumer that calls
the same entry points in the same way still gets an answer of the same
shape.

**`af_machine_vfs_generation()` — did the disk move?** A host that
persists what the program wrote runs that write-back inside its own frame
loop, and needs a cheap answer sixty times a second. Walking `\SAVE\`
per frame is not cheap, and polling at quiet moments cannot tell "nothing
changed" from "we did not look" — the save a player made two seconds
before closing the tab being exactly the one they care about. So the
filesystem gets the counter `machine::display` has had since M2: one
monotonic number, kept by the caller, compared against the last one it
saw. `Machine.vfsGeneration()` is the page's spelling and
`tools/drive.mjs` prints it beside `--vfs-list`.

It moves for a write that landed bytes, a create, a truncate, an unlink
and a mkdir — whether the program made them through INT 21h or a host
made them through `vfsPut()`, `vfsRemove()` or `vfsClear()`. That last
part is deliberate: a host that hands over an installation and then reads
the counter sees it move, because a counter that tracked only the
program's own writes would be a second, subtler answer to the same
question. It does **not** move for an open, a read, a seek, a close, an
`exists`, a `stat`, or a directory listing. The listing is the one this is
careful about: the game's own load menu enumerates the save directory
every time a player opens it, and a counter that moved for that would be
one a write-back loop could not use at all.

What it is *not*: an mtime, and not a diff. It says something changed,
never what or when, and one host-level call can move it more than once (a
put is a create and a write). The walk that finds out what changed is the
caller's — the counter exists so that the walk happens when something
happened, rather than on a timer. `machine/vfs.h` states the whole rule
at the interface that owns it, and the five operations that move it are
non-virtual wrappers there around protected virtuals, so a new backend
cannot forget to move it.

**`af_web_journal_store_changed()` / `_clear_changed()` — should I save
the journal?** `host::journal_store` has carried the flag since M5-E4b
and nothing exported it, so a page's only way to ask was to serialize the
whole store to a string and compare it against what it last persisted: a
hash over everything, every interval, to learn a boolean the store had
already raised. Every write to the store raises it now, not just the
citation log — a correction that did not raise it is a correction that
quietly never gets saved — and reading a store *in* is the one write that
does not, because those bytes came from the caller.

**The lowering is the caller's, and that is the design, not an
omission.** A store cannot know whether `localStorage` accepted the
bytes, and a flag that cleared itself when it was read would lose a
correction made between the read and the write. So a host reads the flag,
writes the store out, and lowers it once the bytes are somewhere.

Both of the journal calls, plus `journalStoreWrite`, `journalStoreRead`
and `journalStoreStats`, are `Machine` methods now. They delegate to
`page/journal.mjs`, which stays the implementation: the ingestion there
is asynchronous, engine-driven and page-shaped and has no business
becoming `Machine` methods, but the *store* was the one thing a page
needed that was not behind the façade. `host.mjs` therefore imports
`journal.mjs`, which is why **`journal.mjs` is in the release bundle**
from this release on. It was owed before: `app.mjs` has imported it since
M5-E3 and it was never staged, so a consumer serving the released
`app.mjs` got a 404 for a file it asks for by name.

### Running your own copy in a browser

The page has two halves. **start** runs the embedded demo program — a
continuous tone and an echo loop, for exactly the reason `DEMO.EXE` has
both. **run your own copy** is #84's: choose or drop a directory, pick a
program, press **boot**.

Nothing is persisted. The files live in the machine's own in-memory
filesystem for as long as the page is open, a reload starts from empty,
and nothing leaves the browser. Onboarding, fingerprint UX and IndexedDB
are M6's reference shell; this is a dev-page affordance and is not trying
to be more.

A few things are worth knowing about what it does:

- **Paths are decided in core, not by the page.** Every one goes across
  the ABI as the player's own text and is canonicalized by
  `machine::canonicalize()` — the one implementation of DOS short-name
  rules. A name no DOS 8.3 name can equal is refused and listed as
  skipped, which is how a boxed copy's PDF ends up outside the machine
  without the page ever having looked at a file extension. Since #146 it
  is a *path* and not merely a name: the picker keeps the
  `webkitRelativePath` it used to throw away, `/` and `\` are one
  separator at this door, and core makes the directories a path names. A
  player who arrives with a `\SAVE\` arrives with the slots in it, which
  is what `LOAD SAVED GAME` needs and what the page could not do before.
- **A refused file says which kind of refusal it was** (#158). There are
  two, and they are opposites. A path DOS could never have named is the
  machine working, and the page lists it as ignored. A file the
  filesystem had no room for is a *hole in the disk about to be booted* —
  `AF_NO_ROOM` at the ABI, its own line in the console and its own
  clause in the status text. Both were one status until #158, and a
  browser handed a real installation reported seven of the game's data
  files in the same sentence as a PDF it was right to ignore; the disk
  ran, with holes in it, and the first anyone knew was that no save could
  be written. `tools/drive.mjs` prints the same two sentences, from the
  same `describeSkip()` in `host.mjs`.
- **The stop report is the same text the desktop host prints**, because
  it is formatted in core (`machine/report.h`) rather than by either
  host. That is what makes the comparison below possible at all.
- **The cursor keys work.** On an 83-key XT board the arrows, Home/End,
  Page and Insert/Delete *are* the keypad, and they now map onto the same
  scancodes. They were simply absent until #84, which made a
  keyboard-driven game unplayable in a browser while the desktop host had
  had them since M2-H1; `hosts/web/tests/smoke.mjs` checks the rows.
- **The speed preset is a control, not a build option** (#107, #108). The
  same four names the desktop host's `--speed` takes, applied whenever it
  changes rather than only at boot — the useful thing to do with it here
  is to turn it up while watching the readout under the canvas and find
  where the browser stops keeping up. It is a governor and not a
  fast-forward: virtual time still decides every deadline, so a run at
  `at` is exactly as deterministic as one at `xt`.
- **Virtual time is paced against the wall clock, not against the
  display** (#157). See below: it looked right on every 60 Hz monitor and
  ran the machine at 4x on a 240 Hz one.
- **The volume slider and the mute box are the page's, not the
  machine's** (#148). The gain is applied inside the speaker worklet, on
  the audio thread, where the held level of an underrun is also made —
  §4 says why that is the only place from which a mute actually mutes.
  Nothing about it reaches the machine, is recorded or is hashed, and the
  readout under the canvas says `volume=` only when it is not at 100%.

### How the page keeps time

The desktop host owns its own cadence: it runs the machine forward one
60 Hz frame of *virtual* time, presents, and sleeps whatever *wall* time
is left over (`hosts/sdl/src/main.cpp`'s top comment, PLAN.md §4). A
browser does not own its cadence. `requestAnimationFrame` fires at the
**display's refresh rate**, and until #157 the page advanced virtual time
by one 60 Hz frame per callback — so the monitor was a speed control
nobody had asked for:

| display | how fast the machine ran |
| --- | --- |
| 60 Hz | 1.0x — correct, and why it went unnoticed |
| 120 Hz | 2.0x |
| 144 Hz | 2.4x |
| 240 Hz | 4.0x |

It compounded with the speed preset rather than replacing it: the preset
sets steps per *virtual* second, and this set virtual seconds per *real*
second. `--fast 4` on the desktop host and a 240 Hz monitor here were the
same arithmetic, and only one of them was asked for.

The loop now does what the desktop host does, with the arithmetic turned
round to suit a caller that does not choose when it is called. rAF hands
the callback a `DOMHighResTimeStamp`, and `pacedAdvance()` (in
`host.mjs`, so it is a pure function a test can drive) answers where the
elapsed *real* time says virtual time should be. The refresh rate then
decides only how finely a second is chopped up, not how many seconds
there are — which is rAF back in the role PLAN.md §4 allows it, throttling
presentation and nothing else. Presentation itself never moved: a frame
is drawn when `frameGeneration()` says a new one exists (`platform.h`'s
pull contract), which on a 240 Hz display is now every fourth callback.

**The catch-up is clamped at a tenth of a second, and what is past the
clamp is dropped rather than banked.** A backgrounded tab, a breakpoint
or a laptop that slept hands the next callback a delta of minutes;
advancing by all of it would be exactly the burst of emulated
instructions the SDL host's loop is written to forbid. So virtual time
falls behind the wall and *stays* behind, which is precisely what the
desktop host does when it declines to sleep. The clamp is also what makes
a browser that cannot keep up settle instead of diverge: without it, each
callback that overran would ask the next for more virtual time than the
last. A tenth of a second is six 60 Hz frames — long enough that ordinary
jitter (a dropped frame, a GC pause, an rAF throttled to 30 Hz on
battery) is absorbed rather than lost, short enough that anything past it
is a stall and not jitter. The readout under the canvas adds `stalls=N`
when it has happened, by the same rule `volume=` joins that line: a run
that kept up reads as it always did.

There is deliberately **no fast-forward control on this page**. The
desktop host has `--fast N`; if this one ever grows one it will be a
control somebody chose, off by default, and not a property of the monitor
they happen to own.

`hosts/web/tests/smoke.mjs` drives `pacedAdvance()` with a synthetic
clock, because the defect is browser-only by construction — there is no
rAF and no display in the node harness, which is the point #147 makes
about that whole checklist. The assertion that says it is fixed is that
**100 callbacks at 240 Hz advance the same virtual time as 25 at 60 Hz**,
they having covered the same wall time; the clamp, a first callback with
nothing to measure against, a clock that went backwards, and the callback
*after* a stall (which must advance its own delta and no more) are
checked beside it.

### What a browser run says about itself

Until M4-W1 (#108) the answer was "almost nothing". The diagnostics sink
is a C++ interface that hands out structured records held by reference
(`machine/diagnostics.h`), and `abi.h`'s rules are the opposite of every
word in that sentence — no structs by value, no ownership across the
boundary, nothing that can throw — so the sink does not cross it. A
desktop run printed its notices, its file activity and every seam
transition; a browser run of the same program printed the number it
stopped with.

What crosses now is the same thing the stop report crosses as: **the
account, formatted in core**. `machine::format_diagnostic()` renders one
line per record, `machine/log.h` keeps the last few kilobytes of them
beside the machine, and `af_machine_read_log()` hands the characters over.
The SDL host renders the *same function's* output to stderr, so a line
about a given record is character for character the same on both hosts —
which is the property `#84`'s comparison rests on, extended from one line
at the end of a run to the whole log during it.

Three things follow that are worth knowing before you drive a leg here:

- **The log is not machine state.** It sits beside the machine rather than
  inside it: nothing in it is hashed, saved or replayed, and
  `af_machine_reset` leaves it alone (`af_machine_clear_log` is the
  host's own broom). That is what let it exist without moving a single
  replay hash.
- **`af_machine_set_trace` owns both halves of one facility** — the trace
  ring, all three of it (steps, service calls, naming file calls), *and*
  the service-call and file-event streams, exactly as the SDL host's
  `--trace` does. Notices and seam transitions are always kept; those two
  are not, because a boot makes tens of thousands of them.
  `af_machine_trace_report` renders the ring, file lines included, so a
  browser run's account of which files failed to open is the same block
  of characters the desktop host prints.
- **With tracing on, a browser run is a sample plus a count, not a
  transcript.** The ring is bounded and a program in a tight `INT 16h`
  poll outruns any per-frame drain — `smoke.mjs` drives exactly that and
  drops twelve thousand lines doing it. A line that will not fit is
  dropped whole and counted rather than written in half, the page prints
  the count when the run ends, and the desktop host is still the place to
  take a full trace.

### Driving it headlessly: `tools/drive.mjs`

Everything above describes a page a person clicks. M4-W1 (#108) adds the
other thing the desktop host has had since M3: a way to *spell* a run.

```sh
cmake --build --preset wasm-release
node build/wasm/hosts/web/Release/drive.mjs <dir> <PROGRAM.EXE> [options]
```

It is the dev page's own run loop with the browser taken out — the same
`host.mjs` `Machine`, the same one-frame-per-iteration cadence `abi.h`
documents, the same audio pull, the same log drain — and it takes a
directory exactly as the SDL host does, walking the subdirectories under
it (#146) and handing each file over at its path relative to the top. Its source is `hosts/web/tools/`;
the build places it beside the module and the page's own files, which is
why it imports `./host.mjs` with no path in it.

| option | what it is for |
| --- | --- |
| `--frames N`, `--until TICKS`, `--steps N` | bound the run. With none of them it ends when the machine does, which for a program that never stops is never. |
| `--press KEY@FRAME` | post a key at the top of frame `FRAME`, counting 60 Hz frames of virtual time. Repeatable. |
| `--pull ID@FRAME` | pull a seam's trigger at the top of frame `FRAME`, in the desktop host's spelling (#161). |
| `--seam ID` | turn one seam on after the load and before the first step. Repeatable, and a refusal **ends the run**: a script that asked for the cheats seam and silently got a plain machine would be the worst outcome this apparatus has. |
| `--seams` | list every seam this build carries, in the state the run would have started in, and exit without running. |
| `--speed xt\|turbo\|at\|386` | the governor, spelled as on the desktop host. |
| `--trace` | the trace ring and the service-call and file channels, as `--trace` does there. |
| `--automap-store` | the exploration sidecar, as `--automap-store` does there — the same object, the same filenames and the same bytes, in this module's own filesystem rather than a directory. Turned on after the files are in and before the program is loaded, because turning it on is what reads the working table back. |
| `--dump PREFIX` | `PREFIX.ppm` (the last composed frame), `PREFIX.wav` (the speaker rendered) and `PREFIX.edges` (the speaker as the machine published it, #148) — the same three files the SDL host's `--dump` writes, in the same formats, so a leg run on both hosts can be diffed rather than described. |
| `--dump-every N` | also `PREFIX-NNNNNN.ppm` every N frames — the "what did the screen *do*" instrument, which the browser had no equivalent of. |
| `--replay PATH` | be the run a recording describes, and check it. The recording decides the seams and the speed; nothing else runs, and the answer is the verdict. |
| `--vfs-list` | every file on the disk after the run, at its own path — what the run left behind, `\SAVE\` included, in the SDL host's spelling. |
| `--vfs-get PATH` | one file read back through the door after the run, printed as its size and the SHA-256 of the bytes that came back and never as bytes (#273). Repeatable, and the SDL host's flag spelled identically — it is how "the two hosts wrote the same save" becomes a comparison of two digests. |
| `-- ARGUMENTS` | the command tail, with DOS's leading space. |

Two differences from the desktop host, both deliberate and both worth
knowing before a comparison is made — and one that used to be here by
accident:

- **A frame is the same tick on both hosts**, and was not until #273. The
  boundary is `machine.time() + Math.trunc(ticksPerSecond() / 60)`, taken
  off the machine's clock each iteration exactly as the SDL host's
  `box.time() + ega::frame_period` is, so a slice's overshoot is carried
  rather than dropped and `--press E@20400` names one tick rather than
  two. §4's "What the first diff of two hosts' edge lists found" is what
  that cost while it was untrue.
- **It carries an empty directory** (#273). A directory the walk put
  nothing into is made anyway — a put and the remove that leaves the name,
  because the ABI has no `mkdir` and should not grow one — and the disk
  line says how many with `dirs=`. A freshly installed copy's `\SAVE\` is
  empty, so this is not a corner: it is the first disk anybody drops on
  the page, and until it was carried the two committed sessions recorded
  over a pristine disk were refused here before a step was taken.
- **Key names take either spelling.** `--press KeyA@60` is the browser's
  name for the key and `--press A@60` is SDL's; both resolve, through
  `host.mjs`'s one scancode table and no second one. That is so a leg
  written down in [`docs/playable.md`](playable.md) can be typed here
  unchanged — parity in the machine is worth little without parity in the
  procedure.
- **`--steps N` is coarser here.** `af_machine_run_until` takes a tick and
  the ABI has no step-bounded run call, so a step budget is checked
  between frames and the run ends on the first frame boundary at or past
  N. The SDL host clamps the budget into the slice and ends on step N
  exactly. `--frames` and `--until` are exact to the machine's own step
  granularity, and the stop report always names the step it really ended
  on — so the comparison is still made on a number both hosts state.

A run prints the load line, the edition, every key it posted, the log as
it happens, and then a report block:

```
amberfolio: stop reason=tick_budget steps=298296 ticks=1193184 frames=61 ...
amberfolio: state hash=<64 hex characters>
amberfolio: audio underruns=0 resyncs=0
amberfolio: seams cheat-invulnerable unavailable wrong_binary - ...
amberfolio: throughput virtual=1.000s wall=0.008962s factor=111.59x steps=298296 steps/s=33286763
```

The first line is core's, character for character the desktop host's. The
`state hash` is the digest a recording's checkpoint carries
([`docs/replay.md`](replay.md) §2), so two hosts' runs compare on one line
rather than by eye over a picture. The seam table is the SDL host's
`--seams` shape.

**The throughput line is the one thing here that is measured rather than
reported.** #116 was closed on the strength of an out-of-tree number and
there was no instrument in the tree that produced one; this is it. Wall
time appears in this program and nowhere near `core/`
(`scripts/check-host-time.sh`), and the loop is unpaced — no
`requestAnimationFrame`, no sleep — so the factor is a *ceiling*: how
much faster than real time this module can run this program on this
machine, not what a browser will do. Wall seconds are printed to the
microsecond because a short run of a small program is single-digit
milliseconds and three decimals would round the divisor away.

Everything the driver prints goes to stdout, including the lines the SDL
host writes to stderr. To diff two runs:

```sh
amberfolio <dir> P.EXE --headless --until 40000000 2>desktop.txt
node .../drive.mjs <dir> P.EXE --until 40000000 >web.txt
diff <(grep '^amberfolio: stop' desktop.txt) <(grep '^amberfolio: stop' web.txt)
```

**It replays a recording too**, since the M5 closeout audit. It did not,
and the paragraph here used to say that was a decision (#147): the wasm
module verifies recordings through `af_machine_verify_recording` and
`hosts/web/tests/smoke.mjs` asks it that on every CI run, so a `--replay`
would change which process asked, not whether it was asked. What changed
the answer is that a **game** session cannot be asked by CI at all — its
disk is a player's — so the only place the question can be put to the
wasm module about a real run is a command somebody types.
`tests/sessions/README.md` is where the answers are kept, and they are now
23 of 23.

**It reads nothing but the directory it is given.** No game content is in
it and none may ever be — not bytes, not names, not screen text
(CONTRIBUTING.md). What CI runs it against is the repository's own
`tests/sessions/spin/`: `hosts/web/tests/smoke.mjs` spawns it as a
process and asserts the load line, the stop report, the seam table, the
audio counters, the state hash (twice, and they must agree), the
throughput line's arithmetic, the PPM it wrote, and that a seam keyed to
another binary is refused with an exit code. What CI does **not** check —
here or on the desktop side, where there is no such case either — is a
seam being *accepted*: that needs a program a seam's addresses are facts
about, and no such program is in this repository.

### The audio path under load, and the underrun policy

`app.mjs` pulls, on the main thread, exactly the audio contained in the
virtual time each `requestAnimationFrame` callback advanced — a fixed
frame's worth until #157, which on a 240 Hz display was four times more
audio than the machine had made — and posts each chunk to the
AudioWorklet, which plays them back in order. Two things can go wrong
with that and they are counted separately, both now shown on the page
beside the frame and step counts:

- **`underruns` / `resyncs`** are core's (`af_machine_audio_underruns`,
  `af_machine_audio_resyncs`): a pull that ran past settled virtual time,
  and a pull that had to jump forward to bound latency.
- **`starved`** is the worklet's: a render quantum the audio thread had no
  chunk for, counted once per run of starvation rather than once per
  sample.

Neither is machine state (`platform.h`), and nothing about them
back-pressures into the machine — a run that sounded wrong is still the
same run.

**The two hosts used to disagree about what an underrun sounds like, and
#108 settles it.** `audio_timeline::render()` holds the last level
(`platform_test.cpp`'s `AnUnderrunHoldsTheLevelAndKeepsItsPlace`); the
worklet filled silence. The reconciliation is not to copy core's line but
to follow its reasoning:

- Core holds because its underrun is **not a gap in the waveform**. The
  cursor does not advance, so the audio for that span is not lost but not
  yet made; the level held is the level the cone is genuinely at, the next
  pull resumes on the same tick, and the wave continues. It lasts as long
  as one pull runs past the horizon — microseconds.
- The worklet's underrun is a different event with the same name: the main
  thread did not post in time, and how long that lasts is a question about
  the browser's scheduler. A backgrounded tab is seconds, and the backlog
  cap means a long stall does not even replay what it missed. A held
  non-zero level for that long is not a continuation of anything — it is a
  DC offset: silent in itself, but a deflected cone, a bias in whatever
  the destination mixes it with, and a step at both ends of the gap.

So the worklet now **holds across the seam and then fades to silence** —
the last real sample for three milliseconds, ramped to zero over six
more, silence thereafter. Core's rule for as long as core's reasoning
holds, and an honest nothing once it stops. `smoke.mjs` drives the
processor directly (stubbing the three globals `AudioWorkletGlobalScope`
provides) and asserts the hold, the monotonic fade, the silence, and that
one stall is counted once.

### The comparison M3's exit criterion rests on

PLAN.md §7 asks for first light "verified locally on desktop **and** web",
and #84 states the test: a player's directory boots in the browser to the
same stop line the desktop host reports, **at the same step**. Both halves
of that are one command each, and the two outputs are compared by eye:

```sh
./build/<preset>/hosts/sdl/<config>/amberfolio <dir> <PROGRAM.EXE> --headless
```

against the page's console after **boot**. The `amberfolio: stop ...`
lines should be identical, field for field. Since #108 the web half of
that comparison need not be a person clicking: `tools/drive.mjs` above
does it from a command line, and adds a state hash to compare on as well
as a stop line.

If `frames=` disagrees and nothing else does, the two machines were not
powered on the same way rather than not run the same way: `reset()` blanks
and republishes the frame, which advances the generation counter, so a
machine that was reset and one that was not stay one frame apart forever.
Both hosts pull the line — `wired_machine`'s constructor on the desktop
side, `ensureMachine()` in `app.mjs` on this one — and that is why.

**No test in this repository ever runs the game**, here or on the desktop
side. What CI proves is the path: `smoke.mjs` puts a self-written program
into the filesystem through the same ABI the picker uses, loads it from
there, runs it, and reads the report. The comparison above is a procedure
a person carries out against their own copy — and it is now written down
in full, stage by stage and with the notices a healthy run prints, in
[`docs/first-light.md`](first-light.md) — and, for everything past the
roster, in [`docs/playable.md`](playable.md).
