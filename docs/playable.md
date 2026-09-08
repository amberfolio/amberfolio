# Playable

How to drive a player-supplied copy from the party roster into the game,
leg by leg: the keystrokes, what the run should print or show, and the
traps. `docs/first-light.md` stops at the roster. Nothing in this
repository can run any of it (PLAN.md §6); CI runs `synthetic_boot` and
its siblings in `tests/programs`.

---

## The method

One command, a list of keystrokes, SDL's `dummy` drivers:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/<preset>/hosts/sdl/<config>/amberfolio <your-directory> START.EXE \
    --seam code-wheel --code-wheel-answered --fast max \
    --press KEY@FRAME ... \
    --until <ticks> --dump run --dump-every 90
```

| flag | what |
| --- | --- |
| `--press KEY@FRAME` | a real SDL key event at a frame; 60 frames to the virtual second, so `FRAME = seconds * 60` |
| `--until TICKS` | stop at a PIT tick; `TICKS = seconds * 1193182` |
| `--fast max` | virtual time unpaced; nothing inside the machine can tell (`docs/hosts.md` §2) |
| `--dump run --dump-every 90` | a still every ninety frames, plus the WAV and the `.edges` list at the end |
| `--record tests/sessions/NAME.rec --record-every 128` | keep the leg as a session; `tests/sessions/README.md` has the descriptor and `--pin` |
| `--trace` | every DOS call, by name |
| `--watch OFF[:N]` | a data-segment word, printed when it changes (leg 5) |
| `--pull NAME@FRAME` | trigger a pulled seam at a frame |

Finding the stills that changed:

```sh
sha256sum run-*.ppm | sort -k2 | uniq -f0 -w12 --group
```

**Every script below is one boot out of date (#293).** They press two
keys at the challenge (`A@7600`, `Return@7650`) from when the code-wheel
seam answered it; since #291 it only watches and `--code-wheel-answered`
skips the challenge, so those presses come out and every later frame
number moves earlier. Leg 7's EXIT script is re-derived: with the flag,
`L@7550 C@7800` stands where `A@7600 Return@7650 L@8950 C@9200` did.

Rules:

- **Snapshot the game directory before you begin and restore it between
  runs.** Every run mutates it: a character created is a file, a save is
  more.
- Two runs of the same script over the same directory are identical to
  the step; two whose keystrokes differ by a frame are not, the game's
  randomness being driven from the clock. A particular fight is
  reproducible only as a recording (`docs/replay.md`).
- Menu keys may be 60 to 90 frames apart. **Movement keys want 150.** A
  press that lands while the party is still walking is flushed with the
  buffer; the symptom is a heading that never changes, or a turn that
  reads three squares later as a walk in the wrong direction.
- An entrance event stops the walk until answered, and every later
  keystroke is flushed. A sweep presses `Return` and `N` after every
  move; neither does anything at the exploration bar.
- Record anything worth repeating; `scripts/sweep.py` re-runs it on a
  machine that has the disk.

---

## Leg 0 — a party

Code-wheel challenge to a character in the roster. Recorded as
`tests/sessions/party.rec`.

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

Both the letter and the Return are required at the wheel: the seam fires
inside the program's own string compare. End state: `\SAVE\BOB.CHA`,
`\SAVE\BOB.SPC` and the roster list are in the directory. `--trace`:

```
amberfolio: file mkdir \SAVE handle=0000 access_denied from=0B58:1823
amberfolio: file create \SAVE\CHARLIST.TXT handle=0006 none from=0B58:063B
```

The `access_denied` is the program making its save directory on every
visit and ignoring the answer.

Trap: adding a character to the party removes it from the roster list,
so a second run over the same directory finds nothing to add and returns
to the four-item menu.

## Leg 1 — the city, and the story (#102)

`BEGIN ADVENTURING` opens on the 3D view and the guide's tour of Phlan;
Return advances each screen.

```
--press Return@15500 --press Return@15590 ... --press Return@20000
```

Ninety frames apart is faster than the text. The tour ends at `0,4 W`
with the exploration bar up:

```
AREA CAST VIEW ENCAMP SEARCH LOOK
```

Arrow keys move and turn, `AREA` draws the district map, and walking into
a building's entrance fires its event
(`YOU ARE OUTSIDE THE CITY HALL...`). Legs 0, 1 and 3 are one run in
`tests/sessions/save.rec`.

## Leg 2 — a fight (#103)

North out of the tour's end is the slums, and an encounter within a
couple of dozen steps. `tests/sessions/fight.rec` and `fight-cheat.rec`
are the pair.

```
--press Up@20250 --press Up@20340 ... --press Up@22050    walk north
--press C@23000                                           COMBAT
--press Q@23800                                           QUICK, one round
```

`C` at `COMBAT WAIT FLEE PARLAY` opens the tactical map. The speaker's
first sound in the run is here: `--dump`'s WAV holds a burst at each hit.

A first-level character does not survive it. The cheats
(`docs/seams.md` §10):

```
--seam cheat-invulnerable --seam cheat-kill-all --pull cheat-kill-all@600
```

- `cheat-invulnerable` is a setting: the party ends the encounter on
  eight hit points instead of one.
- `cheat-kill-all` is **pulled** (#161): Pause/Break on the desktop host,
  the `pull` button on the page, `--pull` in a script. It is served at
  the first step where its guard recognises the combat, with the
  end-of-round check as the fallback (#163). It does `debug_damage`, 120
  points, to every standing enemy; anything tougher survives.

The seam's end-of-run line says how the pull was served:

| pulled at | line |
| --- | --- |
| after `Q` starts the round | `fired=1 reached=1 waited=0` |
| before the round starts | `fired=1 reached=0 waited=8327644`: declined until the roster was recognised, then served |
| after the combat ended | `fired=0 reached=1 waiting` and `inert point_not_recognized`: pull kept |

One firing ends a fight:

```
THE PARTY HAS WON.
EACH CHARACTER RECEIVES 107 EXPERIENCE POINTS.
```

Measuring the damage and the side (#271), from slot J (`HULK`, `MULE`,
`THIEF`, on the wilderness):

```
--press L@8950 --press J@9200            LOAD SAVED GAME, slot J
--press Up@10650 ... every 150 frames    walk north across the wilderness,
--press Return@+50 --press N@+100          answering anything that asks
--press C@17500                          COMBAT, at the encounter prompt
--pull cheat-kill-all@18400              with the tactical map up
--watch 49F3 --watch 6814:2              the mode byte, and both body counts
```

`6814` and the byte after it count who is still standing on each side;
the low byte is the party, side 0. Seven soldiers at 9,2 N read
`6814=0703` before the pull and `6814=0003` at it. Nothing with 121 to
255 hit points has been met. Slot E's city area gave no encounter in
twenty-seven moves; `fight.rec`'s slums give a single orc.

## Leg 3 — a save, and a load (#105)

`tests/sessions/save.rec` and `load.rec`. From the exploration bar,
without walking:

```
--press E@20400        ENCAMP
--press S@21100        SAVE
--press A@22100        slot A
```

then a fresh run over the directory that left:

```
--press A@7600 --press Return@7650    the code wheel
--press L@8950                        LOAD SAVED GAME
--press A@9200                        slot A
```

`--trace` for the save:

```
amberfolio: file mkdir \SAVE handle=0000 access_denied
amberfolio: file create \SAVE\SAVGAMA.DAT handle=0006 none
amberfolio: file close \SAVE\SAVGAMA.DAT handle=0006 none
amberfolio: file open \SAVE\BOB.CHA handle=0006 none
amberfolio: file unlink \SAVE\BOB.CHA handle=0000 none
```

The save moves the character files into the slot, so the roster list is
empty afterwards and `LOAD SAVED GAME` is the way back, not
`ADD CHARACTER TO PARTY`. The load returns the party to `0,4 W` with the
same character, AC and hit points. Files land only under the save path
the program's configuration names (`C:\SAVE\` in a standard install),
inside the directory given to the host.

## Leg 4 — a shop, and a purchase (#104)

Starts from slot A, a whole party with money, standing at `4,12 S`; the
armourer is at `8,11`.

```
--press A@7600 --press Return@7650      the code wheel
--press L@8950 --press A@9200           LOAD SAVED GAME, slot A
--press Up@11000  --press Up@11150 --press Up@11300 --press Up@11450
--press Right@11600
--press Up@11750  --press Up@11900 --press Up@12050 --press Up@12200
--press Right@12350
--press Up@12500  --press Up@12650 --press Up@12800 --press Up@12950
--press Right@13100
--press Up@13250  --press Up@13400 --press Up@13550 --press Up@13700
--press Right@13850
--press Up@14000
--press Y@14400        yes: into the shop (BUY VIEW POOL APPRAISE EXIT)
--press B@15000        BUY: the stock list
--press B@15600        BUY again: the highlighted item, a HAND AXE
--press E@16600        EXIT the stock list
--press E@17000        EXIT the shop
--press V@17600        VIEW the first character
--press I@18200        ITEMS: what they are carrying
```

The square fires
`THE SHOP SPECIALIZES IN ARMS AND ARMOR. 'CAN I SHOW YOU OUR WARES?'`
with a portrait; the stock list (`HAND AXE 1`, `BARDICHE 7`, ...
`GLAIVE-GUISARME 10`) sits over an `ITEMS: BUY NEXT PREV EXIT` bar.

Check: run with and without `--press B@15600` and read the sheet.

| | without the buy press | with it |
| --- | --- | --- |
| `FIGHTER1`'s money | `PLATINUM 1586`, `GOLD 1` | `PLATINUM 1586`, no gold line |
| `FIGHTER1`'s pack | ends `POTION OF HEALING` | ends `POTION OF HEALING`, `HAND AXE` |

Traps:

- The buyer is the current character, not the party; `VIEW` then `ITEMS`
  on the paying character is the check.
- Do not read walkability off the `AREA` panel: an eleven-by-eleven
  window on the district (origin map `0,5` here, the party arrow pinning
  it) whose light-grey lines do not match the walls the party bumps into.

Finding a service: player material gives the civilised district's
services by square (inns around `4,12`, a shop at `11,12`, taverns at
`8,9`, a jewellers at `8,10` behind a shop at `9,10`, the hiring hall at
`7,2`); `--watch` (leg 5) is how to confirm one.

---

## Leg 5 — the temple, and a sale (#104)

`tests/sessions/temple.rec` is the cure.

### Watching where the party is

```
--watch 6AAD --watch 6AAE --watch 6AAF --watch 49F3
```

| offset | what |
| --- | --- |
| `6AAD` | the party's X on the area map |
| `6AAE` | its Y |
| `6AAF` | its facing: 0 N, 2 E, 4 S, 6 W |
| `49F3` | the screen mode: 0 menu, 1 a portrait sub-view, 2 camp, 3 and 4 adventure, 5 tactical combat, 6 a banner |
| `49FA` | with `49F3:1`, how the wilderness arrival is timed (leg 11) |
| `6814:2` | combat: who is still standing on each side, low byte the party (leg 2) |
| `6DDA` | non-zero while the rest screen is the screen (leg 7) |
| `6DCA` | the days field of the rest clock, what the Fix dials (leg 7) |
| `6B2B` | the menu bar's highlight: one-based group index, one byte shared by every bar (leg 7, #304) |
| `84E4` | "a script has the message area"; oscillates on every step (leg 8) |

```
amberfolio: watch frame=012814 ds=0CDC 6AAD=07 6AAE=05 6AAF=00 49F3=04
amberfolio: watch frame=013114 ds=0CDC 6AAD=08 6AAE=05 6AAF=02 49F3=04
```

Filter on `ds=0CDC`, the data segment on this edition. A wall reads as a
press that changed the facing and not the position, which is also what a
flushed keystroke looks like.

### The temple, the sale, and what else is on the map

The healing temple is at **3,1**, firing
`YOU ARE WELCOMED BY PRIESTESS JOY OF SUNE. 'DO YOU SEEK HEALING?'`. `Y`
opens `HEAL VIEW POOL APPRAISE EXIT`; `H` asks which character and lists
the cures, `CURE BLINDNESS` to `STONE TO FLESH`. Then:

```
FIGHTER1 IS NOT BLIND.  CAST CURE ANYWAY: YES NO
CURE BLINDNESS WILL ONLY COST 1000 GOLD PIECES.  PAY FOR CURE: YES NO
```

| | declined | paid |
| --- | --- | --- |
| `FIGHTER1` | `PLATINUM 1586` | `PLATINUM 1386` |

**No shop in this district buys.** `APPRAISE` is the sale, on the
`BUY VIEW POOL APPRAISE EXIT` bar at the armourer and the general store
at **12,10**. It opens on `YOU HAVE A FINE COLLECTION OF: 4 GEMS` over
`APPRAISE : GEMS EXIT`; `G` shows a rolled valuation
(`THE GEM IS VALUED AT 100 GP.`) over `YOU CAN : SELL KEEP`, and `S`
sells: `GEMS 3`, and the platinum up by the valuation at five gold to one.

Squares reached on foot in the civilised district:

| square | what happens |
| --- | --- |
| `3,1` | Sune's temple, healing |
| `4,4` | the city hall (leg 1 reaches its entrance event) |
| `6,2` | the training schools' lobby |
| `7,2` | the arena master: a duel, and a hireling prompt |
| `8,2`, `9,2` | the dueling rooms |
| `10,5` | the Temple of Tyr: the bishop's study, and an NPC who asks to come along |
| `10,8` | a random encounter; the district is not safe |
| `12,10` | the general store |
| `13,5` | the docks, through the temple |
| `0,4` | the gate out of the civilised district |

### Walking off the edge of the map (#102)

West through the gate at `0,4` is an area transition:

```
amberfolio: watch frame=014344 ds=0CDC 6AAD=00 6AAE=04 6AAF=06 49F3=04
amberfolio: watch frame=014908 ds=0CDC 6AAD=0F 6AAE=04 6AAF=06 49F3=04
```

X goes from `0` to `15`, a different wall set is drawn, and the bar comes
up on the other side.

---

## Leg 6 — the same game, in the browser's machine (#108, #99)

`hosts/web/tools/drive.mjs` drives the wasm module headless and spells
`--press KEY@FRAME` the SDL host's way:

```sh
node build/wasm/hosts/web/Release/drive.mjs <your-directory> START.EXE \
  --seam code-wheel --press A@7600 --press Return@7650 ... --frames 28000 \
  --quiet --dump run
