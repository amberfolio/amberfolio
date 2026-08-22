# Playable

How to drive a player-supplied copy from the party roster into the game —
a party, the city, a story event, a fight, a save and a load — and what
each of those legs is evidence for.

`docs/first-light.md` is the M3 procedure and stops where this one starts:
at the roster, with an empty party and nothing to do. M4's exit criterion
(PLAN.md §7) is the rest of the loop, and this is how it gets checked.

The same rule as first light applies and always will: **nothing in this
repository can run any of it.** No copy of any game is here, none ever
will be, and CI has no way to obtain one (PLAN.md §6). What CI runs is
`synthetic_boot` and its siblings in `tests/programs`. This document is
the private half, written down so it can be repeated rather than
remembered.

---

## The method

The whole procedure is one command with a list of keystrokes on it, run
under SDL's `dummy` drivers so it needs no display and no speaker:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/<preset>/hosts/sdl/<config>/amberfolio <your-directory> START.EXE \
    --seam code-wheel --fast max \
    --press KEY@FRAME ... \
    --until <ticks> --dump run --dump-every 90
```

Three flags carry it:

- **`--press KEY@FRAME`** posts a real SDL key event at a frame. Frames
  are 60 to the virtual second, so `FRAME = seconds * 60`; `--until` is
  in PIT ticks, so `TICKS = seconds * 1193182`.
- **`--fast max`** runs virtual time unpaced. Nothing inside the machine
  can tell (`docs/hosts.md` §2): a leg that takes seven virtual minutes
  takes about ten seconds of yours.
- **`--dump-every 90`** writes a still every ninety frames. That is what
  makes the run readable afterwards — the interesting screens are the
  ones that *changed*, and hashing the stills finds them:

  ```sh
  sha256sum run-*.ppm | sort -k2 | uniq -f0 -w12 --group
  ```

Each run replays from the start and mutates the game directory (a
character created is a file, a save is more), so **snapshot the save
directory before you begin and restore it between runs**, or the second
run answers a question you did not ask. Two runs of the same script over
the same starting directory are identical to the step; two runs whose
keystrokes differ by a frame are not, because the game's own randomness
is driven from the clock. That is not a defect and it is the reason
`docs/replay.md`'s recordings exist: a *particular* fight is reproducible
only as a recording of one.

---

## Leg 0 — a party

From the code-wheel challenge to a character standing in the roster.
Answering the wheel needs the seam (`docs/seams.md`); the letter *and* the
Return are both required, because the seam fires inside the program's own
string compare and an empty answer never reaches it.

```
--press A@7600 --press Return@7650                      the code wheel
--press C@8950                                          create a character
--press Return@9100 --press Return@9250 --press Return@9400
--press Return@9550 --press Return@9700                 race, gender, class,
                                                        alignment, the roll
