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
    --seam code-wheel --code-wheel-answered --fast max \
    --press KEY@FRAME ... \
    --until <ticks> --dump run --dump-every 90
```

> **Every script below this line is one version out of date, and #293
> is where they are brought back.** They were written when the
> code-wheel seam *answered* the challenge, so each of them presses two
> keys at it (`A@7600`, `Return@7650`) and everything after those keys
> is timed against a boot that drew that screen. Since #291 the seam
> only watches, and `--code-wheel-answered` — the flag above — makes
> the program skip the challenge outright instead. So a driven run
> needs that flag, the two presses at the challenge come out, and
> **every frame number after them moves earlier**. The keystrokes
> themselves, and what each leg is evidence for, are unchanged; the
> numbers are what #293 re-derives, one session at a time, against the
> recordings it re-makes.

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
--seam cheat-invulnerable --seam cheat-kill-all --pull cheat-kill-all@600
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

**Since #161 it is pulled rather than left on**, which is why the flags
above carry a `--pull` as well as a `--seam`. A person playing pulls it
with **Pause/Break** on the desktop host or with the `pull` button beside
the seam's checkbox on the page; a script names the frame. The run that
produced the two lines above predates the trigger and was driven by the
flag alone, so repeating it now needs the pull.

**And since #163 the pull is served where the person is**, not at the end
of the round. The seam now carries a second point with no address at all,
offered at every step boundary while the pull is outstanding, which acts
at the first step where it can confirm from the program's own structures
that this is a combat it recognizes; the end check stays underneath it as
the fallback (`docs/seams.md` §10). What it does there is **120 points of
damage** to every standing enemy rather than an instant slaying, so a
combatant tougher than that survives with fewer hit points and is left
standing.

**Measured, on a player's copy**, by driving `fight.rec`'s own key
script. Note the `--seam code-wheel`: a live driven run without it sits
at the copy-protection challenge for ever and never reaches a fight —
and since #291 it needs `--code-wheel-answered` beside it, because the
seam watches for a person answering the question rather than answering
it.

| pulled at | the seam's end-of-run line | |
| --- | --- | --- |
| frame 13500, just after `Q` starts the round | `fired=1 reached=1 waited=0` | served at the instant of the pull |
| frame 12700, before the round starts | `fired=1 reached=0 waited=8327644` | 6.98 virtual seconds: the guard declined until the roster was one it recognized, then served |
| frame 14000, after the combat ended | `fired=0 reached=1 waiting` + `inert point_not_recognized` | declined, kept the pull, said so |

Before the change, the same script pulled before the round waited
`22110288` ticks — 18.5 virtual seconds — because `reached=1`: the end
check is arrived at exactly once per encounter. `waited=` is the answer
to "was it immediate"; `reached=` is what the pull would have cost with
only the old point, and the two together are the comparison.

**`debug_damage` is measured** (#271), and so is the thing the seam's
guard had assumed about which side the party is on. Both came out of two
driven fights on 2026-09-02, from the shipped **slot J**, whose party of
three — `HULK`, `MULE`, `THIEF` — starts on the wilderness:

```
--press L@8950 --press J@9200            LOAD SAVED GAME, slot J
--press Up@10650 ... every 150 frames    walk north across the wilderness,
--press Return@+50 --press N@+100          answering anything that asks
--press C@17500                          COMBAT, at the encounter prompt
--pull cheat-kill-all@18400              with the tactical map up
--watch 49F3 --watch 6814:2              the mode byte, and both body counts
```

`6814` and the byte after it are the program's own count of who is still
standing on each side, and reading them as one word is the whole
measurement:

| the fight | before the pull | at the frame of the pull |
| --- | --- | --- |
| a group of **seven soldiers** at 9,2 N | `6814=0703` | `6814=0003`, and `fired=1 reached=1 waited=0` |
| a group of **two centaurs** at 10,2 N, the roster panel showing `CENTAUR HITPOINTS 20 AC 5` | `6814=0203` | `6814=0003`, served at the instant of the pull |

Both ended `THE PARTY HAS WON` — the soldiers at 107 experience points
each, which is the same encounter the seam's own note was written from.
So one pull finishes what a routed walk reaches, and the low byte of the
pair — 3, the party's own size — is **side 0 watched** rather than taken
from the fact table.

What it does not do is find the ceiling. The centaurs at twenty are the
only hit points anything on this route states, the field is a byte, and
a combatant between 121 and 255 has not been met. A pull that leaves an
enemy standing is still the number being wrong rather than the seam.

Getting somewhere tougher costs more than a `--press` route: slot E's
party stands in a city area that produced no encounter in twenty-seven
moves, and the slums from `fight.rec`'s own disk produce a single orc.

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

(`cheat-invulnerable` is not a trigger and never was — a party's not
taking damage is a property rather than an act — so this leg is
unchanged by #161. The kill-all leg above it is the one that now needs a
pull.)

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

It cannot be committed, for the reason nothing here can — the disk is a
player's own — so it is a procedure and not a test, and it would have
been one however the driver was built. `drive.mjs` deliberately does not
grow a `--replay` for it (#147): the wasm module already verifies
recordings through `af_machine_verify_recording`, and
`hosts/web/tests/smoke.mjs` asks it that on every CI run. What is
un-repeatable here is this particular run, not the capability.

### Arriving with a saved game (#146)

This used to be the section saying a browser could not. `af_machine_vfs_put`
put a file in the **root directory** and there was no door for anything
below it, so a host handing over a real installation handed over its root
and dropped `\SAVE\` — which is where every shipped save slot lives:

```
amberfolio: disk skipped SAVE (not a file)
```

Legs 3, 4 and 5 all begin with `LOAD SAVED GAME`, and none of them had a
web equivalent for that reason and no other. The core was never the
limit: `machine::filesystem` has directories, and the program makes its
own `\SAVE\` on the web exactly as it does on the desktop, so a save and
a load **within one session** were never blocked. What was blocked was
arriving with one.

The door takes a **path** now, and the rule that decides what a path
means is still core's alone: `af_machine_vfs_put` canonicalizes every
component, takes `/` and `\` alike (a browser hands a page
`webkitRelativePath`, and that is the spelling it comes in), and makes
the directories on the way. Above it, `drive.mjs` walks the directory it
is given instead of reading only its top, and the dev page's picker keeps
the relative path it used to throw away. So the skipped line is gone and
legs 3 to 5 are drivable here with the keystrokes they are written with:

```sh
node build/wasm/hosts/web/Release/drive.mjs <your-directory> START.EXE \
  --seam code-wheel --press A@7600 --press Return@7650 \
  --press L@8950 --press A@9200 --frames 12000 --quiet --dump load
