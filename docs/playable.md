# Playable

How to drive a player-supplied copy from the party roster into the game —
a party, the city, a story event, a fight, a save, a load, a purchase, a
cure paid for, a gem sold, and the whole of it again on the machine a
browser runs — and what each of those legs is evidence for.

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

### Keeping a leg

A leg driven this way happens once. To keep it, record it:

```sh
  ... --record tests/sessions/NAME.rec --record-every 128
```

and the run becomes a *session* — keys, ticks and hashes, committable,
and re-runnable by `scripts/sweep.py` on a machine that has the disk.
`tests/sessions/README.md` is the rest of it: the descriptor that says
the disk is the player's own, `--pin` to write down the names and digests
of the snapshot it was recorded over, and why the runner skips rather
than fails when this disk is not that disk. 128 is the cadence a game
session uses, and it is why recording a leg costs almost nothing on top
of driving one.

Two things a recording is better at than a script, and both are reasons
to make one of any leg worth repeating. It carries its own initial
conditions, where a script only works over a directory in exactly the
state the last run left it. And it pins the *machine* rather than the
answer: a script that still reaches the shop proves the program got
there, while a recording that still verifies proves every byte of RAM and
every device register got there the same way.

---

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

`cheat-invulnerable` works: with it on the same encounter ends with the
party on its full eight hit points, where without it the party ends on
one. It had to be corrected first — its two arguments were written down
the wrong way round, and neither cheat had ever been run against the
program (#99, #103).

`cheat-kill-all` works too, and took longer: its facts were right the
whole time, but it armed against where the overlay tracker last saw its
module *read*, and the overlay manager moves the module afterwards
without reading it again. It now resolves its point through the word the
manager keeps that module's segment in, at every step (#131,
`docs/seams.md` §4). One firing ends a fight:

```
THE PARTY HAS WON.
EACH CHARACTER RECEIVES 107 EXPERIENCE POINTS.
```

