# First light

How to boot a player-supplied copy on the desktop host and in a browser,
and what the two runs must agree about. Nothing in this repository can
run it (PLAN.md §6); CI runs `synthetic_boot`
(`tests/programs/machine_programs.cpp`) instead. `docs/playable.md`
continues from the roster.

---

## What you need

A directory holding your own copy, **configured by its own installer or
launcher**, which writes a configuration file beside the executable.
Without it the program asks for a floppy in 80x25 text, which this
machine does not display, and the run looks like a hang at a black
screen two seconds in.

- The boot never writes to the directory. Past the roster the game
  does: a character is a file, and it makes its save directory
  (`INT 21h AH=39h`, `machine/dos.h`) if it is not there.
- One drive, C:, and no directory the program did not make
  (`machine/vfs.h`).
- Files with no DOS short name are skipped, and the hosts say how many.
  A PDF or a long-named readme in the directory is normal.

---

## Desktop

```sh
cmake --preset windows-msvc          # or linux-gcc, linux-clang, macos
cmake --build --preset windows-msvc --config Release
./build/windows-msvc/hosts/sdl/Release/amberfolio <your-directory> <PROGRAM.EXE>
```

The host prints the file's identity before anything runs, and every
refusal after that (`docs/machine.md` §5).

### What the run should look like

Virtual time, which the windowed host paces against the wall:

| about | what is on screen |
|---|---|
| 0:02 | a loading line, drawn by the program |
| 0:57 | nothing: the line clears and the screen goes black |
| 1:13 | the publisher's logo, painting in line by line |
| 1:20 | the title screen |
| 2:05 | the copy-protection challenge, waiting for input |

The black stretch and the slow logo are correct. The boot is about 104
seconds of computation and 21 seconds of the program's own timed pauses,
so `--speed` divides only the first:

| `--speed` | the machine | reaches the challenge at |
|---|---|---|
| `xt` (default) | 4.77 MHz 8088, ~298k steps/s | 2:05 |
| `turbo` | 8-10 MHz XT clone, ~597k | 1:12 |
| `at` | ~1.19M | 0:47 |
| `386` | 33 MHz 386DX, ~5.99M | 0:25 |

`--fast` divides both, by changing only how long the host sleeps:
`--fast 20` reaches the challenge in 0:04 where `--speed 386` takes
0:25, with the same step count, tick count, frame count and framebuffer
as `--fast 1`. `--fast max` is barely faster than `20` because presenting
frames becomes the floor. Audio is spoiled by either; the 48 kHz device
cannot be hurried. Which preset is right is a playtest question (#107).

Answer the challenge from your own wheel, or skip it with
`--code-wheel-answered` (see "The seam"). Past it, the roster menu takes
a keystroke and the character-creation list takes another. That is the
exit criterion.

### Reading the boot log

A healthy run prints a load line, three notices, and no stop:

```
amberfolio: load PROGRAM.EXE sha256=<64 hex characters>
amberfolio: load psp=0050 image=0060 entry=0FD2:0012 stack=117A:0080 tail=0
amberfolio: notice undisplayable_video_mode at 00003 value=03 from=F000:060F
amberfolio: notice unmapped_memory_read at B8000 value=00 from=F000:060F
amberfolio: notice unclaimed_port_write at 000C0 value=9F from=090E:13DA
```

| notice | meaning |
|---|---|
| `undisplayable_video_mode 03` | the program passes through 80x25 text; the machine records the mode, reports it back through `INT 10h AH=0Fh`, programs nothing (`machine/int10.h`) |
| `unmapped_memory_read at B8000` | the BIOS reads the character under the cursor through the bus; nothing answers for B8000 and the read floats high, as hardware does |
| `unclaimed_port_write at 000C0` | the program silences a Tandy sound chip; there is none (PLAN.md §3 scopes sound to the speaker) |

Any `stop reason=` line is the machine refusing to invent an answer; the
`next=` line beneath it names what to widen (`docs/machine.md` §5).

### Useful flags

```
--headless              no window, no audio device; runs flat out
--speed xt|turbo|at|386 which machine to be (see the table above)
--fast N|max            run virtual time N times faster than the wall
--scale N               window size; the frame is 320x200, so 3 gives 960x600
--until TICKS           stop at a moment in virtual time
--steps N               stop after N scheduling steps
--dump PREFIX           write PREFIX.ppm and PREFIX.wav at the end
--trace                 keep the trace ring and print it with the report
--seam code-wheel       see below
--code-wheel-answered   skip the challenge on this boot
```

`--headless --until` is the comparable run: the same tick budget always
produces the same step count.

---

## Web

The dev page has a directory picker (#84). Build and serve:

```sh
cmake --preset wasm                  # needs the emsdk in .emscripten-version
cmake --build --preset wasm-release
# serve build/wasm/hosts/web/Release over http and open index.html
```

The page reports the files taken and skipped, the same fingerprint the
desktop prints, and the same stop report, formatted in core
(`machine/report.h`).

**Build Release, not Debug.** The default `wasm` preset is a Debug build
and about nine times slower (#116).

---

## What the two hosts must agree about

Run both to the same tick budget, not the same wall time or frame count,
and compare:

1. The fingerprint: same SHA-256, computed in core.
2. The step count: identical.
3. The stop report, line for line.
4. The frame, pixel for pixel: `--dump` on the desktop, the framebuffer
   from the dev page.

A divergence is a bug. The usual cause is a host letting wall time reach
machine state (`platform.h`).

---

## The seam

`--seam code-wheel` is off unless named, refused unless the loaded binary
matches its fingerprint, and announced on the host's log (`machine/seam.h`,
PLAN.md §5). On and unanswered it only watches; a correct answer typed
once is latched (#290, #291) and kept between runs by the host (#292,
`code-wheel.txt`, `--forget-code-wheel`). `--code-wheel-answered` tells
the machine the answer is already known, so the boot steps over the
challenge. It never answers the challenge for anybody.

---

## What first light does *not* cover

Sound: the program makes none on this path, so a `--dump` is silent; the
speaker path is asserted by the `sound` machine program and
`sdl-host-sounds-a-tone`. Anything past the roster is `docs/playable.md`.
Timing is a fixed cost per step (`machine/clock.h`).