```

What this repository checks is the **mechanism**, never the game: the
wasm smoke test puts a file below the root and has a program of its own
open it at its DOS path, and the path semantics — a nested put, a parent
made on the way, and each path a file cannot live at — are in the unit
suite. Running the legs themselves against a browser's copy is a
procedure a person carries out, like every other line in this file, and
the last section still records it as owed.

### Leaving with one (M5-D2, #170)

The door was one-way until M5. A page could hand an installation over a
file at a time and could not read back a single byte of what the run
wrote — so leg 3's save was something a browser did and nothing a browser
could show. `af_machine_vfs_get`, `_remove` and a listing that walks the
whole tree are the other direction, and `drive.mjs --vfs-list` is what a
web run says about the disk afterwards:

```sh
node build/wasm/hosts/web/Release/drive.mjs <your-directory> START.EXE   --press ... --frames 12000 --quiet --vfs-list
```

```
amberfolio: vfs 196 file(s)
amberfolio: vfs \SAVE\SAVGAMA.DAT 4096
...
```

The SDL host has the same three, spelled the same, over the real
directory it was pointed at: `--vfs-list`, `--vfs-get PATH` (which prints
the size and the SHA-256 of the bytes that came back, not the bytes), and
`--vfs-remove PATH` (which deletes a real file on the player's disk, and
says so before it does). So "what did the run leave behind" is a question
both hosts answer in the same words, which is what makes an exploration
sidecar (#173) checkable on either.

The mechanism is what CI checks, as ever: the wasm smoke runs a program
of its own that writes a file below the root, then lists it, reads it
back and removes it; the SDL host's own smoke case does the same three
against a directory on disk and then looks at the disk.

### Legs 3, 4 and 5, on both hosts, compared (#273)

The two sections above are doors. This is the walk through them: legs 3,
4 and 5 driven on the wasm module against a player's copy, from the same
script the desktop host was given, and the artefacts diffed —
`docs/hosts.md` §4's method, which is `--dump` on both and compare the
files. Five scripts, because leg 3 is a round trip and leg 5 is two
transactions:

| | disk it starts from | what the script does |
| --- | --- | --- |
| leg 3, save | pristine | legs 0 and 1, then `ENCAMP`, `SAVE`, slot A — 74 keys, to tick 458,181,888 |
| leg 3, load | the directory that run wrote | the code wheel, `LOAD SAVED GAME`, slot A |
| leg 4 | the shipped save slots | slot A, the twenty-one moves to the armourer, `Y`, `BUY`, `BUY`, out, `VIEW`, `ITEMS` |
| leg 5, the cure | the same | slot A, the route to Sune's temple at 3,1, `HEAL`, `CURE BLINDNESS` cast anyway and paid for, `VIEW` |
| leg 5, the sale | the same | slot A, leg 4's route, `APPRAISE`, `GEMS`, `SELL`, out, `VIEW` |

And the results, which are the same word each time:

| | stop line | final still | `.edges` | files written |
| --- | --- | --- | --- | --- |
| leg 3, save | identical | identical | identical | identical |
| leg 3, load | identical | identical | identical | identical |
| leg 4 | identical | identical | identical | — |
| leg 5, the cure | identical | identical | identical | — |
| leg 5, the sale | identical | identical | identical | — |

"Identical" is meant literally. The stop line is core's own and matches
character for character, `cs:ip` included — leg 3's save ends at
`steps=114545472 ticks=458181888 frames=23041 cs=04C4 ip=1AA5` on both.
The stills are compared with `scripts/frames.py diff`, which says `same`
rather than a pixel count. The `.edges` files differ only in line endings,
because the desktop host writes them through a Windows text stream. The
files are compared by SHA-256 through `--vfs-get` on both hosts (#273's
addition to `drive.mjs`): leg 3's save writes four files into `\SAVE\`
and all four digests agree, the slot file's included.

The `.wav` files do **not** agree and are not expected to: the desktop
renders at 48 kHz and the page at 44.1 kHz. That is why the edge list
exists (`docs/hosts.md` §4).

Reading the numbers off the stills, since a matching still is only worth
what is on it: leg 4 ends on `FIGHTER1'S ITEMS` with `HAND AXE` at the
bottom of the pack; leg 5's cure ends on the sheet at `PLATINUM 1386`,
which is this document's own recorded figure for the paid case, two
hundred platinum below the 1586 a declined run leaves; leg 5's sale ends
on the same sheet at `GEMS 3` and `PLATINUM 1606`, having started at
`GEMS 4` and 1586.

Two honest notes on that last row. The sale was driven at the
**armourer**, not at the general store at 12,10 where this document first
recorded it — the two have the same five-word bar and `APPRAISE` is on
both, and reusing leg 4's proven route is a shorter script than a second
walk across the district. And the gem fetched 100 gp here against the 10
recorded above, because the screen shows a rolled valuation
(`THE GEM IS VALUED AT 100 GP.`) and this run rolled differently: the
transaction is the claim, the price is not. There is one more prompt than
this document had: `G` opens the valuation over a `YOU CAN : SELL KEEP`
bar, and `S` is what sells.

**And the recordings, which are the stronger claim.** Every committed
session over these legs' disks was handed to the wasm module through
`--replay` and verified — `save` at 254 checkpoints, `party` at 144,
`load` at 100, `temple` at 181. `save` and `party` are the two that could
not be handed to it at all before (`tests/sessions/README.md`), and what
was in the way was an empty `\SAVE\`, which a directory-walking host had
no file to carry. It carries one now.

**What driving them found, and no replay could have.** The two hosts'
frame *N* was not the same tick, so "the same script on both hosts" was
not the same run: `--press E@20400` reached the program at two different
moments, and leg 3's save wrote a different slot file on each. The
recording verified while the driven pair diverged, because a recording
names ticks and never asks a host what a frame is. `docs/hosts.md` §4's
"What the first diff of two hosts' edge lists found" is the whole of it —
what differed, by how much, and why nothing already in the tree could
have shown it.

---

## Leg 7 — a camp, and the Encamp Fix (M5-E1 #172, M5-E1a #186)

The first M5 enhancement, on a player's copy. Since #186 the Fix is
**not** a pulled seam: it puts one more command on the camp screen's own
bar and a person presses its letter, exactly as they press the game's
own. There is nothing on the command line to ask with.

This leg starts from a saved game, for the reason legs 4 and 5 give — and
from **slot C**, whose party is four strong and reaches the camp screen
from where it stands.

```
--seam code-wheel --seam encamp-fix
--press A@7600 --press Return@7650      the code wheel
--press L@8950 --press C@9200           LOAD SAVED GAME, slot C
--press E@10200                         ENCAMP
--press F@10400                         the Fix, on the camp menu
```

and the trail that makes it readable, beside leg 5's `49F3`:

```
--watch 49F3 --watch 6DDA --watch 6DCA
```

| offset | what |
| --- | --- |
| `6DDA` | non-zero while the rest screen is the screen |
| `6DCA` | the days field of the rest clock — what the Fix dials |

What it is evidence for: the command appearing on the game's own bar, the
letter being taken back off the game's own menu-bar routine — and, since
#192, the command **declining**, because slot C's party is whole.

```
amberfolio: watch frame=009868 ds=0CDC 49F3=04 6DDA=00 6DCA=00
amberfolio: seam encamp-fix armed
amberfolio: watch frame=010391 ds=0CDC 49F3=02 6DDA=00 6DCA=00
amberfolio: seam encamp-fix inert point_not_recognized
amberfolio: seam encamp-fix armed fired=3
```

Read it in order. `armed` at 10360 is the camp screen's **overlay**
arriving — the seam's three points live in it and are resolved through
the program's own note of where it is (`docs/seams.md` §4), so until that
word is written there is nothing to arm. `49F3=02` at 10391 is camp. And
then **nothing else in the trail moves at all**: the days field `6DCA`
and the rest-screen flag `6DDA` stay at zero to the end of the run.

That is the whole of what #192 changed, seen from outside. This leg used
to show `6DCA=01` here and a day of rest after it, and the day was never
the arithmetic's — it was keeping point 3's signature non-zero
(`docs/seams.md` §10). Two things make a rest worth asking for, a hit
point somebody is short and a spell somebody is holding pending, and slot
C's party has neither. So the Fix declines rather than spending a day of
the player's game to look busy.

`inert point_not_recognized` is the point at the menu-bar routine's
return declining — **reported once per enable, and it does not say which
pass it was**, which is worth knowing before reading much into it: the
same line comes out of a run that presses the camp menu's own `R` and
never asks for the Fix at all. What the whole-party case guarantees is
that there is at least one such decline, on the pass where the Fix's own
letter came back and the command found nothing to rest for.

`fired=3` is the three acts: the bar built on each of two passes through
the menu, with no key posted and no field written — and then the report
(#189), on the pass after the command declined.

**And it is on the screen.** With the seam on, the camp menu's bar reads

```
SAVE VIEW MAGIC REST ALTER FIX EXIT
```

drawn by the program, in the program's font, with the program's
highlighting — the prompt the game puts in front of that bar is blanked
for the one call, which is where the four columns come from. With the
seam off the same script leaves the game's own bar, prompt and all, and
`F` does nothing at all: the camp loop compares the letter against its
own commands and goes round again. That is the whole of the difference
`tests/sessions/camp.rec` and `camp-fix.rec` record, and the two
recordings differ by one keystroke at one tick.

**And the command says what it did**, which is M5-E1b (#189) and the
half of this seam that was unwritten until now. The pass after the
command finishes, the seam frames the program's own message panel and
writes into it — through the program's own frame and string drawers, so
what is on the screen is the program's box, the program's font, the
program's colours, and a title centred with the program's own frame
arithmetic — one row below the border since #298, below:

```
                 FIX: PARTY HEALED
