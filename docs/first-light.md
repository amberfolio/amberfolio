# First light

How to watch a player-supplied copy boot on this emulator, on the desktop
host and in a browser, and what the two runs have to agree about.

This is a **procedure**, not a fixture. Nothing in this repository can run
it: no copy of any game is here, none ever will be, and CI has no way to
obtain one (PLAN.md §6). What CI runs instead is `synthetic_boot`
(`tests/programs/machine_programs.cpp`) — a self-written program shaped
like a boot, which exercises every service the real one turned out to
need. That program is the public proof; this document is the private one,
written down so it can be repeated rather than remembered.

M3's exit criterion (PLAN.md §7) is **title → party roster reachable,
verified locally on desktop and web**. Everything below is how to check
that claim yourself.

---

## What you need

A directory holding your own copy of the game, with the program you want
to run in it. Everything the program opens has to be in that one
directory, because the machine has one drive and no subdirectories are
created for you (`machine/vfs.h`).

Two notes about what the emulator does with it:

- **The directory is never written to by these instructions.** Every run
  below is read-only as far as the boot is concerned.
- **Files whose names no DOS short name can equal are skipped**, and the
  hosts say how many. A real game directory usually has one or two — a
  PDF, a readme with a long name. That is the correct answer, not a
  failure: DOS could not have named them either.

---

## Desktop

```sh
cmake --preset windows-msvc          # or linux-gcc, linux-clang, macos
cmake --build --preset windows-msvc --config Release
./build/windows-msvc/hosts/sdl/Release/amberfolio <your-directory> <PROGRAM.EXE>
```

A window opens and the boot begins. Nothing is silent about what it is
doing — the host prints the file's identity before anything runs, and
every refusal after that (`docs/machine.md` §5).

### What the run should look like

Five stages, in this order. Timings are in *virtual* time, which the
windowed host paces against the wall, so at the default speed they are
also how long you will wait:

| about | what is on screen |
|---|---|
| 0:02 | a loading line, drawn by the program itself |
| 0:57 | **nothing at all** — the line clears and the screen goes black |
| 1:13 | the publisher's logo, painting in visibly |
| 1:20 | the title screen |
| 2:05 | the copy-protection challenge, waiting for input |

**The black stretch around a minute in is the one that looks broken and
is not.** Nor is the logo painting itself in line by line: that is what a
4.77 MHz machine looked like doing it.

Almost all of that is computation rather than waiting, which is worth
knowing before reaching for a stopwatch. Running the same boot at each
speed preset and solving for the two parts gives **about 104 seconds of
work and about 21 seconds of timed pause** — the pauses are the
program's own, timed against the BIOS tick, and they do not shrink when
the processor gets faster:

| `--speed` | ticks per step | reaches the challenge at |
|---|---|---|
| `xt` (default) | 4 | 2:05 |
| `turbo` | 2 | 1:13 |
| `at` | 1 | 0:47 |

