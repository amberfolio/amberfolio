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
and the debug cheats and the journal's citation watch are the seams
available on this program. Sixty
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
  `Down` turns round. **In a list, `Home` and `End` move the
  selection**, the party list and a menu's items alike; the arrows do
  nothing there. A bar takes its command's first letter, and `Return`
  takes the highlighted one.
- The `.txt` and `.scene.json` beside a still say `wrong_binary`: screen
  text and the scene have no facts for this program yet (#555).

---

## Leg 0 — a party

```
P A Return C +2 Return +2 Return +2 Return +2 Return +3 N +3 T O R K
Return +4 E +4 +2 Y +4 Y +4 Return +4 A +3 Return +3 Return +4 E +3 B
```

(tokens, 150 frames a slot from 2,500; `+N` skips `N` slots.) `C`
creates: `PICK RACE`, `PICK GENDER`, `PICK CLASS`, `PICK ALIGNMENT`, each
taken with `Return` on its first item, makes a male dwarf lawful good
fighter. Curse makes a new character at level 5 with 25,000 experience
points and 300 platinum. `REROLL STATS?` `N` keeps the roll,
`CHARACTER NAME:` takes the letters and `Return`, and the icon editor
opens (`PARTS 1ST-COLOR 2ND-COLOR SIZE EXIT`): `E`, `IS THIS ICON OK?`
`Y`, `SAVE TORK?` `Y` writes `TORK.GUY` and `TORK.FX` to the save
directory. `A` asks `ADD FROM WHERE? CURSE POOL HILLSFAR EXIT`; `Return`
takes `CURSE`, which lists every `*.GUY` there, and `Return` adds the
highlighted name. The party menu grows to nine items with TORK listed,
and `B`egin runs the opening at `7,13 E 00:00` for a party of one, the
same two screens the shipped slots see. Recorded as `curse-new-party`.

A loaded party's member taken off with `REMOVE CHARACTER FROM PARTY` is
kept the same way: `<NAME>.GUY`, `<NAME>.FX`, and `<NAME>.SWG` when the
member carries something.

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
the second citing journal entry 31, which `--seam journal` puts on the
journal's log (`curse-cite`). `Right Up` is the street at `6,12 N`
and a sign. `Left Up Up Up Return Up` walks west along the street to the
weapon shop's sign at `3,12` and into the shop at `2,12`; `Y` takes the
shopkeeper's offer and the bar is `BUY VIEW POOL APPRAISE EXIT`; `B`
lists the stock with `BUY NEXT EXIT`. Recorded as `curse-town`.

**A purchase.** `B` again buys the highlighted item, the battle axe at
the top, for the selected member: the list stays up, and Mathew's
platinum goes from 300 to 297. `E` leaves the list, `E` the shop, and
`Return` answers the farewell; `E` encamps on the street at `3,12`, and
`S` `C` saves, which writes `CHRDATC1.SWG` (63 bytes) beside the records
(`docs/hosts.md` §6). Recorded as `curse-buy`.

The street runs north and south along column 4. **The temple**: from
`6,12 N`, `Left Up Up Right Up Up Up Up Up` to the Temple of Gond's
sign at `4,7` (`Return`), then `Left Up` through its door to an acolyte
at `3,7` who points the party at the altar (`Return`), and `Up Up Up`
west to `0,7`, where the bar is `HEAL VIEW POOL APPRAISE EXIT`.
Recorded as `curse-temple`. `Return` takes `HEAL`, which lists the
cures for the selected member (`CURE BLINDNESS` to `STONE TO FLESH`);
`End End` moves to `CURE LIGHT WOUNDS`, `Return` asks
`PAY FOR CURE` (100 gold pieces), and `Y` casts it. Leg 3's brawl
leaves Mathew on 39 of 49: after it the cure takes him to 40 and his
platinum from 300 to 280 (`curse-heal`).

Walking west at `1,14`, a group of royal guards turns the party back:
the way is closed until the royal carriage comes. Leg 7 is what brings
it.

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

After it, the program asks whether to go back for the treasure left
(`N`); the party is put out on the street at `6,10` with two screens of
story (`Return`, `Return`), and at `5,10 E` a yes-or-no question: `Y`
finds an item and cites journal entry 17, `N` goes on. Both leave the party on the
street with Mathew on 39, Mark on 19, Travis on 0, Ledera on 13 and
Shara on 10.

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
`D A` adds a day, `R` rests). **On the street royal guards end any rest
at once and tell the party to move along**, `FIX` included. Recorded:
`curse-camp` (`E`, `VIEW`, out) and its wounded twin.

**The room the party wakes in is somewhere they do not walk.** A rest
there with nothing to heal and nothing to memorize ends at once without
a word and without the clock moving; one with spells to learn takes
the time they need. So the Fix leg is, from the opening at `7,13 E`:

```
E +4 End End End End +2 M +3 M +3 Home*6 Return*5 +2 E +3 Return +3 Y
+3 R +4 R +8 E +3 E +3
```

`E`ncamp, `End` four times to Shara (the cleric; the shipped parties
have no spell memorized), `M`AGIC, `M`EMORIZE: her grimoire opens on its
last line, and `Home` six times reaches `CURE LIGHT WOUNDS`; `Return`
five times takes her five first-level slots. `E` lists what is to be
memorized, `Return` takes `EXIT`, `MEMORIZE THESE SPELLS?` `Y`, and
`R`EST from the magic bar: `00:00` to `05:15`, and no guard. Then the
street and leg 3's brawl, and back: from `5,10 E` after the commotion,
`Down Up Left Up Up Left Up Up Right Up Left Up` (with `Return` after
each step) is `7,13`. `E`, `F`IX: Shara casts her five cures, Mathew 39
to 49 and Mark 19 to 49, and the party rests until she has them again,
`05:42` to `10:57`; one round, so Ledera and Shara are still hurt and
Travis still down. Recorded as `curse-fix`.

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