NO HIT POINTS RESTORED.
THE PARTY IS AT FULL HIT POINTS.
```

**And the bar under it is the way out.** The box is drawn before the camp
screen's own command bar goes out, so what the player is looking at is the
report and a live `SAVE VIEW MAGIC REST ALTER FIX EXIT` — any key takes
them somewhere and takes the box with it. Nothing says "press any key",
because the thing on the screen that works is the thing on the screen.

That whole picture is four calls into the program and not one glyph
drawn here. This machine has a font and it is deliberately not the game's
(`docs/seams.md` §3), so a seam that rasterized its own lettering would
have put visibly foreign text beside the game's own, on the game's own
screen — which is the failure #186 was filed about, one layer down.

**And the box leaves when camp does** (M5-E1e, #298), which for a
milestone it did not: a player who pressed EXIT under this very box
found `FIX: PARTY HEALED` still on the adventuring screen, under the
game's own bar, with the rest of the box gone. The frame had been handed
the title and puts it on the box's own top row, `0x11`, and the camp
loop's teardown clears the panel from `0x12` down — the row its own
`THE PARTY MAKES CAMP...` sits on — and nothing above. Reproduced
headlessly on the shorter boot #291 left, which is why the frames below
are not the ones above:

```
--seam code-wheel --code-wheel-answered --seam encamp-fix
--press L@7550 --press C@7800           LOAD SAVED GAME, slot C
--press E@8800                          ENCAMP
--press F@9600                          the Fix, which declines
--press E@10200                         EXIT
```

Before the change, from frame 10,250 — the first dump after the mode
word leaves camp at 10,227 — to the end of the run, row `0x11` holds the
title's 394 pixels and rows `0x12..0x16` hold nothing: the maintainer's
screenshot, frame for frame. After it, the title is on `0x12` (the frame
is handed no title and the string drawer is handed one more line) and
every row of the panel is blank from 10,250 on. The same script with the
seam off, diffed frame by frame, differs from the seam-on run only on
the bar, in the panel's rows `0x12..0x16` while the box is up, in the
camp banner's animated fire, and at one 8-by-8 cell — the knot at the
panel's top-left junction, which the report's frame paints over with a
plain edge tile exactly as the program's own cast screen from camp does.
`tests/visual/camp-fix-exit.leg` is that run as a leg, and its header
says why the runner has not run it yet.

**The healing is the next leg's evidence and no longer this one's.**
Slot C's party is whole, so there is no hit point here for the program's
own heal tick to give back and nothing for this run to show. That is the
right behaviour and it is a worse demonstration, which is exactly why the
wounded leg below exists — and why `tests/sessions/camp-fix.rec` now says
in its own descriptor that it pins the command *appearing* and no longer
pins the command *working*.

**And the same leg on the browser's machine**, which is what #172's exit
criterion asks for. `drive.mjs` takes the same flags leg 6 gives it:

```sh
node build/wasm/hosts/web/Debug/drive.mjs <your-directory> START.EXE \
  --seam code-wheel --seam encamp-fix --press A@7600 ... --press F@10400 \
  --frames 13176 --quiet
```

```
amberfolio: seam encamp-fix armed
amberfolio: seam encamp-fix inert module_not_resident
amberfolio: seam encamp-fix inert point_not_recognized
amberfolio: seam encamp-fix armed fired=3
```

The same three acts, and the same two `inert` sentences — one of them the
overlay not being in memory yet, which is the fail-closed direction a
module-qualified point has and which this host reaches by a slightly
different route through the loading. Two hosts that share no compiler, no
standard library and no SHA-256 implementation agree about what the seam
did, about what it declined to do, and about the report it drew — which
is what #172's and #189's exit criteria ask for. The box itself has been
looked at on the desktop host; the browser's agreement is that its
machine took the same three acts, which is what a headless run of it can
say.

### The same leg on a wounded party (M5-E1b #189, M5-E1c #194)

The shipped slots are not all whole. **Slot B** holds two wounded
fighters — 15 of 17 and 14 of 18 — and a cleric with five ready Cure
Light Wounds, which is the party leg 7 had been missing since M4:

```
--seam code-wheel --seam encamp-fix
--press A@7600 --press Return@7650      the code wheel
--press L@8950 --press B@9200           LOAD SAVED GAME, slot B
--press E@10600                         ENCAMP
--press F@10900                         the Fix
```

The load is slower than slot C's — six characters — so ENCAMP goes at
10600 rather than 10200, and a press that lands while the game is still
drawing is drained and lost.

```
amberfolio: seam encamp-fix armed
amberfolio: watch frame=010621 ds=0CDC 49F3=02 6DDA=00 6DCA=00
amberfolio: seam encamp-fix inert point_not_recognized
amberfolio: watch frame=012107 ds=0CDC 49F3=02 6DDA=01 6DCA=00
amberfolio: watch frame=012175 ds=0CDC 49F3=02 6DDA=00 6DCA=00
amberfolio: watch frame=012259 ds=0CDC 49F3=04 6DDA=00 6DCA=00
amberfolio: seam encamp-fix armed fired=9
```

`6DDA` up at 12107 and down at 12175 is the program's own rest, and
`6DCA` never leaves zero for the whole run. That is the reading that
matters here, and this trail was showing it backwards until now: the rest
happens, and **no day is dialled to make it happen**.

Nine acts, two more than before this leg had a fourth point: the bar
spliced on each pass, the cures cast, the rest asked for, and the report
drawn on the way out of camp. **How many cures** is not a thing to read
off that number — the report below says it, and says three. The party
comes out **17 of 17 and 18 of 18** — the cures closed both deficits, so the days field stays at 0
and the rest is the memorization time the program's own wrapper computed.
Then the program answers it with an event of its own — the city watch
rousting the party. Its rules, running inside the rest the seam asked
for, which is the whole of what "the game's own routines do the work" is
supposed to mean.

**And the game does not hand the camp screen back**, which is the finding
this leg produced for #189 and the reason there is a fourth point. The
city watch does not interrupt the rest and return to the camp menu: the
mode word goes from camp to adventuring and stays there, the camp screen
is gone, and with it the overlay the seam's points live in. The pass of
the menu the report is drawn on never comes.

**So it says so on the way past** (M5-E1c, #194). The camp loop's own
exit is the fourth point, and the loop's own out-parameter says why the
party is leaving — non-zero only when a wandering-monster check ended a
rest. The box lands on the panel the rest orchestrator has just cleared,
in the game's own lettering:

```
                 FIX: INTERRUPTED!
HEALED 6 HP WITH 3 SPELLS IN 0:05.
THE PARTY IS AT FULL HIT POINTS.

CURES ARE STILL BEING MEMORIZED.
```

and is held there by the program's own message delay — the same routine
the program calls after its own `THE PARTY IS RUDELY INTERRUPTED!`, which
is what a box with no command bar under it has instead of a way out.

Read the trail for what that cost: the mode word used to go to
adventuring at **12182** and now goes at **12259**, which is the box
being drawn and held. Everything before 12182 is the same run to the
frame. After the delay the program clears the panel, repaints the screen
and runs its camp-post event exactly as it did before — the party is back
in the city view at 12450, out of camp.

The report is also the first driven statement of what the command
actually did, and it corrects this document: **three** cures were cast,
not two, closing six hit points between the two fighters (15/17 → 17 and
14/18 → 18). The yellow line is #189's promise saying out loud where it
could not quite be kept — the cures were queued back before they were
spent, and the interruption came before the rest had memorized them.

**And the same thing on the browser's machine**: `drive.mjs` on the same
script reports `fired=9`, the same nine acts.

### A party the cures cannot finish (M5-E1d, #196)

Everything above ends with the party whole, because the cures close slot
B's deficit before the rest is ever asked for. So two pieces of the Fix
had never run on a player's copy at all: **the days arithmetic**, which
had only ever dialled zero, and **the report's exception list**, the rows
that name who is still short, which had been drawn by tests and looked at
by nobody.

What that wants is a party that has been in a hard fight and survived it,
and there is no way to get one: leg 2's fight destroys the party, no
shipped save slot holds a hurt one, and a save file this project wrote
would be a save file nobody else's machine has. So it is a **debug
cheat** that supplies it — `cheat-wound-party`, pulled at the camp
screen, leaves every member on one hit point through the same write the
program's own damage routine makes (`docs/seams.md` §10). What is
synthetic here is only how they got hurt; the healing, the arithmetic and
the box are the same code a fight would have reached.

```
--seam code-wheel --seam encamp-fix --seam cheat-wound-party
--press A@7600 --press Return@7650      the code wheel
--press L@8950 --press B@9200           LOAD SAVED GAME, slot B
--press E@10600                         ENCAMP
--pull cheat-wound-party@10624          everybody down to one hit point
--press F@10900                         the Fix
```

```
amberfolio: watch frame=010621 ds=0CDC 49F3=02 6DDA=00 6DCA=00
amberfolio: seam cheat-wound-party served
amberfolio: watch frame=012666 ds=0CDC 49F3=02 6DDA=00 6DCA=1E
amberfolio: watch frame=012718 ds=0CDC 49F3=02 6DDA=01 6DCA=1E
amberfolio: watch frame=012787 ds=0CDC 49F3=02 6DDA=00 6DCA=00
amberfolio: watch frame=012884 ds=0CDC 49F3=04 6DDA=00 6DCA=00
```

**`6DCA=1E` is the whole point of this leg.** Thirty days, dialled on the
program's own rest clock at frame 12666 — the first time that field has
ever left zero on a driven run. It is the worst survivor's deficit plus
one: the cleric, maximum 30, on one after the wounding and never a target
for a cure because somebody was always worse. And it is on the program's
own rest screen, which is where it stops being a number this seam
computed and starts being a number the game agreed to:

```
REST TIME:  30:05:15
```

The thirty is the seam's; the 5:15 is the memorization time the program's
own wrapper computed, untouched. Then `6DDA` goes up and down between
12718 and 12787 — the rest, ended by the game's own wandering-monster
check — and the party is out of camp at 12884.

**And the exception list, looked at for the first time**, in the box the
fourth point draws on the way out:

```
                 FIX: INTERRUPTED!