```

Its report is the SDL host's (`docs/hosts.md`). Legs 0 to 2 as one
script reach `CONTINUE BATTLE:` with the fighter on one hit point:
28,001 frames, 139,204,567 steps, `factor=94.77x` on Release.

- **Cheats.** The same script with `--seam cheat-invulnerable` added ends
  at `HITPOINTS 8` instead of `HITPOINTS 1`; the seam reports `on armed`.
- **A desktop recording verifies on the module** through
  `af_machine_verify_recording`:
  `amberfolio: replay verified checkpoints=101 keys=186`. Rejected: a
  `--replay` of a driven run in `drive.mjs` (#147), because
  `hosts/web/tests/smoke.mjs` already verifies recordings in CI.
- **Arriving with a saved game** (#146). `af_machine_vfs_put` takes a
  path, `/` and `\` alike, and makes directories on the way; `drive.mjs`
  walks its directory and the dev page's picker keeps
  `webkitRelativePath`, so `\SAVE\` arrives and leg 3's load script runs
  with `--frames 12000 --quiet --dump load`. An empty `\SAVE\` is carried
  as a put plus the remove that leaves the name, the ABI having no
  `mkdir` (#273).
- **Leaving with one** (M5-D2, #170). `af_machine_vfs_get`, `_remove`
  and a tree listing; on both hosts `--vfs-list`, `--vfs-get PATH` (size
  and SHA-256, not the bytes) and `--vfs-remove PATH` (deletes a real
  file, and says so first):

  ```
  amberfolio: vfs 196 file(s)
  amberfolio: vfs \SAVE\SAVGAMA.DAT 4096
  ```

**Legs 3, 4 and 5 on both hosts** (#273): `--dump` on both,
`scripts/frames.py diff` on the stills (it says `same`), `--vfs-get` for
the files (`docs/hosts.md` §4).

Leg 3's save (legs 0 and 1, `ENCAMP`, `SAVE`, slot A; 74 keys, to tick
458,181,888, over a pristine disk) stops on both hosts at
`steps=114545472 ticks=458181888 frames=23041 cs=04C4 ip=1AA5`, with the
final still, the `.edges` and the four `\SAVE\` files identical. So do
leg 3's load over that run's directory, and, over the shipped save slots,
leg 4 (`FIGHTER1'S ITEMS` ending `HAND AXE`), leg 5's cure
(`PLATINUM 1386`) and leg 5's sale (`GEMS 3`, `PLATINUM 1606`). The
`.wav` files differ (48 kHz against 44.1 kHz), which is what the `.edges`
list is for; the `.edges` files differ only in line endings. `save` (254
checkpoints), `party` (144), `load` (100) and `temple` (181) verify on
the module through `--replay`.