The guards at `1,14` let the party by only once the royal carriage has
come, and it comes **the second time they meet the party after it has
been into both the weapon shop and the sage's**. From the street at
`6,12 N`:

```
Left Up Up Up Return Up +1 N +2 Return +2 Down Up Return Left Up Return
Up Return Up Return Up Return Up Return Up Return Up Return Right Up
Return Up +2 Y +3 Return +3 Return +3 Return +3 Return +3 Down Up
Return Left Up (Return Up)*9 Right Up Return Up Return Up Return +3
Return +4 Up
```

Into the weapon shop at `2,12` and out with `N`; north up column 4 to
`4,5` and east past the sign at `5,5` into the sage Filani's at `6,5`.
`Y` (the sigils), `Return` takes `TELL THE TRUTH`: half the party's
funds, and journal entry 38. South down column 4 to `4,14` and west to
`1,14`: the guards send the party back to `2,14`. `Up` again, and the
carriage comes: three screens of story, and a battle with the guards
begins (frame about 22,700). Recorded as `curse-carriage`.

### Out of the city

The carriage's battle is the start of a road out, not a gate. The
wilderness is reached at the end of the thieves' plot, and the sewers'
placard that offers it appears only once that plot is won:

1. **The guards.** Win the carriage's battle (`N` to stop fighting
   when it asks), leave the treasure (`E`, `N`), and `Y` surrenders to
   the guards that charge next: jail, a thief, and the thieves' guild at
   `9,3`.
2. **The guild.** `N` to the guildmaster's offer of rest; four
   messages, then a battle with a rival gang that the party's `QUICK`
   setting fights to the end. Journal entry 4.
3. **To the sewers.** From `9,3 E` to the guild's south exit at `10,15`
   and off the map's south edge, past two random patrols (`11,6`,
   `10,8`), a crowd at `8,10` (`Return` keeps calm) and a locked door
   between `10,13` and `10,14` (`Return` bashes it).
4. **The sewers.** Four stretches, each ended by stepping off the south
   edge, which puts the party back on the north row (at `0,0`, `9,0`,
   `11,0`, then into the hideout): a checkpoint at `1,8` (`Y` surrenders
   and costs the party its money and gear), a knight at `13,10` (`N`
   answers that the party serves no one), and a locked door into
   `12,13` on the last. The position line shows the facing and the time
   but no square, so the route is counted by the steps. A `Return`
   after every step answers each message without losing a key.
5. **The hideout.** Entered at `11,0`;
   a patrol at `11,6`, and at `4,12` a checkpoint: `Y` is taken to the
   gang's leader (journal entry 11), whose battle ends the plot. Journal
   entry 54, a dream, and the party is on the overland map.

`cheat-invulnerable` and `cheat-kill-all` with a pull before each of
the six battles take a scripted run through it (several pulls each wait
for the next battle). The run ends at about frame 96,700: `C` camps at
the town's edge, `S` and `D` save the party in slot `D`. That disk is the
`curse-wild` snapshot.

### The overland map

Curse has no wilderness to walk square by square. The overland map is
a picture of the region with the party's mark on it; `LOAD` `D` and
`BEGIN ADVENTURING` bring it straight up, with the party at a town's edge
(`W 02:45`, no square) and the bar `ENTER CITY`, `JOURNEY ON`, `CAMP`.
`ENTER CITY` here only turns the party away.

```
P A Return L D B +4 J +3 End +2 Return +6 W +4 Return +3 Return
@7250 Return @7650 E
```

`JOURNEY ON` lists the three places the party may go from here, a list
that `Home` and `End` move through. The second asks how: `TRAIL`,
`WILDERNESS` or `EXIT` (`EXIT` goes back to the town's edge). `W` takes
the wilderness, a message about a mountain pass, and a battle with
flying beasts on open ground (frame about 6,300). The party fights it by
itself and loses: everyone down by about frame 9,600, and the screen
that ends the game. Recorded as `curse-journey`. The trail to the same
place meets the same pass and the same battle. The first place on the
list goes by an inn, where the party's rest is cut short by a battle.

`curse-journey-kill` is the same script with `cheat-kill-all` pulled at
the `W`: the party wins at once (frame about 7,100), `Return` takes the
win, `E` leaves the treasure, and the mark stands at the next town's
edge with the same bar.

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
| `curse-new-party` | `curse-slots` | none | |
| `curse-buy` | `curse-slots` | none | |
| `curse-heal` | `curse-slots` | none | |
| `curse-fix` | `curse-slots` | none | |
| `curse-carriage` | `curse-slots` | none | |
| `curse-journey` | `curse-wild` | none | |
| `curse-journey-kill` | `curse-wild` | `cheat-kill-all`, pulled | `contrast curse-journey` |
| `curse-journey-journal` | `curse-wild` | `journal` | `identical curse-journey` |
| `curse-cite` | `curse-slots` | `journal`, entry 31 cited | `identical curse-buy` |

```sh
python3 scripts/sweep.py --game-disk games/curse-slots --game-disk games/curse-saved \
  --game-disk games/curse-wild
```

The sweep replays them on the desktop host. The wasm module replays one
through `drive.mjs`, which does not lay a Steam copy out at `\CURSE` by
itself the way the desktop host does, so it is told:

```sh
node build/wasm/hosts/web/Release/drive.mjs games/curse-slots START.EXE \
  --install '\CURSE' --replay tests/sessions/curse-fight.rec
```