HEALED 17 HP WITH 5 SPELLS IN 0:05.
FIGHTER1          1/17      SHORT 16
FIGHTER2          1/18      SHORT 17
...AND 4 MORE.
CURES ARE STILL BEING MEMORIZED.
```

Read it cell by cell, because that is what this leg is for. The three
columns line up — a name, a current-over-maximum, a shortfall — in the
program's own font at the program's own column positions. The list is
**two rows and a tail**, not six, because the box held four rows and the
pending-cures warning owns one of them; the `...and N more.` line is the
proven design's own answer to a list that does not fit, and it is drawn
rather than paged. That is the box as it was read off the screen before
#298 moved the title down a row (below): the box holds three rows now,
so this list is **one row and a tail** — `FIGHTER1` and `...AND 5
MORE.` — and nobody has looked at it since. **The summary's `IN 0:05` is five minutes and not
thirty days**, which is the distinction to hold on to: the clause says
how long the rest *took*, and the game's own watch ended this one five
minutes in. The clause used to be absent here entirely {D} the command had
dialled days, and this seam believed the game kept no day count. It does
(#269), and `docs/seams.md` §10 has where.

Two other things this leg settled by being run:

- **the loop's own end condition.** Five spells is exactly what slot B's
  cleric holds ready, so the casting loop ended because the party ran out
  of cures rather than because the backstop caught it — the first driven
  evidence of that;
- **who the cures go to.** They go to the worst-wounded member the
  program's own healing would accept, recomputed on every arrival, so
  four of the five went to the same fighter (maximum 42, the deepest
  deficit in the party) and one to the thief. The roster panel behind the
  box says so: `1 1 16 3 1 1`.

**The committed camp pair is this run.** `tests/sessions/camp.rec` and
`camp-fix.rec` are both recorded over it — both halves enable
`cheat-wound-party` and pull it at the same tick, so the wounding is part
of the run they have in common and the difference between them is still
one keystroke. That is what put back what #192 cost the pair: it pins the
command *working* again, not merely appearing.

```
  camp-fix     contrast ok  91 of 112 checkpoints identical, then
                            divergent from tick 216799088 to the end
```

**And on the browser's machine**: `drive.mjs` on the same script reports
`encamp-fix fired=11` and `cheat-wound-party fired=1`, which is what the
desktop host reports.

**One thing an earlier version of this leg found that no test could.**
The Fix's first cut had a point with no address, so its guard ran with DS
holding whatever the program had loaded at that instant — and it walked
the party roster before it checked anything cheaper. Driven here, it left
seven `unmapped_memory_read` notices between the ask and the camp screen:
the walk following a far pointer out of a data segment that was not the
program's. Nothing was corrupted; the machine reported, correctly, that
something had touched memory nobody answers for, and the something was
the seam. The points have addresses now and every read still refuses one
outside conventional memory, and the run is back to the three notices
below. That is the cheapest possible instance of this document's whole
point: the suite was green throughout.

### A party the game hurt (#269)

Everything above is a party a **seam** hurt: `cheat-wound-party` writes
the hit point, and what it cannot stand in for is the wound *statuses* a
fight leaves behind. This is the same command over a party the **game**
put down, in a fight the game started, and it is the last half leg 7 was
missing.

**The fight is four squares from where slot B stands, and it is not a
wandering encounter.** Walking south out of `4,3` is met at `4,6` by the
council guard — *"Halt. Your presence is not authorized. Leave."* over a
`YES NO` bar. Answering `N` is a battle, against a garrison of twenty-odd
guards, and slot B's six are outnumbered from the first round. No hours
of play and no wandering: it is six keystrokes past the load.

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

Three of those want explaining.

**`Q` is pressed nine hundred times because `QUICK` is one round, not the
battle.** It hands the party's turn to the computer and comes back to the
tactical bar, so a script that presses it once watches the same prompt for
twenty thousand frames — which reads exactly like a seam that broke, and
did for one afternoon here. The damage tracks presses and not elapsed
time: 88 presses left the party 15 hit points short of full, 310 left it
24, and 910 left it 73 — the last of which counts the maxima the fight's
own experience raised as well as the damage.

**`cheat-kill-all` is pulled to survive and not to win.** It does 120
points to every standing enemy (`docs/seams.md` §10) and touches nothing
on the party's side, so every hit point the party is missing at the camp
screen was taken off it by the game's own combat, by the game's own
rolls. Without it the guards win: this is leg 2's method with a party
worth camping afterwards.

**`E` twice and `N` once is the way out of a won battle**: `EXIT` on the
loot bar, `NO` to *"there is still treasure left, do you want to go back
and claim it"*, and then `ENCAMP` on the party's own bar. Two of those
are the same letter, which is a coincidence and not a trick.

What the fight left, off the loot screen at frame 181,000:

```
FIGHTER1  7    FIGHTER2 10    FIGHTER3 25
THIEF     0    CLERIC   30    MAGIC-USER 12
```

**The THIEF is drawn in red on nothing**, which is the game saying they
went down in the fight. Two others are more than half gone, and the
maxima have moved since the earlier halves of this leg — the experience
from a fight this size levels the front rank, so the deficits below are
against 17, 18, 42, 31, 30 and 19.

Then ENCAMP at 184,021 and the Fix at 186,000:

```
amberfolio: watch frame=184021 ds=0CDC 49F3=02 6DCA=00 6DDA=00
amberfolio: watch frame=187585 ds=0CDC 49F3=02 6DCA=11 6DDA=00
amberfolio: watch frame=187638 ds=0CDC 49F3=02 6DCA=11 6DDA=01
amberfolio: watch frame=196113 ds=0CDC 49F3=02 6DCA=00 6DDA=00
```

`6DCA=11` is **seventeen days**, dialled on the program's own rest clock
after the five cures the cleric held were spent — the worst deficit left
after them was the THIEF's sixteen, plus the day of slack. The rest
screen says so in the program's own hand:

```
REST TIME:  17:05:15
```

and the rest runs to the end without the game interrupting it, which is
the first driven camp here that does.

**And the report, on the pass of the menu after it:**

```
                 FIX: PARTY HEALED