A party that reaches this leg from a *saved game* is the practical way to
see it. A first-level character made in leg 0 does not survive the slums
long enough to reach a second round, and `LOAD SAVED GAME` at the main
menu (leg 3's second half) puts a whole party on the map in nine hundred
frames.

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

## Leg 4 — a shop, and a purchase (#104)

A city service, transacting: a shop reached on foot, its stock listed, an
item bought, and the money gone from the character who paid.

This leg starts from a **saved game** rather than from leg 0, for the
reason leg 2 gives — `LOAD SAVED GAME` puts a whole party on the map in
nine hundred frames, and a shop wants a party with money in it.

```
--press A@7600 --press Return@7650      the code wheel
--press L@8950 --press A@9200           LOAD SAVED GAME, slot A
```

Then walk. The party starts at `4,12 S`; the armourer is at `8,11`, and
the way there is four moves east and one north through the streets
between. The presses that get there, at **150 frames apart** (see the trap
below):

```
--press Up@11000  --press Up@11150 --press Up@11300 --press Up@11450
--press Right@11600
--press Up@11750  --press Up@11900 --press Up@12050 --press Up@12200
--press Right@12350
--press Up@12500  --press Up@12650 --press Up@12800 --press Up@12950
--press Right@13100
--press Up@13250  --press Up@13400 --press Up@13550 --press Up@13700
--press Right@13850
--press Up@14000
```

Walking onto the shop's square fires its event — a portrait, and

```
THE SHOP SPECIALIZES IN ARMS AND ARMOR. 'CAN I SHOW YOU OUR WARES?'
```

and the rest is menus:

```
--press Y@14400        yes: into the shop (BUY VIEW POOL APPRAISE EXIT)
--press B@15000        BUY: the stock list
--press B@15600        BUY again: the highlighted item, a HAND AXE
--press E@16600        EXIT the stock list
--press E@17000        EXIT the shop
--press V@17600        VIEW the first character
--press I@18200        ITEMS: what they are carrying
```

**What it is evidence for.** The shop's own screens render and answer: the
entrance event with its portrait, the shop bar, a full stock list with
prices (`HAND AXE 1`, `BARDICHE 7`, … `GLAIVE-GUISARME 10`) over its own
`ITEMS: BUY NEXT PREV EXIT` bar, the character sheet, and the item list.
That is #104's "text and list rendering", driven rather than described.

And the transaction itself, which is the part worth being careful about.
Run the same script twice, once with `--press B@15600` and once without —
`docs/seams.md`'s cheap check, applied to a purchase — and read the
character sheet both times:

| | without the buy press | with it |
| --- | --- | --- |
| `FIGHTER1`'s money | `PLATINUM 1586`, `GOLD 1` | `PLATINUM 1586`, no gold line |
| `FIGHTER1`'s pack | ends `POTION OF HEALING` | ends `POTION OF HEALING`, `HAND AXE` |

One hand axe, one gold piece, on the character who paid. Nothing else
about the two runs differs.

**Two traps, both of which cost a run.**

*Presses inside a step are lost.* A press that lands while the party is
still walking into its square is flushed with the rest of the buffer, and
the run then reads as though the key did nothing. Sixty frames apart is
enough between menu keys and **not** enough between movement keys; 150 is.
The symptom is a heading that never changes.

*The buyer is the current character, not the party.* The money comes off
one character and the item lands in that character's pack, so a run that
checks the wrong sheet sees an unchanged `PLATINUM` and concludes nothing
happened. `POOL` is the shop's own answer to that; `VIEW` plus `ITEMS` on
the paying character is the check.

**Finding a service without walking the whole grid.** The city is a
sixteen-by-sixteen grid the party walks blind, `AREA` draws it, and
sweeping it by hand is most of a session. Two things make it cheap:

- The published coordinates. Contemporary player material gives the
  civilised district's services by square — inns around `4,12`, a shop at
  `11,12`, taverns at `8,9`, a jewellers at `8,10` behind another shop at
  `9,10`, the hiring hall at `7,2`. Those are facts about a map, and they
  turn a search into a route.
- **Reading the stills for text rather than looking at them.** An event,
  a prompt or a service screen is the only thing that puts ink in the
  message box under the 3D view, so a script that counts non-black pixels
  there across a `--dump-every` run finds every event in it and names the
  frames. A wandering script — forward four, turn right, repeat — plus
  that scan found the armourer in one run without a single frame being
  looked at by eye.

Do **not** try to read walkability off the `AREA` panel. The panel is an
eleven-by-eleven window on the district (its origin was map `0,5` in the
run above, and the party arrow is what pins it), and its light-grey lines
do not correspond to the walls the party actually bumps into — a route
computed from them disagreed with the party's own movement at the first
junction.

---

## Leg 5 — the temple, and a sale (#104)

Two more city services transacting, and the instrument that made finding
them affordable.

### Watching where the party is

Leg 4's honest note was that the city is a sixteen-by-sixteen grid the
party walks blind and that sweeping it by hand is most of a session.
That is a navigation problem, not an emulation one, and `--watch` is the
answer to it: the party's position and the game's screen mode are three
bytes and a byte in the program's data segment, and a line printed every
time one of them changes is a readable trail of a run.

```
--watch 6AAD --watch 6AAE --watch 6AAF --watch 49F3
```

| offset | what |
| --- | --- |
| `6AAD` | the party's X on the area map |
| `6AAE` | its Y |
| `6AAF` | its facing: 0 N, 2 E, 4 S, 6 W |
| `49F3` | the screen mode: 0 menu, 1 a portrait sub-view, 2 camp, 3 and 4 adventure, 5 tactical combat, 6 a banner |

```
amberfolio: watch frame=012814 ds=0CDC 6AAD=07 6AAE=05 6AAF=00 49F3=04
amberfolio: watch frame=013114 ds=0CDC 6AAD=08 6AAE=05 6AAF=02 49F3=04
```

Those are facts about this program, in the same table the seams are
written from (`docs/seams.md`), and `ds=0CDC` is the data segment they
mean something in on this edition — the other segments the flag prints
are frame boundaries that landed elsewhere, and filtering on the one is
how the trail is read.

The trail is the whole method. A leg is now: press the keys, read the
lines the party actually moved on, and write the next leg from where it
stopped. A wall shows up as a press that changed the facing and not the
position, which is the same symptom as a flushed keystroke and used to
be indistinguishable from it.

### Two rules the trail makes obvious

*Turning and walking are not the same cadence.* Ninety frames apart is
enough between menu keys; a hundred and fifty is what movement wants
(leg 4's trap, and the trail is where it shows). A turn lost inside a
step reads as a party that walked east when it was told to walk north,
three squares later.

*An entrance event stops the walk until it is answered.* A script that
walks past a service square and does not answer its prompt stands there
for the rest of the run, and every remaining keystroke is flushed. A
sweep therefore presses `Return` and `N` after every move — the two
answers that dismiss a "press return to continue" and decline a "yes or
no", neither of which does anything at the exploration bar.

### The temple, and paying for a cure

The civilised district's healing temple is at **3,1**, and walking onto
it is the whole of the entrance:

```
YOU ARE WELCOMED BY PRIESTESS JOY OF SUNE. 'DO YOU SEEK HEALING?'
```

`Y` opens the service, and its command bar is the shop's bar with one
word changed:

```
HEAL VIEW POOL APPRAISE EXIT
```

`H` asks which character, and then lists what the temple sells —
`CURE BLINDNESS`, `CURE DISEASE`, the three cure-wounds spells,
`NEUTRALIZE POISON`, `RAISE DEAD`, `REMOVE CURSE`, `STONE TO FLESH`,
`EXIT` — a nine-line list over its own screen, which is #104's "text and
list rendering" for the second time and the first with prices behind it.

The transaction is two prompts, and the party being in perfect health is
not an obstacle to it:

```
FIGHTER1 IS NOT BLIND.  CAST CURE ANYWAY: YES NO
CURE BLINDNESS WILL ONLY COST 1000 GOLD PIECES.  PAY FOR CURE: YES NO
```

Run the same script twice, once answering `Y` to the second prompt and
once `N` — `docs/seams.md`'s cheap check, applied to a purchase the way
leg 4 applied it — and read the payer's sheet both times:

| | declined | paid |
| --- | --- | --- |
| `FIGHTER1` | `PLATINUM 1586` | `PLATINUM 1386` |

Two hundred platinum, which is the thousand gold the temple asked for at
this game's five-to-one, off the character the service was bought for.
Nothing else about the two runs differs.

### The sale, which is not called selling

Leg 4 recorded that `SELL` is not on the armourer's bar and guessed that
it belonged to a shop that buys. It does not. **No shop in this district
buys**, the general store at **12,10** included, and its bar is the same
five words as the armourer's:

```
BUY VIEW POOL APPRAISE EXIT
```

`APPRAISE` is the sale. It opens on what the current character is
carrying that a shop will take —

```
YOU HAVE A FINE COLLECTION OF:
4 GEMS
```

over a bar reading `APPRAISE : GEMS EXIT` — and `G` sells one:

| | before | after |
| --- | --- | --- |
| `FIGHTER1`'s gems | `GEMS 4` | `GEMS 3` |
| `FIGHTER1`'s money | `PLATINUM 1586` | `PLATINUM 1588` |

One gem, two platinum, on the character who was carrying it. That is
#104's "sell", under the name the game gives it.

### What else is on this map

Every square below was reached on foot and fired what is written beside
it. They are facts about a map, like leg 4's published coordinates, and
they are here so that the next person walks a route rather than a grid:

| square | what happens |
| --- | --- |
| `3,1` | Sune's temple — healing, above |
| `4,4` | the city hall (leg 1 reaches its entrance event) |
| `6,2` | the training schools' lobby: a note directing magic-users and clerics north, fighters and rogues east |
| `7,2` | the arena master — a duel, and a prompt offering a hireling |
| `8,2`, `9,2` | the dueling rooms behind him |
| `10,5` | the Temple of Tyr: the bishop's study, and an NPC who asks to come along |
| `12,10` | the general store — the sale, above |
| `13,5` | the docks, through the temple |
| `0,4` | the gate out of the civilised district |

`10,8` put the party into a random encounter, which is worth knowing
before assuming this district is safe: leg 4's note that slot A's
quarter produced none in nine minutes of walking was a fact about a
route, not about the map.

### Walking off the edge of the map (#102)

The gate at `0,4` is the district's western edge, and stepping west
through it is the first **area transition** this machine has run:

```
amberfolio: watch frame=014344 ds=0CDC 6AAD=00 6AAE=04 6AAF=06 49F3=04
amberfolio: watch frame=014908 ds=0CDC 6AAD=0F 6AAE=04 6AAF=06 49F3=04
```

One step, and the party's X goes from `0` to `15` — a different sixteen
by sixteen, a different wall set drawn around it, and the clock back to
where the new area starts it. The exploration bar comes up on the other
side and the party walks on.

That is a code path nothing before it had exercised: leg 1's tour is one
district and legs 2 to 5 never leave it. What is on the other side —
the slums, and the dungeons that open off them — is still the gap below.

---

## Leg 6 — the same game, in the browser's machine (#108, #99)

Every leg above is the desktop host. This one is the wasm module, driven
headless by `hosts/web/tools/drive.mjs`, which takes a directory and a
program the way the SDL host does and spells `--press KEY@FRAME` the same
way — so a leg written here runs there:

```sh
node build/wasm/hosts/web/Release/drive.mjs <your-directory> START.EXE \
  --seam code-wheel --press A@7600 --press Return@7650 ... --frames 28000 \
  --quiet --dump run
```

It prints the load line, the edition, the stop report, the state hash,
the audio counters, the seam table and a throughput line. `docs/hosts.md`
has the tool; this is what it says about the game.

### It runs the loop

Legs 0, 1 and 2 as one script — the code wheel answered by its seam, a
character generated and put in the party, `BEGIN ADVENTURING`, the
guide's tour taken with fifty Returns, twenty steps north into the slums,
`C` and `Q` — reaches the tactical view and a fight that ends at
`CONTINUE BATTLE:` with the fighter alive on one hit point. 28,001
frames, 139,204,567 steps.

**Throughput**, which #116 closed without leaving a number in the tree
and this is:

```
amberfolio: throughput virtual=466.667s wall=4.924015s factor=94.77x
            steps=139204567 steps/s=28270540
```

Ninety-five times real time on the Release module, running the actual
game rather than a synthetic loop. #107's default preset needs one.

### The cheats toggle here too (#99)

The same script, the same directory, one flag apart:

| | fighter at `CONTINUE BATTLE:` |
| --- | --- |
| `--seam code-wheel` | `HITPOINTS 1` |
| `--seam code-wheel --seam cheat-invulnerable` | `HITPOINTS 8` |

The state hashes differ, the seam reports itself `on armed`, and the
screens say what the numbers say. That is #99's last owed item — the
cheats seam toggled end to end against the player's copy from **both**
hosts — and it is the same evidence `tests/sessions/fight-cheat.rec`
carries on the desktop, taken the only way the web host can take it,
which is by hand.

### A recording of the real game, verified on the other target

The stronger claim, and the one the session library exists for. The same
script was recorded on the **desktop** host and the recording handed to
the **wasm** module through `af_machine_verify_recording`:

```
amberfolio: replay verified checkpoints=101 keys=186
```

A hundred and one checkpoints of a 139-million-step run of a real game —
every byte of RAM, every device register, the clock — reproduced exactly
by a different compiler on a different architecture. Until now that claim
rested on `spin.rec`: four frames of a machine executing `JMP $` (#142).

It cannot be committed, for the reason nothing here can, and there is no
`--replay` on the web driver yet, so this is a procedure and not a test.
Both of those are follow-ups rather than doubts.

### What the browser cannot do yet

**A saved game cannot be loaded there.** `af_machine_vfs_put` puts a file
in the **root directory** and there is no door for anything below it, so
a host handing over a real installation hands over its root and drops
`\SAVE\` — which is where every shipped save slot lives. The dev page's
picker says as much in its own comment and the driver reports it:

```
amberfolio: disk skipped SAVE (not a file)
```

The core is not the limit — `machine::filesystem` has directories, and
the program makes its own `\SAVE\` on the web exactly as it does on the
desktop, so a save and a load **within one session** are not blocked.
What is blocked is starting from a directory that already has one, which
is how legs 3, 4 and 5 all start. Until that door exists, the browser can
play the game from the beginning and not from where you left it, and
#105's round trip on the web is the in-session half only.

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

- **Two of the city services** (#104). Legs 4 and 5 buy, heal and sell,
  and the city hall, the arena, the dueling rooms and the training
  schools' lobby all render and answer. What has **not** been transacted
  is **training** — no character with the experience for a level has been
  taken through it, and the lobby's own note says the halls are by class
  — and the **inn**, which has not been found. Those two are what is left
  of "every city service the game offers", and neither is blocked on
  anything the machine does.
- **The dungeon.** Everything above is the city and its slums. The gate
  out of the civilised district is at `0,4` and leg 5's routing method
  works on the other side of it; nobody has walked through.
- **The dev page itself** (#108). Leg 6 drives the wasm module headless
  and the module is the same one the page loads, but nobody has run any
  of this in a browser: the canvas, the AudioWorklet, the seam
  checkboxes and the directory drop are checked by a node harness and by
  reading, not by looking. `docs/hosts.md` §3 is still where a person
  closes that.
- **A saved game in the browser.** Leg 6's last section: the VFS door
  reaches the root directory only, so `\SAVE\` cannot be handed over and
  legs 3, 4 and 5 have no web equivalent.
- **Audio beyond "there was one."** The first sound this program makes is
  in combat. `docs/hosts.md` §4 now measures the speaker — the edge list
  a run published, the box filter's DC offset, the two hosts' sample
  rates against each other — but every one of those numbers comes from a
  program in `tests/programs`. Nothing has measured *this* program, and
  §3 is still where a person checks that a pressure wave left a speaker
  (#106).