--press Y@9900                                          keep it
--press B@10100 --press O@10130 --press B@10160
--press Return@10250                                    name it
--press K@11500                                         keep the portrait
--press E@12200 --press Y@12950                         leave the icon editor
--press A@13300 --press A@13550 --press E@13800         add it to the party
--press B@14050                                         begin adventuring
```

What it is evidence for: the menus answer, the character generator runs,
and **the write path works** — `\SAVE\BOB.CHA`, `\SAVE\BOB.SPC` and the
roster list appear in the directory. `--trace` shows them by name:

```
amberfolio: file mkdir \SAVE handle=0000 access_denied from=0B58:1823
amberfolio: file create \SAVE\CHARLIST.TXT handle=0006 none from=0B58:063B
```

The `access_denied` is the program making its save directory on every
visit and ignoring the answer, which is correct on a directory that
already exists.

One trap worth knowing, because it costs an afternoon: **adding a
character to the party removes it from the roster list**, so a second run
over the same directory finds no character to add and quietly returns to
the four-item menu. Restore the snapshot.

## Leg 1 — the city, and the story (#102)

`BEGIN ADVENTURING` opens on the 3D view with the party panel beside it,
and the game's opening story event: a guide introduces himself and walks
the party through Phlan, a screen and a paragraph at a time. Return
advances each one.

```
--press Return@15500 --press Return@15590 ... --press Return@20000
```

(ninety frames apart; `Return` every 1.5 virtual seconds is comfortably
faster than the text.)

The tour ends at `0,4 W` with the exploration command bar up:

```
AREA CAST VIEW ENCAMP SEARCH LOOK
```

What it is evidence for: the 3D view renders walls, doorways, buildings
and interiors; the coordinate and heading readout tracks; text events
render and take their prompts; and `AREA` draws the district map. Arrow
keys move — forward and turn — and walking into a building's entrance
fires its own event (`YOU ARE OUTSIDE THE CITY HALL...`).

## Leg 2 — a fight (#103)

Walking north out of the tour's end reaches the slums, and the slums
produce a random encounter within a couple of dozen steps:

```
--press Up@20250 --press Up@20340 ... --press Up@22050    walk north
--press C@23000                                           COMBAT
--press Q@23800                                           QUICK fight
```

`C` at the `COMBAT WAIT FLEE PARLAY` prompt opens the tactical map; `Q`
hands the round to the computer and the fight resolves on its own.

What it is evidence for: the tactical view's stone, floor, sprites and
side panel; animation pacing; the keyboard read in a tight loop; and the
speaker, which is the first thing in the whole run to make a sound —
`--dump`'s WAV holds a short burst at each hit.

A first-level character does not survive this, which is what the debug
cheats seam is for (#99, `docs/seams.md`):

```
--seam cheat-invulnerable --seam cheat-kill-all
```

Both work, and both had to be corrected first — neither had ever been run
against the program (#99, #103). With them on, the same encounter ends
`THE PARTY HAS WON. EACH CHARACTER RECEIVES 15 EXPERIENCE POINTS.` and
the party is on its full hit points; with them off it ends on one hit
point and a `CONTINUE BATTLE:` prompt.

## Leg 3 — a save, and a load (#105)

From the exploration bar, without walking anywhere:

```
--press E@20400        ENCAMP
--press S@21100        SAVE
--press A@22100        slot A
```

and then, in a fresh run over the directory that leg left behind:

```
--press A@7600 --press Return@7650    the code wheel
--press L@8950                        LOAD SAVED GAME
--press A@9200                        slot A
```

What it is evidence for: the camp screen, the slot menu, and the whole
save write path in five lines of `--trace`:

```
amberfolio: file mkdir \SAVE handle=0000 access_denied
amberfolio: file create \SAVE\SAVGAMA.DAT handle=0006 none
amberfolio: file close \SAVE\SAVGAMA.DAT handle=0006 none
amberfolio: file open \SAVE\BOB.CHA handle=0006 none
amberfolio: file unlink \SAVE\BOB.CHA handle=0000 none
```

The save moves the party's character files into the slot, which is why
the roster list is empty afterwards and why `LOAD SAVED GAME` is the way
back rather than `ADD CHARACTER TO PARTY`. The load returns the party to
`0,4 W` with the same character, the same AC and the same hit points.

Files land **only where the program put them**: inside the directory you
gave the host, under the save path the program's own configuration names
(`C:\SAVE\` in a standard install). The machine creates no directory of
its own accord. On desktop, that directory *is* the save location, as it
was on the original machine, and backing it up is yours to do until M5's
save management.

---

## What the run should not say

Three notices, and no others, on a clean run of any of the legs above:

```
amberfolio: notice undisplayable_video_mode at 00003 value=03
amberfolio: notice unmapped_memory_read at B8000 value=00
amberfolio: notice unclaimed_port_write at 000C0 value=9F
```

All three are M3's and are correct answers (`docs/first-light.md`): the
80x25 text mode this machine records and cannot draw, the text page
nothing answers for, and the sound chip this machine is not fitted with.

A fourth appears once combat runs:

```
amberfolio: notice rom_write at F0147 value=00 from=011A:230E
```

The program writes a zero into the BIOS ROM at `F000:0147`. A real PC has
ROM there and swallows it; so does this machine, and the notice is the
record that it did. Nothing depends on it.

Anything else — a `stop`, a `device declined`, a `cpu stopped` — is a
worklist line, and `docs/machine.md` §5 is what to do with it.

## What this procedure has *not* covered

Honest gaps, so nobody reads more into a green run than is there:

- **The city services** — shops, training hall, temple, inn (#104). The
  city hall's entrance event fires and its interior renders; buying,
  selling, training and resting have not been driven.
- **The dungeon.** Everything above is the city and its slums.
- **The web host** (#108) runs the same core and the same recordings, and
  none of these legs has been driven on the dev page.
- **Audio beyond "there was one."** The first sound this program makes is
  in combat, and `docs/hosts.md` §3 is still where a person checks that a
  pressure wave left a speaker (#106).