HEALED 73 HP WITH 5 SPELLS IN 17:05:15.
THE PARTY IS AT FULL HIT POINTS.
```

Read the last clause first, because it is #269's other half. **`IN
17:05:15` is a duration the box could not print until now**, and the
number is checkable twice over: it is the rest screen's own `17:05:15`
above, and it is the game's clock going from `14:31` at the camp to
`19:46` at the report, seventeen days apart. `docs/seams.md` §10 has
where the days come from and the two routes that found them.

**And then the finding this leg was not filed to make.** The exception
list is empty and the reason column is not reached: the party comes out
17/18/42/31/30/19, the THIEF included. **A fight's own wound statuses are
inside the healing gate.** Unconscious and dying are two of the four
codes the program's own Cure Wounds applier accepts, and its own rule is
to promote a dying character to unconscious and then revive them — so a
member a fight leaves down is a member this command *mends*, counted in
the 73 hit points and never named. What reaches the reason column is
dead, stoned or gone, and a party that survived a fight is not carrying
those.

So #196's clause is closed and what replaces it is narrower and different
in kind: the report's reason column is still drawn only over rosters the
unit suite writes, and the way to reach it on the program is a **death**
rather than a knockdown. Two deeper runs of this same fight were made
looking for one — 1,150 presses with the pull at 186,000, and the same
again — and both came out with the party whole and nobody dead. The
honest-gaps entry below says so.


---

## Leg 8 — a map of where you have been (M5-E2, #173)

PLAN.md §5 item 3, the second M5 enhancement, and the first seam in this
tree that draws. `docs/seams.md` §10 is what it is and how it works; this
is the driving.

```
--seam code-wheel --seam automap
--press A@7600 --press Return@7650      the code wheel
--press L@8950 --press A@9200           LOAD SAVED GAME, slot A
--press Tab@11000                       the panel
--press Up@11200 ... Right@11350 ...    forty-eight moves, 150 frames apart
--press Tab@19200                       and the party list back
```

Nothing about the first four lines is new; the fifth is the whole leg.
Tab is **not a key this game has**, and that is why it is the one: the
1988 input alphabet has no Tab in it, so a key claimed here cannot also
be one of the program's. With the seam off it reaches the program, which
goes round its command loop again and does nothing with it. With the seam
on it never gets that far — it is taken out of the BIOS keystroke buffer
before the program's own key routine looks — and a panel appears over the
party roster.

**What forty-eight moves through New Phlan look like.** The party starts
at the docks end and walks north and east to the armourer at `8,11`, and
the streets fill in behind it: brown streets in the colour the program
says the 3D view's ground is, **white building fronts because the tiles
those buildings are drawn with are white**, yellow door leaves where the
party has walked past a way in, and an arrow that turns with the party.

Every one of those colours is derived from the game's own data (M5-E2a)
and none is sampled off the screen: the wall colour is the modal
non-black pixel of the tiles the 3D renderer blits for that kind of wall,
which is why the buildings are the colour of the buildings.

**And the band beside the map says where that is** (M5-E2b): `NEW` over
`PHLAN`, wrapped to the eight columns the map's size leaves, centred, in
the yellow the game highlights its own text in — and in the game's own
glyphs, read out of the program's font and rasterized into the panel, so
the label is pixel-identical to the text the game draws around it. The
status row below it — `8,11 E 03:18` — is the *program's*, untouched, and
the panel stops one row short of it deliberately: that row is redrawn as
the clock ticks, and a panel that claimed it would flicker once a minute
for nothing.

**The panel yields to the game, and the game takes it back.** An NPC's
portrait and a message do not disturb it, because they do not touch those
cells — which is right, and is what the roster does too. A character
sheet, an item list or a shop *clears* them first, and the seam hears
that through the program's own clear routines and stops claiming them;
when the program repaints the roster, the panel comes back on the next
poll. Nothing here knows *what* took the screen, only that something did.

**Tab again puts the party list back**, and it is the program that draws
it: the panel wrote over the roster, so the seam clears the panel's own
rect and calls the program's roster drawer (`docs/seams.md` §3's call
door), and the list returns from live state — six names, their armour
class and their hit points. A snapshot of the pixels could not have done
it: the game redraws single roster rows while the panel is up, so a
snapshot is stale the moment somebody takes damage.

**The roster and nothing else**, which M5-E2d had to learn from a
player's screenshot. M5-E2 closed the panel through the program's
per-mode screen *composer*, which repaints the viewport and the status
line as well. Standing in front of a vendor, that painted the 3D view
over the NPC the player was talking to and left the vendor's yes/no
question on the screen with nothing asking it. The panel covers the
roster, so the roster is what it gives back.

**What driving it found**, and no test could: these routines have to be
called at the paragraph they were *linked at*, not at the image base with
the whole offset in IP. Both run the same bytes; the second reaches its
own literals through the wrong CS and puts the roster back drawn out of
somebody else's data. `docs/seams.md` §8.4 has it as a trap, because the
next routine a seam calls will have the same property.

**It steps aside for the game's own conversations** (M5-E2d). The three
drawing points catch everything that takes the *screen*; a vendor's
question takes neither the screen nor the panel's cells, and the panel
sat over the party roster through the whole conversation. So the seam has
a sixth point, at the thunk of the one routine every menu bar in the game
goes up through: the adventuring screen hands it a string out of the data
segment, and every vendor, script and shop hands it a copy built on the
stack. Away from the party's own bar the panel comes down on its own and
Tab is not this seam's key. And while the panel *is* up it takes the two
keys that step the roster cursor, whose whole visible effect is a repaint
of the cells it is sitting on — without that, pressing one made the map
flash for a frame.

**What driving it found, twice.** The first cut of this gated on the byte
the program keeps for "a script still has the message area", which is one
fact and no new point. `--watch 84E4` over the walk below showed it
oscillating on *every step* and sitting down at the bar more often than
up: the panel would have been gone for most of the walk. The driven run
is the only thing that could have said so, and the driven run is also
what says the sixth point is right — at the armourer, the shopkeeper's
portrait is in the viewport, his question is on the message row, and the
party roster is under it with no map on top, without Tab having been
pressed a second time.

**The pair.** `tests/sessions/walk.rec` and `walk-map.rec` are this run
recorded twice, one flag apart, with the Tab in **both** halves at the
same tick:

```
  walk-map  contrast ok  90 of 203 checkpoints identical, then
                         divergent from tick 218787888 to the end
```

Ninety identical checkpoints is the fidelity claim on a real game run:
the seam is on, armed at five addresses and reached over a million times,
and until somebody presses Tab the machine is the machine it would have
been. Tick 218,787,888 is frame 11,003 — the Tab, three frames late,
which is how long it takes the program to poll.

**And it is the same panel in the browser's machine.** Leg 6's method,
applied to this leg: the identical script through `drive.mjs` against the
wasm module, and the two hosts' final frames compared as files.

```
node build/wasm/hosts/web/Debug/drive.mjs <your-directory> START.EXE   --seam code-wheel --seam automap --press A@7600 --press Return@7650   --press L@8950 --press A@9200 --press Tab@11000 <the forty-eight moves>   --until 381818240 --quiet --dump wasmmap
```

```
amberfolio: seam automap armed fired=1283132
amberfolio: throughput virtual=320.000s wall=33.6s factor=9.52x
            steps=95454560 steps/s=2839333
