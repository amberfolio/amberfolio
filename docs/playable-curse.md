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
GOG copy at the root (`docs/hosts.md` §2c). No seam is needed to play;
those available on this program are the debug cheats, the code wheel,
the text faces, the message pause, the journal, `modern-controls` (nine
of its pieces) and the automap, each with its facts in `docs/seams.md`
§10. The
legs below answer the challenge themselves; with `--seam code-wheel
--code-wheel-answered` the boot goes from `P` straight to the party
menu, and every frame after it comes earlier. Leg 3 with `--seam
message-pause` stands each combat line the setting. Sixty
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
  nothing there (with `modern-controls` on, Up and Down do what Home and
  End do, Left and Right step a bar's highlight, and at the party menu Up
  and Down move a cursor over the commands that `Return` takes). A bar
  takes its command's first letter, and `Return` takes the highlighted
  one.
- The `.txt` and `.scene.json` beside a still are the screen's text and
  the scene (`docs/hosts.md` §10, §13). Each area is named
  as a public walkthrough names it, and a grid it does not name is
  `AREA <n>` (`docs/seams.md` §10, the automap).

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

With `modern-controls` on the menu has a cursor, a white block on its
first row from the first frame: `P@2500 A@2650 Return@2800 Down@2950
Down@3100 Return@3250 A@3400` takes `LOAD SAVED GAME` by the cursor and
loads slot A, and Up and Down then walk the party's nine rows, wrapping
at both ends. Recorded as `curse-menu-cursor`, against the same keys
with no seam (`curse-menu-keys`, where the arrows and the `Return` do
nothing and the `A` asks `ADD FROM WHERE?`).

