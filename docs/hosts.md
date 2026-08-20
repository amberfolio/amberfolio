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

`ctest -L smoke` on any desktop preset runs six cases. Three are headless
and were there before; three are not.

| case | what it settles |
| --- | --- |
| `sdl-host-usage` | the binary starts, links and can talk |
| `sdl-host-smoke-disk` | the smoke disk's program is written |
| `sdl-host-runs-a-program` | headless: loader, CPU, DOS, console, exit code |
| `sdl-host-demo-disk` | the demo disk's two programs are written |
| `sdl-host-presents-a-frame` | windowed: the picture that reached the render target, and a keystroke that reached the program |
| `sdl-host-sounds-a-tone` | windowed: what reached the audio device was a tone |

The last two run the host **without** `--headless`, under SDL's
`offscreen` video driver and `dummy` audio driver. Those are not stubs of
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
  ever presented or the picture did not match.
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

If a build uses a system SDL3 compiled without the `offscreen` or `dummy`
drivers, the two windowed cases fail at `SDL_Init` and say so.

---

## 2. What a person still has to check

Two things, and no runner anywhere can check either: **that a photon left
a display, and that a pressure wave left a speaker.** Everything up to the
device is now asserted; the device itself is not.

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
the speaker's whole timeline as a WAV. If the PPM is right and the window
is not, the fault is in this half; if the PPM is wrong too, it is not, and
`ctest -L unit` will have a good deal more to say about which pixel probe
stopped agreeing.

### Recording what you found

The last inch is the part that stays a person's word. When you have run it
on a target, say so where the next person will look — the issue, or a line
in the commit that touched the host. "Ran `DEMO.EXE` on macOS 14, arm64:
bars correct, tone audible, keys echo" is worth more than any number of
green runners, because it is the only claim any of them cannot make.

---

## 3. The wasm host

`ctest --preset wasm` runs the module under node, headless, and asserts the
same program's console output and the ABI's export list. The browser half
— canvas, AudioWorklet, keyboard — has the same shape of gap the desktop
host had, and `scripts/serve-web.py` plus a browser is how a person closes
it. The dev page embeds a demo program with a continuous tone and an echo
loop for exactly the reason `DEMO.EXE` has both.