```

The same 95,454,560 steps as the desktop run, and `cmp` on the two `.ppm`
files says nothing: every one of the 64,000 pixels agrees, panel
included. That is #173's "on both hosts", made as a comparison of two
files rather than as two people's descriptions of two screens.

**The doors here are the fallback, and it is worth knowing which.** A
door leaf is drawn when a wall face's *kind* is known to be a door, and
what knows that is a shut instance of the same kind — this map's own, or
the table of every shut face in the shipped data (`docs/seams.md` §10).
Twenty-one of the twenty-nine shipped grids carry one. **New Phlan does
not**, and its wall set is not in the table, so the city falls through to
the older rule: a passable face is a door. So this leg drives the
*picture* and the fallback, and the evidence path is driven by **leg 12**
instead, in the Kobold Caves, where the party's own map names a door kind
and the trace says so leaf by leaf (#268).

**And what it walked outlives the machine** (M5-E2c). What the panel has
explored is observation and not machine state, so it is gone when the
machine stops — which is right for fidelity and no use to a player. So a
host writes it beside the save, and the driving is a second run:

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

Two files afterwards, both this project's own and neither of them inside
one of the program's: `\SAVE\AFMAP.DAT`, the working table, and
`\SAVE\AFMAPA.DAT`, the snapshot that belongs to slot A. Take the
working table away, so only the snapshot can supply anything, and load
slot A again:

```
amberfolio: automap-store reads=1 slot=A
```

The panel comes up with the streets already on it, and the same script
through `drive.mjs` against the wasm module produces a final frame that
is byte for byte the desktop host's — the same object writing the same
file on both hosts, and both reading it back.

**The store is off unless it is asked for**, and that is a decision
rather than caution. This is a real directory of the player's; and every
session in `tests/sessions` pins its disk by name, size and SHA-256, so a
sidecar written by a verification run would make the next run's disk a
different disk. The session pair below runs with the seam on and the
store off, which is why the fidelity numbers are unchanged by any of it.

**What the driving found here**, and no test would have: the load menu
opens *every* save file in the directory in turn to find out which slots
exist. A store acting on the naming call alone loads nine slots' maps for
one load and leaves the player looking at the last one in the directory.
`file_event` now says whether bytes actually moved through the handle,
which is what the proven design's own file layer counted, and
`docs/machine.md`'s file-event section has it.

**What this leg does not cover** is written down in the honest-gaps list
below.

---

## Presenting a document (M5-D3, #171)

PLAN.md §5 gates two of the enhancements on a document the player holds —
the code-wheel bypass on the code wheel, the journal on the journal — and
the rule is exact: a possession gate, which demonstrates the player holds
the document and no more. The presenting side is the same flag on both
hosts:

```sh
amberfolio <dir> START.EXE --document "/path/to/code wheel.pdf"
node build/wasm/hosts/web/Release/drive.mjs <dir> START.EXE   --document "/path/to/code wheel.pdf"
```

```
amberfolio: document Pool of Radiance code wheel, archive release (PDF) (code wheel) sha256=0db301ae...
```

The file is read, hashed and dropped. Nothing is parsed, nothing is
kept, and nothing from it enters this tree — the fingerprint in
`machine/document.h`'s table is a fact about a file and carries no byte
of it (CONTRIBUTING.md).

A document this build does not know is **reported, not guessed**, with
the fingerprint of the file on the line, because that is the thing
somebody can act on — an entry in the table is made of it. A gate that
armed on an unrecognized document would be a gate that armed on anything.

**Nothing in this build is gated yet**, so nothing here changes what a
leg does. `--seams` says what each seam needs (`no document`, for all of
them today), and the code-wheel seam's gate is one field away: #115 turns
it on.

---

## Leg 9 — an entry of your own journal, in the game (M5-E4, #175)

PLAN.md §5 item 2's in-game half, and the second seam in this tree that
draws. `docs/seams.md` §10 is what it is; `docs/journal.md` is where its
text comes from; this is the driving.

**It needs a store, and this is the one leg that can be driven without a
journal.** The reader is answered out of `journal.txt` — whatever
`--journal` wrote there, or whatever a person put there by hand in the
format `docs/journal.md` §6 sets out. What was driven *here* is a file of
this project's own sentences under an entry number: enough to prove the
reader, the key, the callout and the give-back, and deliberately not the
recognizer against real prose.

A real ingestion and a real citation came later and are elsewhere:
`docs/seams.md` §10's "What a real citation did" (#232) is the game's own
words opening a player's own ninety-nine entries, and
`tests/sessions/cite.rec` is that run as a session, over a store this
tree pins by digest and does not carry.

```
--seam code-wheel --seam journal --journal-store ./journal.txt
--press A@7601 --press Return@7651      the code wheel
--press L@8951 --press A@9201           LOAD SAVED GAME, slot A
--press F1@10600                        the reader
--press 3@10700 --press Return@10800    the entry to open
--press F1@11200                        and the party list back
```

The store is read before the run and said so, in the same words an
ingestion prints:

```
amberfolio: journal store ./journal.txt entries=1 corrections=0
```

**F1 is not a key this game can mean**, and that is a stronger version of
Leg 8's argument for Tab rather than the same one. A function key has no
character at all — `keyboard.h` answers AL=0 for the whole F1–F10 row —
and this program picks its commands off its bars *by character*, so a key
with none cannot be a command on any of them. F11 and F12 never reach the
machine at all; they are the SDL host's own (`docs/hosts.md` §3).

**Then the entry is on the game's screen**, in the game's own lettering,
wrapped to twenty-two columns inside the frame the game drew, with
`ENTRY 3` in the yellow the program highlights with and the body in the
green it writes messages in. The callout says it was served:

```
amberfolio: seam journal armed fired=602659
amberfolio: host-service journal-open calls=1 last=3 at=214790400
```

**F1 again puts the party list back**, and it is the program that draws
it — the panel's rect through the program's own region clear, then its
own roster drawer, from live state. The 3D view is untouched, which is
the M5-E2d property this seam inherited by asking for the roster drawer
rather than the screen composer.

**Run it with `--seam automap` as well** and the modal rule is a picture
rather than a paragraph: Tab's map, the entry drawn over it, and — one F1
later — the map back, with New Phlan's label and the party's square where
they were. The two panels are the same pixels and the reader is the thing
on them while it is up.

**And it is the same reader in the browser's machine.** Leg 6's method
again, and `tools/drive.mjs` takes `--journal-store` in the desktop
host's own spelling for exactly this:

```
node build/wasm/hosts/web/Debug/drive.mjs <your-directory> START.EXE \
  --seam code-wheel --seam journal --journal-store ./journal.txt \
  --press A@7601 --press Return@7651 --press L@8951 --press A@9201 \
  --press F1@10600 --press 3@10700 --press Return@10800 \
  --until 240000000 --quiet --dump wasmj
```

```
amberfolio: journal store ./journal.txt entries=1 corrections=0
amberfolio: seam journal armed fired=602779
amberfolio: host-service journal-open calls=1 last=3 at=214772808
```

`cmp` on the two `.ppm` files says nothing: **every pixel of the entry
agrees**, which is #175's "on both hosts" as a comparison of two files.
The callout's tick differs by 17,592 — under one frame — because the two
hosts post a `--press` at slightly different points inside a frame; the
machines converge, which is what the identical final frame says.

**What this leg does not drive is the watch.** The keys above open the
reader at the prompt, which is the half a person asks for; the other half
is the game citing something with nobody having pressed anything, and no
key sequence can make that happen here — it wants a square where the game
cites, and a journal to answer with.

That was driven later, on a player's own ingested journal, and it is
`docs/seams.md` §10's "What a real citation did" (#232). It is worth
reading beside this leg for one reason: an earlier version of this
section drove the watch by *building it wrong on purpose* — the pattern's
word set to a letter the position line puts in front of the clock — and
reported the resulting callout as the citation path proven. It was not.
The position line does not go through the routine the game's narration
goes through, and the probe proved the point was reachable rather than
that it was the right point. A probe that reaches a routine says nothing
about whether that routine sees the thing you are watching for.

---

## Leg 10 — the journal's own log (M5-E4a #221, M5-E4b #222, #230)

The other half of the reader, and the half a person reaches on purpose:
**`Notes`** on the party's own command bar opens a full-screen log of
everything the game has cited, newest first, with a `*` on what has not
been read.

Leg 9's prefix, and then the log with nothing in it:

```
--seam code-wheel --seam journal --journal-store ./journal.txt
--document "<the code wheel>"
--press A@7601 --press Return@7651 --press L@8951 --press A@9201
--press N@10600 --press Escape@11400
```

The screen it draws is the game's own frame with
`ADVENTURER'S JOURNAL` at the top, one sentence — the game has not sent
you anywhere yet — and `EXIT` on the bottom row. A log with something in
it wants a citation, which is `tests/sessions/cite.rec`'s run: the city
hall's four proclamations, in the order the game said them, with the `*`
on the three not read and the cursor row in the program's highlight
colour.

**The three things worth pressing while it is up**, because each was a
bug once:

- `S`, `C`, `L` and the arrows. The program's own command bar is what
  answers those, and until #230 it was still running underneath: `E`
  encamped, arrows walked, and the program then repainted its bar over
  the journal a piece at a time. Nothing reaches it now, and the
  measurement is in `tests/visual/not-log-modal.leg` — one digest across
  1,475 frames while all six are pressed.
- `E`, which is the word on the log's own bottom row and is how this game
  leaves every screen it has.
- **Escape**, which is what a *script* should use instead. `E` on the
  party's own bar is ENCAMP, so a run that presses it with the seam off
  ends up somewhere else entirely — which is why the confinement legs and
  `notes.rec` all leave by Escape.

**What comes back is everything except one thing.** The frame after the
log closes is the frame before it opened — viewport, roster, status line,
ornaments, every pixel — except *which command the bar's highlight is
sitting on*: the give-back injects a space to make the menu-bar routine
draw itself again, and it draws itself with the highlight at its first
command. 951 pixels, all on the bar row
(`tests/visual/not-log-giveback.leg`). A player who had stepped the
highlight along finds it back at the start.