Leg 0's party of one, made and added without the `Return` after `SAVE
TORK?` (with the seam on that `Return` takes the cursor's `CREATE`),
then `M` at the main menu opens `MODIFY:` with `STR` lit. A character
can be modified while its experience is what Curse starts it on. With
`modern-controls` on, `Down Down Up Up Up` steps to `WIS`, back to `STR`
and on to the name field, a white block with the cursor's letter the gap
in it; `Right` moves the cursor and `Up` steps on to the hit points.
Recorded as `curse-modify-arrows`, against the same keys with no seam
(`curse-modify-keys`, where the arrows do nothing and the `Right` raises
the strength).

## Leg 2 — the town and a story event

From the opening, `Return Return Left` (facing a wall north), then
`Left Up` steps west into `6,13` and the innkeeper's event: two screens,
the second citing journal entry 31, which `--seam journal` puts on the
journal's log (`curse-cite`). `Right Up` is the street at `6,12 N`
and a sign. `Left Up Up Up Return Up` walks west along the street to the
weapon shop's sign at `3,12` and into the shop at `2,12`; `Y` takes the
shopkeeper's offer and the bar is `BUY VIEW POOL APPRAISE EXIT`; `B`
lists the stock with `BUY NEXT EXIT`. Recorded as `curse-town`.

The shopkeeper's offer is a script's two-answer question, `YES NO` with
`YES` lit: `Escape` does nothing there and `Return` takes `YES`. With
`modern-controls` on, `Escape` answers `NO`, the keeper's farewell takes
a `Return`, and the party is back on the street at `3,12`. From the
opening, `Return Left Left Up Return Return Right Up Return Left Up Up
Up Return Up +1 Escape +1 Return`: recorded as `curse-script-esc`,
against the same keys with no seam (`curse-script-keys`, whose first
`Return` takes `AREA` and whose last ends at the shop's bar).

**A locked door.** East of the sign at `6,12` the street narrows into an
alley: from `6,12 N`, `Right Up` is `7,12` and a line about it
(`Return`), and `Left Up` walks into the door on its north wall, which
is locked: the bar is `BASH PICK EXIT`, and the party does not move.
The bar is not raw, so the arrows do nothing at it. With
`modern-controls` on, Up and Down select the member who bashes or
picks: from the opening, `Return Left Left Up Return Return Right Up
Return Right Up Return Left Up +1 Down Down Up E` selects Mark, Travis
and Mark again, and `E` leaves the bar with the walk still on.
Recorded as `curse-door-arrows`, against the same keys with no seam
(`curse-door-keys`).

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

**The journal's reader.** With `--seam journal` the party's bar carries
`NOTES` from its first draw. After the innkeeper (`curse-quiet`'s keys),
`N@5350` opens the log over the program's outer frame, entry 31 on it;
`Return@5500` opens the entry, `N@5800` and `N@6100` turn to its second
page and its picture, `E@6400` is the log again and `E@6700` closes it,
the screen composed again and the bar back (`Right@7000` turns the
party). Over `tests/visual/curse-reader-store.txt` (`--journal-store`),
recorded as `curse-notes`; the same keys with no seam are
`curse-notes-keys`, where `Return` takes `AREA` and `E` encamps.
`tests/visual/curse-rdr-page.leg` holds the same walk in pixels and in
what each screen says, and `curse-not-bar.leg` that `NOTES` is all the
seam puts on the adventuring screen.

**The automap.** With `--seam automap`, `Tab` at frame 4,000 (ahead of
the `Left` there) opens the panel over the roster at the opening room,
and the walk draws the street into it; every `PRESS RETURN`, the
shopkeeper's question and the shop's bar take it away until the
party's bar is back, and a second `Tab` puts the roster back
(`docs/seams.md` §10). Recorded as `curse-town-map`. The same `Tab` on
`curse-temple`'s keys marks the temple's arched door at `4,7 W` as a
door, and at the altar the program's roster replaces the panel whole
under the temple's bar. Recorded as `curse-temple-map`.

**The automap under the reader.** With both seams on, after the
innkeeper: `Tab@5200` puts the map over the roster, `N@5350` opens the
log over the whole screen, `Return@5500` entry 31 and `N@5800` its
second page; `E@6400` is the log and `E@6700` closes it, and the screen
the program composes has the map on it again, pixel for pixel the one
before `N`. `Tab@7000` puts the roster back and `Tab@7300` the map.
Recorded as `curse-map-reader`; `tests/visual/curse-map-back.leg` holds
the three give-backs as stills.

**The move mode.** With `modern-controls` on, the party's bar is
`MOVE CAST VIEW ENCAMP SEARCH LOOK` with `MOVE` lit, and the arrows step
it and the party list instead of walking: `Return` on `MOVE` makes the
bar `EXIT` alone, and the arrows walk until `Return`, `Escape` or `E`,
or a screen of the game's own that asks for a choice, brings the menu
back. A sign's or a story line's `PRESS <ENTER>/<RETURN> TO CONTINUE` is
not one: after its `Return` the bar is `EXIT` again. So a walk with the
seam on needs a `Return` before it, and only that. From the opening,
`Return Left Escape Return Left Up Return Return Right Up Return Left Up
Up Right Up Up Up Up Up Return Up Up Up Up Right Up Return Escape Down
Right Right Return Escape` takes `MOVE`, turns, goes back to the menu,
walks past the innkeeper and the signs at 6,12, 4,7 and 5,3, selects Mark and opens View for him (`curse-move-walk-mode`);
with the seam off the same keys toggle the overhead view, make the same
walk and turn (`curse-move-walk`).

Walking west at `1,14`, a group of royal guards turns the party back:
the way is closed until the royal carriage comes. Leg 7 is what brings
it.

## Leg 3 — a fight

The tavern, The Curse: from the street at `6,12 N`, `Left Up Up Right Up
Up Right Up Return Up` (its sign at `5,10`, then its question at
`6,10`), and the bar's `PUNCH BARKEEP` is the first choice: `Return`
starts a brawl with ten patrons. In the round, `Escape` leaves the move
cursor and `Q` puts a member on QUICK; six of each puts the party on
QUICK (`curse-fight` presses `Up Return Return` first: the two Returns
bring up the command bar and take its `MOVE` again). The program asks
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

**The aim bar.** With `modern-controls` on the walk is made in walking
mode: `curse-fight`'s keys as far as `Return@8200`, with `Return@4000
Left@4150 Escape@4300 Return@4450` in the place of its `Left@4000`, and
the brawl is the same one, Travis first at `25,10`. `Escape@9050 Q@9200
Q@9350` puts Travis and Shara on QUICK, and Mark's command bar, `MOVE
VIEW AIM QUICK DONE`, is up by 9,600 with a patron beside him. `A` is `AIM:NEXT PREV MANUAL
CENTER EXIT` on Mark himself; `N` the patron, `RANGE = 1`, and `TARGET`
on the bar; `M` the manual cursor, `(USE CURSOR KEYS) TARGET CENTER
EXIT`, which the arrows move; `E` goes back to the target bar and `E`
again to the command bar. The scene's `aim` beside each still is in
`docs/hosts.md` §13.

**The command bar's arrows.** At that bar, with `modern-controls` on,
Left and Right step the highlight as at every other bar: the bar opens
on `DONE`, and `Right@9700 Right@9850 Right@10000 Return@10150` light
`MOVE`, `VIEW` and `AIM` and take `AIM`, on Mark himself. With the
seam off the program takes an arrow there as the start of a move, and
the first Right moves Mark. Recorded as `curse-combat-bar-arrows`,
against the same keys with no seam (`curse-combat-bar-keys`).

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

## Leg 8 — a Pool of Radiance party

`ADD CHARACTER TO PARTY`, `POOL` asks for no directory and looks in none
but the program's own save directory, the one `CURSE.CFG` names
(`\CURSE\SAVE` on the Steam copy) and leg 0's `CURSE` lists `*.GUY` in.
It searches `*.CHA` there, Pool of Radiance's file for a character kept
under a name (`docs/hosts.md` §6), and then `*.SAV`, opening each to
measure it: a 285-byte record is a member of a Pool of Radiance saved
game, and a 422-byte one is Curse's own and is left off the list. So the
disk for this leg is `curse-slots` with Pool of Radiance's files copied
into its `SAVE` from that game's save directory: two `<NAME>.CHA` with
their `<NAME>.SPC`, and the `CHRDATJ1.SAV`, `.ITM` and `.SPC` of a game
saved in slot J. The two games name a slot's records alike, so a record
sits there under a letter Curse has not saved.

```
P A Return A P Return End End Return E
```

`POOL` lists the three, the saved game's member with the slot's letter
after the name, over the bar `ADD A CHARACTER: ADD EXIT`. `Return` takes
`ADD`, which marks the row; `End End Return` adds the third and `E` is
the party menu again, nine items now, with both listed at the armour
class and hit points Pool of Radiance left them (frame 3,950).

Adding a character reads its record, then looks for `<NAME>.SWG`,
`<NAME>.FX` and `<NAME>.SPC` and reads the `.SPC` when it is there. A
saved game's member is looked for the same way, under the first eight
letters of the character's name, and not under the record's own. No
`.ITM` is asked for, the one beside the record included, and the member
has no items in `VIEW`, whose bar is `TRADE DROP EXIT`. Nothing is
written by the add. `SAVE CURRENT GAME` then writes 422-byte records
under Curse's names, a member removed is kept as `<NAME>.GUY`, and Pool
of Radiance's files stay as they were. Records out of a Steam copy's
save directory and out of a copy's laid out at the root were added this
way.

The wasm module is the same from the same folder: `drive.mjs` with
`--install '\CURSE'` and these keys draws frame 3,950 byte for byte. No
session records the leg; no snapshot holds one game's files in the
other's save directory.

**`COPYCURS.EXE`**, the tool shipped beside the program for moving
characters between the games, does not run. It asks for 80x25 text,
which is a notice (`docs/machine.md` §5), and stops six frames in at
`INT 10h AH=01h`, the cursor's shape, before it names a file. Its
screens are text this machine has no path for.

---

## The session library

Every session is `disk external`, pinned to a snapshot by its file
lines, at `--speed 386` and `--wall none`:

| session | disk | seams | relation |
| --- | --- | --- | --- |
| `curse-boot` | `curse-slots` | none | |
| `curse-boot-wheel` | `curse-slots` | `code-wheel`, a wrong letter typed | `identical curse-boot` |
| `curse-boot-answered` | `curse-slots` | `code-wheel`, answered | `contrast curse-boot` |
| `curse-quiet` | `curse-slots` | none | |
| `curse-quiet-cheats` | `curse-slots` | the three cheats, none pulled | `identical curse-quiet` |
| `curse-quiet-font-sans` | `curse-slots` | `font-sans` | `contrast curse-quiet` |
| `curse-quiet-font-chisel` | `curse-slots` | `font-chisel` | `contrast curse-quiet` |
| `curse-quiet-message-pause` | `curse-slots` | `message-pause` | `identical curse-quiet` |
| `curse-quiet-controls` | `curse-slots` | `modern-controls` | `contrast curse-quiet` |
| `curse-quiet-journal` | `curse-slots` | `journal`, `Notes` never chosen | `contrast curse-quiet` |
| `curse-notes-keys` | `curse-slots` | none | |
| `curse-notes` | `curse-slots` | `journal`, entry 31 opened and read | `contrast curse-notes-keys` |
| `curse-town` | `curse-slots` | none | |
| `curse-quiet-automap` | `curse-slots` | `automap`, Tab never pressed | `identical curse-quiet` |
| `curse-town-map` | `curse-slots` | `automap`, Tab at frame 4,000 | `contrast curse-town` |
| `curse-temple-map` | `curse-slots` | `automap`, Tab at frame 4,000 | `contrast curse-temple` |
| `curse-temple` | `curse-slots` | none | |
| `curse-camp` | `curse-slots` | none | |
| `curse-camp-wound` | `curse-slots` | `cheat-wound-party`, pulled | `contrast curse-camp` |
| `curse-save` | `curse-slots` | none | |
| `curse-load` | `curse-saved` | none | |
| `curse-fight` | `curse-slots` | none | |
| `curse-fight-cheat` | `curse-slots` | `cheat-invulnerable` | `contrast curse-fight` |
| `curse-fight-kill` | `curse-slots` | `cheat-kill-all`, pulled | `contrast curse-fight` |
| `curse-fight-pause` | `curse-slots` | `message-pause` | `contrast curse-fight` |
| `curse-new-party` | `curse-slots` | none | |
| `curse-buy` | `curse-slots` | none | |
| `curse-heal` | `curse-slots` | none | |
| `curse-fix` | `curse-slots` | none | |
| `curse-carriage` | `curse-slots` | none | |
| `curse-journey` | `curse-wild` | none | |
| `curse-journey-kill` | `curse-wild` | `cheat-kill-all`, pulled | `contrast curse-journey` |
| `curse-journey-journal` | `curse-wild` | `journal` | `identical curse-journey` |
| `curse-cite` | `curse-slots` | `journal`, entry 31 cited | `contrast curse-buy` |
| `curse-camp-keys` | `curse-slots` | none | |
| `curse-camp-arrows` | `curse-slots` | `modern-controls` | `contrast curse-camp-keys` |
| `curse-name-keys` | `curse-slots` | none | |
| `curse-name-arrows` | `curse-slots` | `modern-controls` | `contrast curse-name-keys` |
| `curse-menu-keys` | `curse-slots` | none | |
| `curse-menu-cursor` | `curse-slots` | `modern-controls`, the main menu's cursor moved and taken | `contrast curse-menu-keys` |
| `curse-modify-keys` | `curse-slots` | none | |
| `curse-modify-arrows` | `curse-slots` | `modern-controls`, Modify's fields and name field | `contrast curse-modify-keys` |
| `curse-move-walk` | `curse-slots` | none | |
| `curse-move-walk-mode` | `curse-slots` | `modern-controls`, the move mode | `contrast curse-move-walk` |
| `curse-quiet-all` | `curse-slots` | `code-wheel`, `automap`, `journal`, `message-pause` and the three cheats, none triggered | `identical curse-quiet-journal` |
| `curse-quiet-all-on-sight` | `curse-slots` | those, and `modern-controls` | `contrast curse-quiet-all` |
| `curse-map-reader` | `curse-slots` | `automap`, `journal`: the map, the reader over it, the map back | |
| `curse-script-keys` | `curse-slots` | none | |
| `curse-script-esc` | `curse-slots` | `modern-controls`, Escape at a script's question | `contrast curse-script-keys` |
| `curse-door-keys` | `curse-slots` | none | |
| `curse-door-arrows` | `curse-slots` | `modern-controls`, Up and Down at a locked door's bar | `contrast curse-door-keys` |
| `curse-combat-bar-keys` | `curse-slots` | none | |
| `curse-combat-bar-arrows` | `curse-slots` | `modern-controls`, Left and Right at the brawl's command bar | `contrast curse-combat-bar-keys` |

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

## The visual legs

Four legs in `tests/visual` drive this program, each on the
`curse-slots` snapshot with a `speed 386` line
(`scripts/visual-legs.py`, `docs/journal-test-plan.md` §5):

| leg | kind | what it holds |
| --- | --- | --- |
| `curse-not-bar` | pair, `journal` | on and off differ in the five cells of `NOTES` on the party's bar and nowhere else |
| `curse-rdr-page` | single, `journal` | the log, entry 31's pages forward and back, its picture, each settled and each saying what it said; the screen before `N` back after the last `E`, every pixel |
| `curse-map-back` | single, `automap` and `journal` | the map up before the log is the map up after it; Tab takes it away and brings it back |
| `curse-text-create` | single, `modern-controls` | eleven screens from the title's bar through a character's creation read back as text, every highlight a block |

```sh
for leg in curse-not-bar curse-rdr-page curse-map-back curse-text-create; do
  python3 scripts/visual-legs.py --game-disk games/curse-slots --leg $leg
done
```

The runner takes one disk and runs every leg it is asked for on it, so
a leg is named: without `--leg` the Pool of Radiance legs would be
driven on this disk too.
