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

### Curse of the Azure Bonds

The Steam copy (v1.3) runs from `C:\CURSE` with its own `CURSE.CFG`,
which is where its edition row (`curse-steam`) puts it without
`--install`. The log names it `Curse of the Azure Bonds (START.EXE)` and
says `save directory \CURSE\SAVE`, read off the file's third line. The
GOG copy runs the same program from the root (`curse-gog`): `save
directory \SAVE`, and `CURSE.CFG sound P, started as T (Tandy sound)`.
No seam is offered for either yet, and every figure below is with every
seam off, on the Steam copy. Virtual time,
read off `--dump-every 30` stills (to the half second):

| `--speed` | loading line | publisher's logo | title | second title | credits | play or demo |
|---|---|---|---|---|---|---|
| `xt` | 0:04 | 0:38.5 | 1:17 | 1:40.5 | 1:52 | 2:02 |
| `turbo` | 0:03 | 0:20 | 0:41.5 | 0:58 | 1:09 | 1:19 |
| `at` | 0:02.5 | 0:11 | 0:24.5 | 0:37.5 | 0:48 | 0:58 |
| `386` | 0:01.5 | 0:03.5 | 0:10 | 0:20.5 | 0:30.5 | 0:40.5 |

From the title on, each screen holds for about ten seconds of the
program's own timing; `--speed` shortens the computation before and
between them. The last screen waits for a key, and after thirty seconds
without one the program starts its demo. A key that chooses to play
brings up the copy-protection challenge within a second; the Steam copy
takes any letter and `Return`, and the party menu follows. At `386`:
`--press P@2500 --press A@2650 --press Return@2700` reaches the party
menu by frame 2730, on the desktop and under `hosts/web/tools/drive.mjs`
alike, at the same step and with the same frame.

The program clears an 80x25 text screen through `INT 10h AH=06h` before
it sets mode 0Dh, and finds every file before it opens it
(`INT 21h AH=4Eh`, a `file find` line naming the directory). Its log
carries one notice Pool of Radiance's does not: a write to ports 00h and
01h, the DMA controller's channel 0, which this machine does not have.
`CURSE.CFG` says `T`, so the Tandy chip is written throughout the boot
(about 82,000 writes in the first 45 seconds at `386`); it is heard once,
as the second title screen comes up. The speaker is silent.

### Secret of the Silver Blades

The Steam copy (version 1.30, of May 18, 1990) runs from `C:\SECRET`
with its own `BLADES.CFG`, which is where its edition row
(`secret-steam`) puts it without `--install`. The log names it `Secret
of the Silver Blades (START.EXE)` and says `save directory
\SECRET\SAVE`, read off the file's fourth line. No seam is offered for
it, and every figure below is with every seam off. Virtual time, read
off `--dump-every 30` stills (to the half second), to where each screen
first appears:

| `--speed` | loading line | publisher's logo | a picture | series title | title | credits | play or demo | the demo |
|---|---|---|---|---|---|---|---|---|
| `xt` | 0:03.5 | 0:39.5 | 1:19 | 2:21 | 3:34 | 4:45.5 | 6:07 | 6:37 |
| `turbo` | 0:03 | 0:21 | 0:48 | 1:44 | 2:45.5 | 3:51 | 5:11.5 | 5:42 |
| `at` | 0:02.5 | 0:11.5 | 0:32.5 | 1:25.5 | 2:21 | 3:24 | 4:44.5 | 5:14.5 |
| `386` | 0:01.5 | 0:03.5 | 0:19.5 | 1:10 | 2:02 | 3:02 | 4:22 | 4:52 |

Each screen from the picture on holds for the program's own timing,
fifty seconds to a minute for the picture and the titles and eighty
seconds for the credits, so `--speed` shortens only the computation
before and between them. The
last screen offers `PLAY` or `DEMO` and starts the demo by itself after
thirty seconds. **A key during the titles goes straight to that
screen**, and `Return` there takes `PLAY`, so at `386`
`--press Return@600 --press Return@900` has the play-or-demo screen at
0:10.5 and the party menu at 0:15.5; a third `Return` at frame 1500
takes the menu's first row into character creation. Nothing asks a
copy-protection question on the way.

**The menus are lists.** A row is chosen with `Up` and `Down` under a
highlight and taken with `Return`; a letter does not pick a row. The
prompts on the bottom line still take a letter (a slot, `Y` or `N`).

The boot log carries what the first two games' do not:

- **`notice absent_driver_called at 00033`**, once. `BLADES.CFG` asks
  for the mouse on its third line, as Steam ships it, and the program
  resets the mouse driver through `INT 33h`. This machine has none, and
  the call comes back as it went (`docs/machine.md` §5). The game is
  played from the keyboard, and the party menu's own
  `INITIALIZE MOUSE/JOYSTICK` finds neither.
- `INT 1Ah AH=00h`, the BIOS tick count, which the sound driver paces
  itself by.
- **`adlib writes=`** at the end of the run. The sound driver looks for
  an AdLib card as it starts, finds the one this machine has
  (`docs/hosts.md` §4b), and looks no further.

`BLADES.CFG` says `T`. **The titles' tune is on the AdLib card all the
same** (about 3,600 register writes in the first two minutes at `386`,
no speaker edge, and four writes to the Tandy chip, which silence it):
the program plays its title music through the card whenever it finds
one, and through the PC speaker when it does not. From the party menu
on the Tandy chip is written about 950 times a second with every voice
at full attenuation, which is silence: the party menu has no music.
The effects in play are on the Tandy chip.

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

Sound: Pool of Radiance makes none on this path, so a `--dump` is silent; the
speaker path is asserted by the `sound` machine program and
`sdl-host-sounds-a-tone`. Anything past the roster is `docs/playable.md`.
Timing is a fixed cost per step (`machine/clock.h`).