`N` is ignored while a page is up, deliberately — the way out of the
reader is the way out of the reader — so a script that wants the log
after a citation closes the panel first.

---

## Leg 11 — the overworld, and where you have been (M5-E5, #179; the fog M5-E5f, #263)

PLAN.md §5 item 5, and the third seam in this tree that draws.
`docs/seams.md` §10 is what it is and
[`docs/explored-overlay.md`](explored-overlay.md) is the fact table it was
built from; this is the driving.

**The screen is the wilderness travel view**, which nothing above ever
reaches: a five-by-five window of a wilderness area's overhead map,
scrolling with the party. It is not the 3D screen Legs 1 to 10 are on and
it is not the automap's grid.

**Getting there costs four keystrokes.** Slot **J** of the edition's own
shipped saves has a party already standing on one of the three wilderness
areas, so there is no walk out of a city and no hours of play:

```
--seam code-wheel --seam explored --document "<the code wheel>"
--press A@7601 --press Return@7651      the code wheel
--press L@8951 --press J@9201           LOAD SAVED GAME, slot J
--press Up@10600 ... every 150 frames   eight steps north
```

The mode byte becomes 3 at frame **9,552** and the screen has settled by
**10,275**; `--watch 49F3:1 --watch 49FA:1` is how that is seen without
looking at a picture, filtered on the data segment (`0CDC`). Moves are 150
frames apart, the cadence every driven walk here uses. Wandering brings
an encounter within a few virtual minutes, so a quiet walk is a short
one.