If the emulator feels slower than another one you have used, this is
why, and it is not a defect: DOSBox's default of 3000 cycles per
millisecond is an order of magnitude faster than the machine this game
was written for. Which of these presets is *right* is a playtest
question and an open one (#107).

**To just get to the game, use `--fast` instead.** It is the other knob
and it is not the same one: `--speed` changes which machine this is, and
divides only the 104 seconds of computation; `--fast` changes how fast
you watch it, and divides both numbers.

| | reaches the challenge in |
|---|---|
| `--speed at` (four times the CPU) | 0:47 |
| `--fast 20` | **0:04** |
| `--fast max` | 0:04 |

Nothing inside the machine can tell the difference. A run at `--fast 20`
produces the same step count, the same tick count, the same frame count
and a byte-identical framebuffer as one at `--fast 1`; the only thing
that changes is how long the host sleeps at the bottom of its loop. That
is what `platform.h`'s rule about wall time never reaching machine state
buys you.

`max` is barely faster than `20` here, because composing and presenting
eight thousand frames becomes the floor once the sleep is gone. And
audio is spoiled by any of this, unavoidably: the speaker is pulled by a
real 48 kHz device that cannot be hurried.

**Answer the challenge from your own wheel.** That is the whole of it:
the game asks, you answer, and the roster menu appears. If you would
rather not, `--seam code-wheel` answers it for you — see "The seam"
below for what that is and what it is not.

Past the challenge, the roster menu appears and takes a keystroke; the
character-creation list appears and takes another. **That is the exit
criterion met.**

### Reading the boot log

A healthy run on a copy of this vintage prints a load line, three
notices, and no stop:

```
amberfolio: load PROGRAM.EXE sha256=<64 hex characters>
amberfolio: load psp=0050 image=0060 entry=0FD2:0012 stack=117A:0080 tail=0
amberfolio: notice undisplayable_video_mode at 00003 value=03 from=F000:060F
amberfolio: notice unmapped_memory_read at B8000 value=00 from=F000:060F
amberfolio: notice unclaimed_port_write at 000C0 value=9F from=090E:13DA
```

All three notices are true statements about this machine, and none of
them is a fault:

- **`undisplayable_video_mode 03`** — the program passes through 80x25
  text on its way to graphics. This machine has no text path, so it
  records the mode, reports it back through `INT 10h AH=0Fh`, programs
  nothing, and says so once (`machine/int10.h`).
- **`unmapped_memory_read at B8000`** — and then it asks for the
  character under the cursor. The BIOS reads the text page through the
  bus, as a real one does; nothing in this machine answers for B8000, so
  the read floats high. FFFFh is what the hardware returns, not an
  invented answer.
- **`unclaimed_port_write at 000C0`** — the program silences a Tandy
  three-voice sound chip on its way past. There is none here, the write
  goes nowhere, and that is the end of it. PLAN.md §3 scopes sound to
  the PC speaker.

**A fourth line means something new.** Any `stop reason=` is the
machine refusing to invent an answer, and the `next=` line beneath it
names exactly what to widen — that is the whole M3 method, and
`docs/machine.md` §5 describes the shape.

### Useful flags

```
--headless              no window, no audio device; runs flat out
--speed xt|turbo|at     which machine to be (see the table above)
--fast N|max            run virtual time N times faster than the wall
--scale N               window size; the frame is 320x200, so 3 gives 960x600
--until TICKS           stop at a moment in virtual time
--steps N               stop after N scheduling steps
--dump PREFIX           write PREFIX.ppm and PREFIX.wav at the end
--trace                 keep the trace ring and print it with the report
--seam code-wheel       see below
```

`--headless --until` is how to get a comparable run: it removes the wall
clock from the picture entirely, so the same tick budget always produces
the same step count.

---

## Web

The wasm host has no window of its own to hand you a directory through,
so the dev page has a picker (#84). Build and serve it:

```sh
cmake --preset wasm                  # needs the emsdk in .emscripten-version
cmake --build --preset wasm-release
# serve build/wasm/hosts/web/Release over http and open index.html
```

Pick your directory, pick the program, and run. The page reports how many
files it took and how many it skipped, the same fingerprint the desktop
host prints, and the same stop report — formatted in core precisely so
the two cannot drift (`machine/report.h`).

Answer the challenge the same way. The roster menu appears, and takes a
keystroke.

> **Build Release, not Debug.** The default `wasm` preset is a Debug
> build and is roughly nine times slower — still fast enough, but it
> spends most of the headroom. See #116.

---

## What the two hosts must agree about

This is the part worth doing carefully, because it is the claim that the
core is one machine and the hosts are only windows onto it.

Run both to the *same tick budget* — not the same wall time, and not the
same number of frames presented — and compare:

1. **The fingerprint.** Same file, same SHA-256, computed by the same
   code in core.
2. **The step count.** Identical. Not close: identical.
3. **The stop report**, line for line.
4. **The frame**, pixel for pixel. `--dump` gives you a PPM on the
   desktop side; the dev page can hand you the framebuffer.

A divergence in any of those is a real bug and worth stopping for. The
usual suspect is a host that let wall time reach machine state, which
platform.h's design essay exists to prevent.

---

## The seam

`--seam code-wheel` answers the copy-protection challenge, so a
maintainer can get to the roster without reaching for the wheel every
time.

It is **off unless you name it**, it is refused unless the program you
loaded is the exact binary its addresses are facts about, and the host
prints a line saying it is on — because a run with a seam on is not the
same run as one without it (`machine/seam.h`, PLAN.md §5).

It is also **not finished**. PLAN.md §5 gates this enhancement on a
fingerprint-verified code wheel PDF — a possession gate, demonstrating
that the player holds the document — and that gate is M5's work (#115).
Until it lands, this flag is a switch for the person who wrote it, used
on their own copy.

---

## What first light does *not* cover

Worth writing down, so the milestone is not credited with more than it
proved:

- **Sound.** The program makes none on this path. It clears the
  speaker's gate and data bits during startup, silences the Tandy chip,
  and never enables either again; a `--dump` of the run is silent, and
  correctly so. The speaker path itself is asserted elsewhere and end to
  end — M2's `sound` machine program measures two tone periods back out
  of the pulled audio as exact virtual-time equalities, and
  `sdl-host-sounds-a-tone` runs the real SDL audio callback on every
  desktop target — but the *game* driving it is M4's playtests.
- **Anything past the roster.** Exploration, combat, shops and saving are
  M4. The menus respond; that is the criterion, and it is all of it.
- **Timing fidelity.** The machine runs at a fixed cost per step
  (`machine/clock.h`), and whether that *feels* right is a playtest
  question M4 answers with real presets.
