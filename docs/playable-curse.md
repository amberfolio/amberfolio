# Playable: Curse of the Azure Bonds

How to drive a player-supplied copy of Curse of the Azure Bonds from the
party menu into the game, leg by leg: the keystrokes, what the run shows,
and the traps. `docs/playable.md` is the same for Pool of Radiance and
has the method in full; `docs/first-light.md` stops at the party menu.
Nothing in this repository can run any of it (PLAN.md §6).

---

## The method

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/<preset>/hosts/sdl/<config>/amberfolio <your-copy> START.EXE \
    --fast max --speed 386 --wall none --no-config \
    --press KEY@FRAME ... \
    --until <ticks> --dump run --dump-every 60
```

No `--install`: a Steam copy is recognized and laid out at `\CURSE`, a
GOG copy at the root (`docs/hosts.md` §2c). No seam is needed to play,
and none but the debug cheats is available on this program. Sixty
frames to the virtual second; `TICKS = FRAME * 19,887` at `--speed 386`
is close enough for `--until`.

Every script below is on a fresh copy of the Steam disk with its shipped
slots (the `curse-slots` snapshot), keys **150 frames apart** from frame
2,500. Snapshot the directory and restore it between runs.

Rules:

- **The challenge takes any letter.** `P` at the last title screen
  chooses to play, then any letter and `Return` answer the copy
  protection: `P@2500 A@2650 Return@2800`, and the party menu is up by
  2,900.
- **A sign or a message stops the walk** until `Return` answers it, and
  every key before that answer is flushed. `Return` at the adventuring
  bar takes `AREA`, which toggles the overhead map; a walk goes on
  through it, so a `Return` after a step that might have hit a sign costs
  nothing.
- Movement is the arrow keys: `Up` steps, `Left` and `Right` turn,
  `Down` turns round.
- The `.txt` and `.scene.json` beside a still say `wrong_binary`: screen
  text and the scene have no facts for this program yet (#555).

---

## Leg 0 — a party

**Not driven yet.** `CREATE NEW CHARACTER` is on the party menu; `ADD`
lists every `*.GUY` in the save directory (`docs/hosts.md` §6), and the
shipped disk has none. Owed: a character created, kept, added and taken
into the game, which is also what writes the first `.GUY`.

## Leg 1 — loading A and B

```
P@2500 A@2650 Return@2800 L@2950 A@3100 B@3250
```

`LOAD SAVED GAME` asks `A` or `B` (the slots whose `SAVGAM<S>.DAT` is
there) and brings back the party menu with the party listed;
`BEGIN ADVENTURING` starts it. The two shipped slots are the same
moment of the story with different parties, all six of each unhurt and
with no spell memorized:

| slot | party |
| --- | --- |
| A | Mathew and Mark (paladins), Travis (fighter/thief), Ledera (fighter/mage), Shara (cleric), Philippe (mage); 27 to 51 hit points |
| B | Demeltina (paladin), Argora and Rwellyn (rangers), Florentz (cleric), Oratisi Nomoon (fighter/mage/thief), Brytwyn (mage); 20 to 72 hit points |

Both stand at `7,13 E` in a small room at `00:00`, and the opening event
runs at once: two screens that `Return` answers. Recorded as
`curse-quiet` (slot A).

## Leg 2 — the town and a story event

From the opening, `Return Return Left` (facing a wall north), then
`Left Up` steps west into `6,13` and the innkeeper's event: two screens,
the second citing journal entry 31. `Right Up` is the street at `6,12 N`
and a sign. `Left Up Up Up Return Up` walks west along the street to the
weapon shop's sign at `3,12` and into the shop at `2,12`; `Y` takes the
shopkeeper's offer and the bar is `BUY VIEW POOL APPRAISE EXIT`; `B`
lists the stock with `BUY NEXT EXIT`. Recorded as `curse-town`.

The street runs north and south along column 4. **The temple**: from
`6,12 N`, `Left Up Up Right Up Up Up Up Up` to the Temple of Gond's
sign at `4,7` (`Return`), then `Left Up` through its door to an acolyte
at `3,7` who points the party at the altar (`Return`), and `Up Up Up`
west to `0,7`, where the bar is `HEAL VIEW POOL APPRAISE EXIT`.
Recorded as `curse-temple`. Walking west at `1,14`, a
group of royal guards turns the party back (the way is closed until the
royal carriage comes), every time.

## Leg 3 — a fight

The tavern at `7,10`: from the street at `6,12 N`, `Left Up Up Right Up
Up Right Up Return Up Return Up` (signs at `5,10` and `7,10`), and the
bar's `PUNCH BARKEEP` is the first choice: `Return` starts a brawl with
ten patrons. In the round, `Escape` leaves the move cursor and `Q` puts
a member on QUICK; six of each puts the party on QUICK. The program asks
whether to continue the battle when more than one of the party is still
standing and no patron is; `N`, `Return` past the reward, and `E` out of
the treasure. Recorded as `curse-fight`: the party wins, one member
down, 70 experience points each.

`--watch 6FAA:2` is the two sides' counts, a byte each, the party's
first.

## Leg 4 — the cheats

| seam | how | what the run shows |
| --- | --- | --- |
| `cheat-invulnerable` | on | through the quick-fought brawl, fires on every hit and no member loses a hit point (`curse-fight-cheat`) |
| `cheat-kill-all` | on, `--pull cheat-kill-all@FRAME` once the brawl has begun | the patrons' count to zero at the pull, `fired=1`; the downed patrons stay drawn until the round ends, then the program's continue-the-battle question (`curse-fight-kill`) |
| `cheat-wound-party` | on, pulled at camp | every member on one hit point, shown when the camp redraws (`curse-camp-wound`) |

`docs/seams.md` §10 has each program's facts. **A pull is an input**, and
a frame that carries an input is checkpointed, so a pulled half put
beside a baseline that pulls nothing pulls on a frame the baseline
presses a key on, or the two checkpoint at different ticks:
`curse-camp-wound` pulls at `V`'s frame, `curse-fight-kill` at the
fight's first `Escape`.

## Leg 5 — a camp, and the game's own Fix

`E` on the street encamps: the bar is `SAVE VIEW MAGIC REST ALTER FIX
EXIT`. `R` opens the rest time (`DAYS HOURS MINS ADD SUBTRACT EXIT`,
`D A` adds a day, `R` rests). **In town royal guards end any rest at
once and tell the party to move along**, `FIX` included, so the Fix leg
is owed somewhere they do not walk, with a cleric's cures memorized
first (`MAGIC`); the shipped parties have none. Recorded: `curse-camp`
(`E`, `VIEW`, out) and its wounded twin.

## Leg 6 — save and load

`E` on the street, `S`, and a letter: `SAVE WHICH GAME` offers `A` to
`J`. Slot `C` writes `SAVGAMC.DAT`, `CHRDATC1.SAV` to `CHRDATC6.SAV` and
an `.FX` for the four members under an effect (`docs/hosts.md` §6).
Recorded as `curse-save`; the disk it ends with is the `curse-saved`
snapshot. `curse-load` loads `C` from it and `BEGIN ADVENTURING` puts the
party back at `5,12 W 00:03`.

Before it writes, the program asks DOS how much of the drive is free
(`INT 21h AH=36h`), and the machine answers from a drive size it states
(32 MiB, less what the files on it take).

## Leg 7 — the wilderness

**Not driven yet.** The town's gates are closed by the story at first
(leg 2's guards); the wilderness waits on playing past that.

---

## The session library

Every session is `disk external`, pinned to a snapshot by its file
lines, at `--speed 386` and `--wall none`:

| session | disk | seams | relation |
| --- | --- | --- | --- |
| `curse-boot` | `curse-slots` | none | |
| `curse-quiet` | `curse-slots` | none | |
| `curse-quiet-cheats` | `curse-slots` | the three cheats, none pulled | `identical curse-quiet` |
| `curse-town` | `curse-slots` | none | |
| `curse-temple` | `curse-slots` | none | |
| `curse-camp` | `curse-slots` | none | |
| `curse-camp-wound` | `curse-slots` | `cheat-wound-party`, pulled | `contrast curse-camp` |
| `curse-save` | `curse-slots` | none | |
| `curse-load` | `curse-saved` | none | |
| `curse-fight` | `curse-slots` | none | |
| `curse-fight-cheat` | `curse-slots` | `cheat-invulnerable` | `contrast curse-fight` |
| `curse-fight-kill` | `curse-slots` | `cheat-kill-all`, pulled | `contrast curse-fight` |

```sh
python3 scripts/sweep.py --game-disk games/curse-slots --game-disk games/curse-saved
```