Trap (#273): the two hosts' frame *N* was not the same tick, so
`--press E@20400` reached the program at different moments and the save
wrote a different slot file on each; the recording verified anyway, a
recording naming ticks. `docs/hosts.md` §4 has what differed.

---

## Leg 7 — a camp, and the Encamp Fix (M5-E1 #172, M5-E1a #186)

The Fix is a command on the camp bar, not a pulled seam.
`tests/sessions/camp.rec` and `camp-fix.rec` are the pair: slot B,
`cheat-wound-party` pulled at the same tick, one keystroke apart.

Slot C, whose party is whole, so the Fix declines (#192):

```
--seam code-wheel --seam encamp-fix
--watch 49F3 --watch 6DDA --watch 6DCA
--press A@7600 --press Return@7650      the code wheel
--press L@8950 --press C@9200           LOAD SAVED GAME, slot C
--press E@10200                         ENCAMP
--press F@10400                         the Fix, on the camp menu
```

```
amberfolio: seam encamp-fix armed
amberfolio: watch frame=010391 ds=0CDC 49F3=02 6DDA=00 6DCA=00
amberfolio: seam encamp-fix inert point_not_recognized
amberfolio: seam encamp-fix armed fired=3
```

`armed` is the camp overlay arriving (`docs/seams.md` §4); `49F3=02` is
camp; `6DDA` and `6DCA` never move, because a whole party with no pending
spell gets no rest. `inert point_not_recognized` is reported once per
enable and does not say which pass; a run pressing the camp's own `R`
prints it too. `fired=3` is the bar on two passes and the report on the
third.

The bar reads `SAVE VIEW MAGIC REST ALTER FIX EXIT`, drawn by the
program; with the seam off, `F` does nothing. The report is drawn into
the program's message panel, over a live bar that is the way out of it
(`docs/seams.md` §10):

```
                 FIX: PARTY HEALED
NO HIT POINTS RESTORED.
THE PARTY IS AT FULL HIT POINTS.
```

EXIT under the box, on the shorter boot
(`tests/visual/camp-fix-exit.leg`, driven by hand because the runner does
not yet carry `--code-wheel-answered`):

```
--seam code-wheel --code-wheel-answered --seam encamp-fix
--press L@7550 --press C@7800           LOAD SAVED GAME, slot C
--press E@8800                          ENCAMP
--press F@9600                          the Fix, which declines
--press E@10200                         EXIT
```

The mode word leaves camp at 10,227 and the first dump after it is
10,250. From there to the end of the run:

- Every row of the panel is blank; the title sits on row `0x12`, the
  first the camp teardown clears (`0x12..0x16`, never `0x11`, #298).
- Column `0x10` of row `0x10`, the viewport box's corner knot, is the
  seam-off cell (#303).
- `--watch 6B2B`: EXIT leaves 6 with the seam off and 7 with it on; the
  seam maps 7 to 6 at frame 10,226, and from 10,325 the bar row matches
  (10,300 catches it mid-draw) (#304).
- 50 of the 53 stills equal the seam-off run's; the other three are the
  bar mid-draw and the camp fire.

The same script through `drive.mjs` with `--frames 13176 --quiet` prints
the same lines plus `inert module_not_resident`, this host reaching the
overlay by a different route through the loading. So do the two scripts
below, at `fired=9` and at `encamp-fix fired=11` with
`cheat-wound-party fired=1`.

### The same leg on a wounded party (M5-E1b #189, M5-E1c #194)

Slot B holds two wounded fighters, 15 of 17 and 14 of 18, and a cleric
with five ready Cure Light Wounds.

```
--seam code-wheel --seam encamp-fix
--press A@7600 --press Return@7650      the code wheel
--press L@8950 --press B@9200           LOAD SAVED GAME, slot B
--press E@10600                         ENCAMP
--press F@10900                         the Fix
```

Six characters load slower than four, so ENCAMP goes at 10600.

```
amberfolio: seam encamp-fix armed
amberfolio: watch frame=010621 ds=0CDC 49F3=02 6DDA=00 6DCA=00
amberfolio: seam encamp-fix inert point_not_recognized
amberfolio: watch frame=012107 ds=0CDC 49F3=02 6DDA=01 6DCA=00
amberfolio: watch frame=012175 ds=0CDC 49F3=02 6DDA=00 6DCA=00
amberfolio: watch frame=012259 ds=0CDC 49F3=04 6DDA=00 6DCA=00
amberfolio: seam encamp-fix armed fired=9
```

`6DDA` up and down is the program's own rest; `6DCA` stays 0 because
three cures close both deficits (17 of 17, 18 of 18). The city watch ends
the rest with an event and the camp screen never comes back, so the
fourth point draws the report on the camp loop's exit, held by the
program's own message delay after `THE PARTY IS RUDELY INTERRUPTED!`
(#194):

```
                 FIX: INTERRUPTED!
HEALED 6 HP WITH 3 SPELLS IN 0:05.
THE PARTY IS AT FULL HIT POINTS.

CURES ARE STILL BEING MEMORIZED.
```

The mode word goes to adventuring at 12259 instead of 12182: the box
being held.

### A party the cures cannot finish (M5-E1d, #196)

`cheat-wound-party`, pulled at the camp screen, leaves every member on
one hit point (`docs/seams.md` §10); it drives the days arithmetic and
the report's exception list.

```
--seam code-wheel --seam encamp-fix --seam cheat-wound-party
--press A@7600 --press Return@7650      the code wheel
--press L@8950 --press B@9200           LOAD SAVED GAME, slot B
--press E@10600                         ENCAMP
--pull cheat-wound-party@10624          everybody down to one hit point
--press F@10900                         the Fix
```

```
amberfolio: seam cheat-wound-party served
amberfolio: watch frame=012666 ds=0CDC 49F3=02 6DDA=00 6DCA=1E
amberfolio: watch frame=012718 ds=0CDC 49F3=02 6DDA=01 6DCA=1E
amberfolio: watch frame=012787 ds=0CDC 49F3=02 6DDA=00 6DCA=00
amberfolio: watch frame=012884 ds=0CDC 49F3=04 6DDA=00 6DCA=00
```

`6DCA=1E` is thirty days: the worst survivor's deficit plus one (the
cleric, maximum 30, never a cure target). The rest screen reads
`REST TIME:  30:05:15`, the 5:15 being the program's own memorisation
time. The wandering-monster check ends the rest; the exit report:

```
                 FIX: INTERRUPTED!
HEALED 17 HP WITH 5 SPELLS IN 0:05.
FIGHTER1          1/17      SHORT 16
FIGHTER2          1/18      SHORT 17
...AND 4 MORE.
CURES ARE STILL BEING MEMORIZED.
```

Since #298 the box holds three rows, so the list is one row and
`...AND 5 MORE.`. `IN 0:05` is how long the rest took, not the days
dialled. Five spells is all the cleric holds; four went to the fighter
with the deepest deficit (maximum 42) and one to the thief (roster panel
`1 1 16 3 1 1`). Sweep:

```
  camp-fix     contrast ok  91 of 112 checkpoints identical, then
                            divergent from tick 216799088 to the end
```

Trap: a point with no address runs its guard with DS holding whatever
the program had loaded, and a roster walked through a far pointer out of
the wrong segment leaves `unmapped_memory_read` notices with the suite
green. Every point has an address.

### A party the game hurt (#269)

Slot B walked south from `4,3` to `4,6` is met by the council guard over
a `YES NO` bar; `N` is a battle against twenty-odd guards.

```
--seam code-wheel --seam encamp-fix --seam cheat-kill-all
--press A@7600 --press Return@7650        the code wheel
--press L@8950 --press B@9200             LOAD SAVED GAME, slot B
--press Right@10600 --press Right@10850   turn south
--press Up@11100 --press Up@11850 --press Up@12100      4,3 -> 4,6
--press N@12600                           refuse the guard
--press Q@13500 ... 910 of them, 150 apart      QUICK, round after round
--pull cheat-kill-all@150000              survive it
--press Q/N/Return@150200 ... 400 apart   the end of the battle
--press E@181500 --press N@182500         EXIT the loot, leave the rest
--press E@184000                          ENCAMP
--press F@186000                          the Fix
```

- `QUICK` is one round, not the battle; pressed once, the same prompt
  sits for twenty thousand frames. Damage tracks presses: 88 left the
  party 15 hit points short, 310 left it 24, 910 left it 73.
- `cheat-kill-all` touches nothing on the party's side, so every missing
  hit point is the game's own combat.
- `E`, `N`, `E` leaves a won battle: `EXIT` the loot bar, `NO` to going
  back for the rest, `ENCAMP`.

Off the loot screen at frame 181,000: `FIGHTER1 7`, `FIGHTER2 10`,
`FIGHTER3 25`, `THIEF 0` (in red: down), `CLERIC 30`, `MAGIC-USER 12`,
against maxima of 17, 18, 42, 31, 30 and 19.

```
amberfolio: watch frame=184021 ds=0CDC 49F3=02 6DCA=00 6DDA=00
amberfolio: watch frame=187585 ds=0CDC 49F3=02 6DCA=11 6DDA=00
amberfolio: watch frame=187638 ds=0CDC 49F3=02 6DCA=11 6DDA=01
amberfolio: watch frame=196113 ds=0CDC 49F3=02 6DCA=00 6DDA=00
```

`6DCA=11` is seventeen days, the thief's sixteen after the five cures
plus one; `REST TIME:  17:05:15`, and the rest runs uninterrupted. The
report, on the next pass of the menu:

```
                 FIX: PARTY HEALED
HEALED 73 HP WITH 5 SPELLS IN 17:05:15.
THE PARTY IS AT FULL HIT POINTS.
```

`17:05:15` checks against the game's clock (`14:31` at camp, `19:46` at
the report). The exception list is empty, the thief included:
unconscious and dying are two of the four codes the program's cure
applier accepts, so a member a fight leaves down is mended and never
named. The reason column needs dead, stoned or gone; two deeper runs
(1,150 presses) produced no death.

---

## Leg 8 — a map of where you have been (M5-E2, #173)

`docs/seams.md` §10 is the seam. `tests/sessions/walk.rec` and
`walk-map.rec` are the pair, with Tab in both halves at the same tick.

```
--seam code-wheel --seam automap
--press A@7600 --press Return@7650      the code wheel
--press L@8950 --press A@9200           LOAD SAVED GAME, slot A
--press Tab@11000                       the panel
--press Up@11200 ... Right@11350 ...    forty-eight moves, 150 frames apart
--press Tab@19200                       and the party list back
```

Tab is not a key this game has; with the seam off it reaches the program
and does nothing. What to see: the party walks from the docks end to the
armourer at `8,11` and the panel fills in behind it: brown streets, white
building fronts (M5-E2a), yellow door leaves, a turning arrow, and `NEW`
over `PHLAN` in the program's own glyphs (M5-E2b). The status row
`8,11 E 03:18` below is the program's. The second Tab clears the panel's
rect and calls the program's roster drawer. A character sheet, item list
or shop clears the cells and the panel returns on the next poll; at a
vendor's bar it comes down on its own and Tab is quiet (M5-E2d), leaving
the shopkeeper's portrait, his question, and a roster with no map on it.

Sweep:

```
  walk-map  contrast ok  90 of 203 checkpoints identical, then
                         divergent from tick 218787888 to the end
```

Tick 218,787,888 is frame 11,003: the Tab plus the program's poll.

The same script through `drive.mjs` with
`--until 381818240 --quiet --dump wasmmap` reports
`seam automap armed fired=1283132`, runs the same 95,454,560 steps, and
`cmp` on the two `.ppm` files says nothing.

Doors: New Phlan has no shut face and its wall set is not in the table,
so every leaf here is the fallback rule, a passable face is a door; leg
12 drives the evidence rule.

The store (M5-E2c), as a second run:

```
--seam code-wheel --seam automap --automap-store
--press A@7600 --press Return@7650      the code wheel
--press L@8950 --press A@9200           LOAD SAVED GAME, slot A
--press Tab@11000                       the panel
--press Up@11200 ... Right@11350 ...    twelve moves
--press E@13500 --press S@14500 --press A@15500    ENCAMP, SAVE, slot A
```

```
amberfolio: automap-store writes=4 reads=0 slot=A trouble=none
```

leaves `\SAVE\AFMAP.DAT`, the working table, and `\SAVE\AFMAPA.DAT`,
slot A's snapshot. Remove the working table and load slot A again:
`amberfolio: automap-store reads=1 slot=A`, and the panel comes up with
the streets on it; the module's final frame is byte for byte the
desktop's. The store is off unless asked for: a sidecar would change the
disk every session pins by name, size and SHA-256.

Traps:

- A routine the seam calls must be called at the paragraph it was linked
  at, not at the image base with the whole offset in IP
  (`docs/seams.md` §8.4).
- The load menu opens every save file in the directory to find which
  slots exist, so a store keyed on the open alone loads nine slots' maps.
  `file_event` says whether bytes moved (`docs/machine.md`).
- Rejected: gating the panel on `84E4`, because `--watch 84E4` shows it
  oscillating on every step.

---

## Presenting a document (M5-D3, #171)

```sh
amberfolio <dir> START.EXE --document "/path/to/code wheel.pdf"
node build/wasm/hosts/web/Release/drive.mjs <dir> START.EXE   --document "/path/to/code wheel.pdf"
```

```
amberfolio: document Pool of Radiance code wheel, archive release (PDF) (code wheel) sha256=0db301ae...
```

The file is read, hashed and dropped; `machine/document.h` holds the
fingerprint table and no byte of any document (CONTRIBUTING.md). An
unknown document is reported with its fingerprint, never guessed. No
seam in this build is gated: the code wheel's gate became answering once
(#290, #291), and `--seams` says what each needs (`no document`).

---

## Leg 9 — an entry of your own journal, in the game (M5-E4, #175)

`docs/seams.md` §10 is the seam and `docs/journal.md` the store. It can
be driven without a journal: `journal.txt` is whatever `--journal` wrote,
or a hand-written file in `docs/journal.md` §6's format.
`tests/sessions/reader.rec` is the session; `cite.rec` is a real citation
over a real store pinned by digest (#232).

```
--seam code-wheel --seam journal --journal-store ./journal.txt
--press A@7601 --press Return@7651      the code wheel
--press L@8951 --press A@9201           LOAD SAVED GAME, slot A
--press N@10600                         Notes, the log
--press Return@10800                    the row the cursor is on
--press N@11600                         NEXT, the entry's second page
--press E@13000 --press E@13800         out of the page, out of the log
```

```
amberfolio: journal store ./journal.txt entries=2 corrections=0 seen=2
amberfolio: host-service journal-open calls=1 last=3
```

**`Notes` is the only way in** (#346). F1 opened a number prompt until
then; the key is claimed by nothing now, and a script that presses it
gets the program's own answer to a key it has no use for.

The screen: `ENTRY 3` in the program's highlight yellow, the body in its
message green, `NEXT PREV EXIT` on the bottom row. With `--seam automap`
on too, the entry draws over the map and leaving gives the map back
(`tests/sessions/subset-map-reader.rec`). The frames are owed a re-drive
(#293).

The same script through `drive.mjs`, same flag spellings, with
`--until 240000000 --quiet --dump wasmj` reports `fired=602779` and
`journal-open calls=1 last=3 at=214772808`;
`cmp` on the two `.ppm` files says nothing. The callout's tick differs
by 17,592, which is leg 6's trap.

Trap: this leg does not drive the citation watch. The position line does
not go through the narration routine, so a probe that reaches a routine
says nothing about whether that routine sees the text (#232).

---

## Leg 10 — the journal's own log (M5-E4a #221, M5-E4b #222, #230)

`Notes` on the party's own bar opens a full-screen log of everything the
game has cited, newest first, `*` on the unread
(`tests/sessions/notes.rec`). Leg 9's prefix, then the empty log:

```
--seam code-wheel --seam journal --journal-store ./journal.txt
--document "<the code wheel>"
--press A@7601 --press Return@7651 --press L@8951 --press A@9201
--press N@10600 --press Escape@11400
```

The screen is the game's own frame with `ADVENTURER'S JOURNAL` at the
top, one sentence, and `EXIT` on the bottom row. A log with entries
wants a citation: `cite.rec` shows the city hall's four proclamations,
`*` on the three unread, the cursor row in the program's highlight.

While it is up:

- `S`, `C`, `L` and the arrows reach nothing (#230;
  `tests/visual/not-log-modal.leg`, one digest across 1,475 frames).
- `E` leaves.
- **Scripts leave by Escape.** `E` on the party's own bar is ENCAMP, so a
  seam-off run pressing `E` ends up somewhere else.
- `N` on a page is `NEXT` and not the bar's `Notes`, which is under the
  screen and unreachable while one is up.
- Return on a row opens that entry full-screen in the same box (#305);
  Escape or `E` brings the log back with the cursor where it was and the
  `*` gone. `tests/visual/not-page-back.leg` and `not-page-modal.leg`
  are the claims, not yet driven (#293).

Give-back: the frame after the log closes equals the frame before it
opened, bar row included (`tests/visual/not-log-giveback.leg`, driven on
the shorter boot), and
the highlight is handed back where the routine found it (#330,
`tests/visual/rdr-bar.leg`, which asks the runner for
`code-wheel-answered`). With `--seam automap` on, the map is redrawn at
its next arrival (#332, `tests/visual/rdr-map-back.leg`).

---

## Leg 11 — the overworld, and where you have been (M5-E5, #179; the fog M5-E5f, #263)

`docs/seams.md` §10 is the seam,
[`docs/explored-overlay.md`](explored-overlay.md) the fact table, and
`tests/sessions/wild.rec` and `wild-trail.rec` the pair. The screen is
the wilderness travel view, a 5x5 window of the overhead map.

```
--seam code-wheel --seam explored --document "<the code wheel>"
--watch 49F3:1 --watch 49FA:1
--press A@7601 --press Return@7651      the code wheel
--press L@8951 --press J@9201           LOAD SAVED GAME, slot J
--press Up@10600 ... every 150 frames   eight steps north
```

Slot J's party stands on a wilderness area (view kind 2 on disk 6). The
mode byte becomes 3 at frame 9,552, the screen has settled by 10,275, and
wandering brings an encounter within a few virtual minutes.

What to see: fog of war. Every square the party has stood on is the
game's own map; every other square of the window is a one-pixel
checkerboard of palette index 0 on half its pixels. There is no key:
`explored_reveal_radius` in `machine/automap.h` is 0 (#299).
Rejected: radius 2 or 3, because the window is five across and they
cover nothing; radius 1, because it uncovers a corridor three wide; a
solid cover, because it loses the shape of the country; a dark-grey
checker, because it reads thin and is the colour of mountain rock
(`docs/explored-overlay.md` §5).

Checks: every pixel differing from the seam-off run is inside the window
`8,8,127,127` (`tests/visual/exp-trail.leg`, `exp-steady.leg`), and the
still after the last step repeats to the end. Sweep: `wild-trail` is 107
of 140 checkpoints identical, then divergent from tick 204,866,288, the
arrival; on with the overworld never shown, `quiet-explored.rec` is all
126 checkpoints of the baseline. These were measured at the radius-one
grey and are re-driven under #293.

Store: `--automap-store` writes the trail into `\SAVE\AFMAP.DAT` with a
snapshot per slot, and the automap alone records the wilderness too
(#254). A slot with no snapshot loads an empty table, so the arrival is
the party's square and fog everywhere else.

The other two wilderness areas are reached by editing a save on a
scratch copy by `docs/explored-overlay.md` §8's offset table. The
program's status line prints `16, 32` on kind 3 and `29, 32` on kind 4
where the record holds column 3; a walk on each keeps every difference
inside `8,8,127,127`.

---

## Leg 12 — the automap's door rule, on a map that has shut doors (#268)

Slot C stands in the Kobold Caves (disk 8, area 13), which has shut
faces. Kovel Mansion (#199) is not needed.

```
--seam code-wheel --seam automap --trace
--press A@7600 --press Return@7650      the code wheel
--press L@8950 --press C@9200           LOAD SAVED GAME, slot C
--press Tab@11000                       the panel
--press Up@11200 Up@11350 Up@11500      west along the corridor
--press Right@11650                     turn north
--press Up@11800 Up@11950               north to 1,1
--press Left@12100 --press Up@12250     west to 0,1
--press Right@12400 --press Up@12550    and north through the door
--until 290000000
```

If something wanders in, `--seam cheat-kill-all` and a pull clears it.

```
amberfolio: automap doors frame=011000 disk=8 area=0D geo=0D seen=0200 table=0200 drawn shut=0 kind-seen=1 kind-table=0 no-evidence=0
amberfolio: automap doors frame=011885 ... drawn shut=0 kind-seen=2 kind-table=0 no-evidence=0
amberfolio: automap doors frame=012647 ... drawn shut=0 kind-seen=3 kind-table=0 no-evidence=0
```

`seen=0200` is bit 9, from this map's own shut faces scanned at arrival;
every leaf is `kind-seen` and `no-evidence=0`. The still: a five-cell
corridor along row 3, a two-cell passage up column 0, `KOBOLD CAVES`, red
walls, a brown floor, three leaves (the east face of `3,3`, and both
sides of the `0,1`/`0,0` border, walked through at frame 12,558), and the
arrow on `0,0`.

The counters live in `machine::automap()` (`automap.h`), are never
serialized, and print only under `--trace`. `seen` and `table` are both
`0200` here; the case where they differ, a wall-set slot filled from a
multi-block load, is a unit test and no save stands on one.

---

## What the run should not say

The three notices `docs/first-light.md` tabulates
(`undisplayable_video_mode`, `unmapped_memory_read`,
`unclaimed_port_write`) and no others, on a clean run of any leg above.
A fourth once combat runs:

```
amberfolio: notice rom_write at F0147 value=00 from=011A:230E
```

The program writes a zero into the BIOS ROM at `F000:0147`; a real PC
swallows it and so does this machine. Anything else, a `stop`, a
`device declined`, a `cpu stopped`, is a worklist line
(`docs/machine.md` §5).

## What this procedure has *not* covered

Not driven by decision: training and the inn (#104, #145), and the
dungeon beyond the gate at `0,4` (#102, #144). Open: nobody has typed a
correct answer into the real program (#290); every script and number
above is owed a re-drive on the shorter boot (#293); the journal's
residual is #270; an entry read on a display, the fog walked, a rest
heard, and any of it in a browser are `docs/hosts.md` §3's. With no
issue: `--watch` has no web equivalent, and no sound of this program has
been measured, though `--dump`, `tools/drive.mjs --dump` and
`amberfolio-dump` write the same `.edges` file (`docs/hosts.md` §4).