**What it does.** It is **fog of war** (M5-E5f, #263). The square the
party is standing on and the eight around it are the game's own map,
untouched; every other square of the window is hazed over with a
**one-pixel checkerboard of dark grey** — palette index 8, on half the
square's pixels, with the program's own pixel on the other half — so the
terrain is faintly there under the fog rather than gone. The fog lifts as
the party goes, and the square under the party is never covered because
the party's icon is drawn there.

**The colour was chosen by looking at it.** The first fog was solid
black, and five coverings were then composited over one real dumped
frame — solid black, a black checker, a dark-grey checker, a light-grey
checker and a two-by-two dark-grey one — and the maintainer picked the
dark-grey checker. A solid cover throws away the shape of the country the
party is standing at the edge of; a black checker collapses against the
grass's own two-green dither into a flat mesh; light grey reads as paler
ground. `docs/explored-overlay.md` §5.2 has all five, black's four
reasons included, because it is the rejected alternative.

**How far it sees is one constant**, `explored_reveal_radius` in
`machine/automap.h`, and it is 1. Two and three were asked for and cover
nothing: the window is five squares across with the party in the middle,
so every square on the screen is already within two. Driven, with the
constant set to 2: **523 dumped frames of this walk, and not one of them
differs from the same walk with the seam off.** The maintainer confirmed
one on the same look that chose the colour, so the radius is settled and
not a placeholder.

**The first design was the other way round**, and this leg used to
describe it: the whole map on the screen with the walked squares one
shade brighter. It was measured visible on 2,800 squares and it did not
read when somebody looked at it, which is the exit rule this item has for
exactly that reason (PLAN.md §5 item 5). `docs/explored-overlay.md` §5
keeps both.

**Nothing is pressed.** It is a setting, not a command: on, it is there
whenever that screen is; off, it is not.

**What the driving says.** 523 stills against the same run with the seam
off: **411 byte for byte identical, and every one of the 112 that differ
differing only inside the squares the party has not been near** — sixteen
of them until the first step and thirteen from then on, and **not one
pixel outside the window at any frame**. On the last of them, 3,166
pixels differ from the seam-off frame, every one of them palette index 8,
and not one pixel of the checker's other half — the program's own — has
moved anywhere on the screen. From the frame the arrival
settles at to the end of the run every still has the fog on it, and the
fifty-one after the last step are one still repeated
(`tests/visual/exp-trail.leg` and `exp-steady.leg` are those two as
assertions). `tests/sessions/wild.rec` and `wild-trail.rec` are the run
recorded twice, one flag apart: **107 of 140 checkpoints identical, then
divergent from tick 204,866,288**, which is the arrival — under the lift
it was 111 and the first step, and that difference is the enhancement's
whole change of mind.

**And one claim went away with it.** Arriving on a wilderness map nobody
has walked is no longer pixel-identical to the seam-off run, because a
fog covers what is *not* known and a fresh map is nearly all of that. The
weaker claim stands and is a session: on, with the overworld never shown,
`quiet-explored.rec` is all 126 checkpoints of the baseline.

**Keeping the trail.** `--automap-store` writes it into `\SAVE\AFMAP.DAT`
beside the game's saves, with a snapshot per save slot. Two things about
it were driven and are worth knowing:

* the **automap alone**, with this seam off, records the wilderness too —
  one recorder, two callers (#254) — so a player who turns this on later
  finds the squares they walked already clear;
* loading a slot that has **no** snapshot beside it empties the table, so
  the arrival is the arrival: the party's own three-by-three and fog
  everywhere else. That is the store's own rule and not a fault — an
  empty map is the truth about a playthrough nobody recorded one for —
  and under the lift the same sentence ended "so the arrival is byte for
  byte the seam-off one", which is the claim the fog gave up.

---

## Leg 12 — the automap's door rule, on a map that has shut doors (#268)

Leg 8 drives the panel through New Phlan and nowhere else, and the
sentence above admits what follows: the city has no shut face anywhere on
it and its wall set is not in the seam's table, so every leaf on that
leg's panel was drawn by the oldest rule of the four — *a passable face
is a door* — and the evidence path had never run at all. This leg is the
map that has one.

**Not Kovel Mansion.** #199 named it because it carries forty-five shut
faces, and routing there is a walk across a district. It was not needed:
the shipped `SAVE` directory's **slot C** stands in the **Kobold Caves**
(disk 8, area 13), which carries shut faces of its own, so the leg is the
same four lines every other leg starts with and a short walk.

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

Moves are 150 frames apart, as everywhere else. Slot C's party is deep
enough that nothing wandered into this walk; if something does,
`--seam cheat-kill-all` and a `--pull cheat-kill-all` at the fight is
leg 6's method and clears it.

**What `--trace` says, which is the point of the leg.** All four rules
draw the same two yellow pixels, so a still of a leaf cannot say which
one put it there. Since #268 the trace prints the evidence instead — the
two masks, and a count of the leaves on the panel by the rule that drew
each:

```
amberfolio: automap doors frame=011000 disk=8 area=0D geo=0D seen=0200 table=0200 drawn shut=0 kind-seen=1 kind-table=0 no-evidence=0
amberfolio: automap doors frame=011885 ... drawn shut=0 kind-seen=2 kind-table=0 no-evidence=0
amberfolio: automap doors frame=012647 ... drawn shut=0 kind-seen=3 kind-table=0 no-evidence=0
```

`seen=0200` is the line that had never been printed before: **bit 9, from
this map's own shut faces**, scanned when the party arrived. Every leaf
the panel draws on this walk is `kind-seen` — a way through whose *kind*
was seen shut somewhere on this grid — and `no-evidence=0` says the city
leg's fallback drew nothing here. The rule has run.

**The still.** Twelve virtual seconds after the last move the panel is a
five-cell corridor along row 3 and a two-cell passage north up column 0,
under the label `KOBOLD CAVES` in the game's own font, the caves' walls
in the caves' own red and the floor in its brown. Three leaves: one on
the **east face of `3,3`**, the cell the party started on, and one on
each side of the border between `0,1` and `0,0` — the door the party
walked north through at frame 12,558, which is why the third line above
appears when it does. The party arrow is on `0,0` and the 3D view beside
it is the wall the party is now facing.

**What this leg does *not* show, said plainly.** It shows a leaf drawn by
the rule; it does not show a leaf the rule drew *and the table would not
have*. The table cannot disagree here, and on this data it can hardly
ever: it was built by sweeping every shut face in the shipped areas, so
the kinds one map's own scan can find are kinds the table already knows,
wherever the wall-set slot names a block it can be looked up by. `seen`
and `table` are both `0200` on this map, which is #199's offline
cross-check of the two disk-8 rows now made by a running machine instead.
The one case where the two really can differ — a slot filled from a
multi-block load, where the table cannot be consulted and the map's own
scan still works — is a unit test and not a run, and there is no known
save that stands on one.

**The counters are not machine state.** They are counted while the panel
is drawn, live beside the exploration table in `machine::automap()`
(`automap.h`), are never serialized, and are read only by a host that was
asked for `--trace`. The panel's pixels are what they were before anybody
counted them.

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

Honest gaps, so nobody reads more into a green run than is there.

**This list was M4's closeout input and is M5's** (#109, #178): each
milestone was closed with every line below still standing, deliberately,
because a milestone closed on a list nobody wrote down is a milestone
whose gaps get rediscovered. The two marked *by decision* are closed and
nobody is coming. The rest have homes, and at M5's closeout they were
given fresher ones: the person's items are #274 (successor to #147 and
#148, which had nothing else left in them), the standing inventory of
what this machine refuses is #275 (successor to #166, which is closed),
and each seam's own residual is its own issue — #267 for the fog, #268
for the automap, #269 for the Encamp Fix, #236 and #270 for the journal,
and #273 for legs 3 to 5 on the web — which is closed, and its line
below says what the walk through it turned up.

Two of them are **decisions** rather than a worklist — nobody is coming,
and the entries stay because a procedure that quietly stopped mentioning
what it skips would be worth less than one that says so.

- **Two of the city services**, by decision (#104, #145). Legs 4 and 5
  buy, heal and sell, and the city hall, the arena, the dueling rooms
  and the training schools' lobby all render and answer. **Training** has
  not been transacted — no character with the experience for a level has
  been taken through it, though the shipped slot A has one — and the
  **inn** has not been found. Three services checked to the coin was
  judged enough: the check is about the transaction path, and these two
  reuse it with different arithmetic on the far side. If training is ever
  driven it is the interesting one, because it *writes a character
  record* and so is closer to leg 3's save path than to a purchase.
- **The dungeon**, by decision (#102, #144). Everything above is the city
  and its slums. The gate out of the civilised district is at `0,4`, leg
  5's routing method works on the other side of it, and nobody has walked
  through. What a dungeon exercises is the same 3D view, the same ECL
  events and the same movement code the city already runs over different
  data — and the map edge itself, which *was* a mechanism, is covered.
- **The Encamp Fix's report, on a member no cure can reach** (#269).
  **The gap that is gone** is the one that used to be here: a party hurt
  by the game rather than by a cheat. Leg 7's fourth half fights the
  council guard with slot B, survives it, and camps {D} the party comes to
  the camp screen with four members down and the thief on nothing, all of
  it taken off them by the game's own combat, and the Fix puts every one
  of them right. What that leg found is why this entry is narrower rather
  than closed. **A fight's wound statuses are inside the healing gate**:
  unconscious and dying are two of the four codes the program's own cure
  applier accepts, so a member a fight leaves down is one the command
  mends and never names. The report's reason column is reached by dead,
  stoned and gone, and a party that survived its fight carries none of
  them {D} so that column is still drawn only over rosters the unit suite
  writes. Closing it wants a **death** on a party that lives, which two
  deeper runs of the same fight did not produce, and it is a question
  whether it is worth the hours: what the column prints is a pointer into
  the program's own status table, so what a driven run would add is that
  the pointer arithmetic is right rather than that the seam is.
- **The automap, off the city streets** (#173). Leg 8 drives it through
  New Phlan and nowhere else, and two things follow from that. The first
  is the part the store was built for: a **map change**, where the
  party's position words hold the old cell until the program's arrival
  script places it and the seam has to wait for the position to stop
  moving before it believes it. The settling rule and the marks it plants
  have unit tests over positions a test hands them; nobody has walked
  through the gate at `0,4` with the panel up and watched the panel
  change maps. The second **was** door detection from evidence, and
  leg 12 closed it (#268): New Phlan has no shut face anywhere on it, so
  leg 8 exercises the oldest of the four rules and nothing else, and it
  took a walk in the Kobold Caves off slot C to run the evidence path on
  a machine. What is left of it is narrower and is said in leg 12: no run
  has ever drawn a leaf the map's own scan found and the shipped table
  did not, because the table was swept from the same shut faces — that
  case needs a wall-set slot filled from a multi-block load, which is a
  unit test and no save stands on one. The third is the **zone label**:
  one of the twenty-nine names has been seen drawn — two of them now, and
  the second is leg 12's `KOBOLD CAVES` — and the wrap's other two shapes
  — a name broken at its soft break, and the `AREA <n>` a map with no row
  falls back to — are unit tests over a font a test hands the seam and
  have never been on a screen. The fourth is the **sidecar's second
  slot**: the driven runs write and read back slot A, and two
  playthroughs in two slots not sharing one map is a unit test over file
  events a test hands the store, not a run.
- **The explored overlay's two driven gaps are closed** (#267 items 2
  and 3), and what they cost is worth keeping. Leg 11 drives the area
  slot J starts on, which is view kind 2 on disk 6; the other two have
  now been stood on as well, off a save edited on a scratch copy of the
  disk by the offset table in `docs/explored-overlay.md` §8 — which had to
  be **corrected first**, because the recipe published there named a
  data-segment global with no save field behind it and an area id that
  moves nobody. The column bias needed no change: it is read out of the
  program, and the program's own status line prints it back, reading
  `16, 32` on kind 3 and `29, 32` on kind 4 where the record holds column
  3. On both areas the arrival, and a twelve-keystroke walk from a rock
  square, keep every difference from the seam-off run inside the window's
  own `8,8,127,127`.

  The fog has also now been over **mountain rock** — the terrain whose own
  art is largely the fog's own palette index 8, and the case the five
  composites never showed. It reads: the tile's bright half goes to a
  one-pixel mesh and the cell edge between covered and clear is a
  straight cut the eye follows. What it costs is that **a third of the
  covering is invisible** there, 1,526 of 4,608 pixels writing grey onto
  grey, so the haze is thinner over rock than over anything driven before
  it. Forest and roads are still not under it, and no shipped tile set
  has been read directly for a tile drawn *entirely* in that grey, which
  would carry nothing and say nothing (#267 item 4).
- **Whether the fog reads as something the game drew *in play*** (#263).
  Narrower than it was. The lift's version of this clause was answered by
  a person and the answer changed the enhancement; the fog's colour was
  then chosen by the same person off five coverings composited over a
  real frame. What is left is that a composite is a picture beside
  another picture — nobody has walked a wilderness map with the haze
  moving in front of them. That is #179's last unticked clause.
- **The dev page itself** (#108). Leg 6 drives the wasm module headless
  and the module is the same one the page loads, but nobody has run any
  of this in a browser: the canvas, the AudioWorklet, the seam
  checkboxes and the directory drop are checked by a node harness and by
  reading, not by looking. `docs/hosts.md` §3 is still where a person
  closes that.
- **Legs 3 to 5 on the web — done, and it found something** (#273). All
  three were driven on the wasm module against a player's copy from the
  same scripts the desktop host was given, and every artefact agrees:
  identical stop lines, identical final stills, identical `.edges`, and
  identical SHA-256s for the four files leg 3's save writes. Leg 6's
  "Legs 3, 4 and 5, on both hosts, compared" has the table. Two things
  had to change for it and both were the driver's rather than the
  machine's: `--press KEY@FRAME` named a different tick on each host, and
  a directory-walking host could not carry the empty `\SAVE\` a fresh
  installation has. What is left of this line is narrow and named
  elsewhere: the sale was driven at the armourer rather than at the
  general store at 12,10 — same bar, same command, shorter route — and
  nobody has driven leg 5's `--watch` half on the web, because `--watch`
  is the desktop host's flag and no web equivalent has been asked for.
- **Audio beyond "there was one."** The first sound this program makes is
  in combat. `docs/hosts.md` §4 now measures the speaker — the edge list
  a run published, the box filter's DC offset, the two hosts' sample
  rates against each other — but every one of those numbers comes from a
  program in `tests/programs`. Nothing has measured *this* program, and
  §3 is still where a person checks that a pressure wave left a speaker
  (#106).

  What *is* ready for that, since the closeout, is the instrument: all
  three writers — the SDL host's `--dump`, `tools/drive.mjs --dump` and
  the host-free `amberfolio-dump` — now write the same `.edges` file, so
  running any leg above with `--dump` and reading the divisors out of it
  answers "which tones did this program actually program" without
  anybody listening to anything (#148). It is one command on top of a leg
  that already works.
