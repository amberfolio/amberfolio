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
at the copy-protection challenge for ever and never reaches a fight.

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

**What is still unmeasured** is `debug_damage` itself: whether 120
finishes what a party actually meets. The wilderness encounter above is
seven soldiers; if one pull leaves any of them standing, that is the
number being wrong rather than the seam.

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
program's colours, and a title the program's own frame routine centred:

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

That whole picture is three calls into the program and not one glyph
drawn here. This machine has a font and it is deliberately not the game's
(`docs/seams.md` §3), so a seam that rasterized its own lettering would
have put visibly foreign text beside the game's own, on the game's own
screen — which is the failure #186 was filed about, one layer down.

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
HEALED 17 HP WITH 5 SPELLS.
FIGHTER1          1/17      SHORT 16
FIGHTER2          1/18      SHORT 17
...AND 4 MORE.
CURES ARE STILL BEING MEMORIZED.
```

Read it cell by cell, because that is what this leg is for. The three
columns line up — a name, a current-over-maximum, a shortfall — in the
program's own font at the program's own column positions. The list is
**two rows and a tail**, not six, because the box holds four rows and the
pending-cures warning owns one of them; the `...and N more.` line is the
proven design's own answer to a list that does not fit, and it is drawn
rather than paged. The summary has no time clause, which is correct and
deliberate: the command dialled days, and the game's clock is an hour and
two minute digits with no day counter, so a number there would be a wrap
printed as an answer.

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
*picture* and the fallback, and the evidence path is exercised by the
unit suite and by no driven run yet — Kovel Mansion, on the other side of
this same district, has forty-five shut faces waiting for one.

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
format `docs/journal.md` §6 sets out. Nobody has an ingested edition
(§3 of that document), so what was driven here was a file of this
project's own sentences under an entry number: enough to prove the
reader, the key, the callout and the give-back, and not the recognizer
against real prose.

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

**This list is M4's closeout input and it was taken as such** (#109): the
milestone was closed with every line below still standing, deliberately,
because a milestone closed on a list nobody wrote down is a milestone
whose gaps get rediscovered. The two marked *by decision* are closed and
nobody is coming; the rest live on in #147, #148 and — as the standing
inventory of what this machine does and does not do — #166.

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
- **A party hurt by the game rather than by a cheat** (#196). Leg 7's
  third half closes the days arithmetic and the exception list, and it
  does it with `cheat-wound-party` rather than with a fight: the party is
  put on one hit point by a debug seam, not by anything in the game. What
  that leaves open is narrow and worth naming — nobody has driven a party
  hurt by **combat** and then camped, so the one thing this substitution
  cannot speak for is a roster carrying the wound *statuses* a fight
  leaves behind (unconscious, dying, and the conditions resting cannot
  mend). The report's reason column for those is drawn by tests over
  rosters the unit suite writes, and by nothing else. The way to close it
  is still the way #196 named: a leg that fights, survives and camps.
- **The automap, off the city streets** (#173). Leg 8 drives it through
  New Phlan and nowhere else, and two things follow from that. The first
  is the part the store was built for: a **map change**, where the
  party's position words hold the old cell until the program's arrival
  script places it and the seam has to wait for the position to stop
  moving before it believes it. The settling rule and the marks it plants
  have unit tests over positions a test hands them; nobody has walked
  through the gate at `0,4` with the panel up and watched the panel
  change maps. The second is **door detection from evidence**: New Phlan
  has no shut face anywhere on it, so the driven run exercises the
  fallback and not the rule. Both close the same way, and the dungeon gap
  above is the same door. The third is the **zone label**: one of the
  twenty-nine names has been seen drawn, and the wrap's other two shapes
  — a name broken at its soft break, and the `AREA <n>` a map with no row
  falls back to — are unit tests over a font a test hands the seam and
  have never been on a screen. The fourth is the **sidecar's second
  slot**: the driven runs write and read back slot A, and two
  playthroughs in two slots not sharing one map is a unit test over file
  events a test hands the store, not a run.
- **The dev page itself** (#108). Leg 6 drives the wasm module headless
  and the module is the same one the page loads, but nobody has run any
  of this in a browser: the canvas, the AudioWorklet, the seam
  checkboxes and the directory drop are checked by a node harness and by
  reading, not by looking. `docs/hosts.md` §3 is still where a person
  closes that.
- **Legs 3 to 5 on the web.** The door that blocked them is open (#146,
  leg 6's last section): a path goes into the VFS, the directories on the
  way are made in core, and both web hosts hand over what is below the
  root. Nobody has driven those three legs against a player's copy on the
  wasm module and diffed them against the desktop runs above, which is
  what would make them legs rather than a mechanism.
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
