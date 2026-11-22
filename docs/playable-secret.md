# Playable: Secret of the Silver Blades

How to drive a player-supplied copy of Secret of the Silver Blades from
the party menu into the game, leg by leg: the keystrokes, what the run
shows, and the traps. `docs/playable.md` is the same for Pool of Radiance
and has the method in full; `docs/first-light.md` stops at the party
menu. Nothing in this repository can run any of it (PLAN.md §6).

The legs here are the ones driven so far. What is still to be driven is
on #608: a training carried through, and the ruins beyond the six of
their script's doors that leg 15 and the runs beside it went through.

---

## The method

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/<preset>/hosts/sdl/<config>/amberfolio <your-copy> START.EXE \
    --fast max --speed 386 --wall none --no-config \
    --press KEY@FRAME ... \
    --until <ticks> --dump run --dump-every 150
```

No `--install`: the Steam copy is recognized and laid out at `\SECRET`
(`docs/hosts.md` §2c). No seam is needed to play, and the ones available
on this program are the three debug cheats, the two text faces, the
message pause, the automap, `modern-controls`, which here is its hero
keys at the party menu's `PICK CHARACTER` (leg 13), its selection block
(below) and its two pieces that observe, for the host reads below, and
the journal, opened by `N`
at the party's bar (`docs/seams.md` §10). Sixty
frames to the virtual second; `TICKS = FRAME * 19,886` at `--speed 386`
is close enough for `--until`.

Every script below is on a fresh copy of the Steam disk with its shipped
slots A and B (the `secret-slots` snapshot). Snapshot the directory and
restore it between runs.

The `.txt` beside a still is the screen's text (`docs/hosts.md` §10),
and the `.scene.json` the scene (`docs/hosts.md` §13): the grid in the
town, the ruins' with the line's square and the stored one (below), every
fight, and with `--seam modern-controls` the aim. The titles and the
opening's pages read `null`.

## The program's own controls

What a key does on each screen driven so far. S3's controls port starts
from this table.

| screen | what it is | keys |
|---|---|---|
| the titles | pictures and credits, then `PLAY DEMO` | any key skips to `PLAY DEMO`; `Return` takes `PLAY`, and `D` plays the demonstration |
| the party menu | a list with a highlight over a `SELECT` bar | `Up` and `Down` move the highlight, `Return` takes the row. **A letter is not a command here** |
| a prompt on the bottom line | `LOAD WHICH GAME: A B`, `YES NO` | the letter |
| a page of story | text under a picture, `PRESS <ENTER>/<RETURN> TO CONTINUE` | `Return` |
| a command bar | words on the bottom line, the first one lit | the word's first letter; `Return` takes the lit one |
| the adventuring bar | `MOVE AREA CAST VIEW ENCAMP SEARCH LOOK` | `M`, or `Return` while `MOVE` is the lit word (it is not after a camp), starts the program's own move mode: the bar is `EXIT`, `Up` steps, `Left` and `Right` turn, `E` ends it |
| the treasure bar | `VIEW TAKE POOL SHARE EXIT` | `E` leaves; with treasure left the program asks whether to go back, and **`N` is the one that leaves** |
| the camp bar | `SAVE VIEW MAGIC REST ALTER FIX EXIT` | letters; the program has its own `FIX` |
| a character's view | a sheet over `HEAL CURE EXIT` (what a member can do varies) | `E` leaves |
| a fight | the field, the member whose turn it is, `MOVE VIEW AIM QUICK DONE` | `Q` gives that member to the computer for the fight, so a party of six takes six |
| the end of a fight | `CONTINUE BATTLE: YES NO`, when no enemy stands | `N` takes the win |
| a pick list | `PICK RACE` and the like in a character's creation, an item list in a shop: rows with one lit as a block, over a short bar (`SELECT EXIT`, `ITEMS: BUY EXIT`) | `Up` and `Down` move the block, `Return` takes the bar's lit word for that row. `Home` and `End` do nothing |
| a line of text | `CHARACTER NAME:`, `HOW MUCH GEMS WILL YOU DEPOSIT?` | the characters, then `Return`. **An arrow key types nothing here** |
| a shop's bar | `BUY VIEW POOL APPRAISE EXIT` (the temple's begins `HEAL`, the vault's is `DEPOSIT WITHDRAW TRADE EXIT`) | the letter. `Return` takes the lit word, which is not always the first after coming back from one |

At any bar `Left` and `Right` move the lit word and `Escape` leaves a
view; at the adventuring bar `Up` and `Down` change which member is the
current one. The key card (`docs/hosts.md` §11) is this table and the
rows below, as a player reads it.

| where | key | what it does |
|---|---|---|
| the party menu | `8`, `2` on the number row or the keypad | as `Up` and `Down`; `Up` on the first row goes to the last |
| the party menu | a letter, `Home`, `End` | nothing |
| a pick list | `8`, `2`; `Escape` | as `Up` and `Down`; `Escape` leaves it (the party menu's `INITIALIZE MOUSE/JOYSTICK` list, leg 7's shop) |
| the party menu's `PICK CHARACTER` | `8`, `2`, `4`, `6`; `1`, `3`, `5`, `7`, `9` | as the arrows: the member, and `SELECT` or `EXIT`; the first member (leg 13). With `modern-controls` on, the number row's `1` to `8` are the member of that number and the keypad's stay the arrows |
| the adventuring bar | `8`, `2`, `4`, `6`, the number row's or the keypad's | as the arrows: the current member, and the lit word |
| the adventuring bar | `1`, `3`, `5`, `7`, `9` | the first member becomes the current one |
| the adventuring bar | `,`, `.`, `Escape` | nothing |
| the adventuring bar | `M` | the move mode, as `Return` on `MOVE` |
| the adventuring bar, the move mode | `N` | nothing: it is none of either bar's commands (with `journal` on, `NOTES` is on the adventuring bar in `AREA`'s place, below) |
| the move mode | `Down`; `8` and `4`, the number row's or the keypad's | turns round; as `Up` and `Left` |
| the move mode | `Return`, `E`; `Escape`, `Home`, `End` | the bar again, `MOVE` lit; nothing |
| camp | `Escape` | at a member's sheet, back to the camp's bar; at that bar, out of camp |
| a fight | `M`, then the arrows | `MOVE/ATTACK, MOVE LEFT = 12 DONE`: each arrow a square, the count going down |
| a fight | `Escape` while moving | the combat bar, and the fighter back on the square the move began on |
| a fight | an arrow at the combat bar | moves the lit word, and nobody |
| a fight | `D`, `V` | `GUARD DELAY QUIT SPEED EXIT`; the member's sheet, which `E` leaves |
| a line of text | `Backspace`, `Escape` | erases the last character; takes the line as typed |

Driven over leg 1 to its adventuring bar, over leg 0 and its party menu,
over leg 4 to its fight and over leg 10 to its name. `Home` and `End` at
the adventuring bar were seen once each, too little to say: `End` took
the current member from the second back to the first, and did nothing
on the first; `Home` did nothing on the first.

## The watch list

`--watch OFF[:N]` reads the data segment, which is the image's segment
plus `0xDE2` paragraphs (`ds=0E42` with the image at `0060`). The program runs with DS elsewhere
now and then, and a line with another `ds` is not this table's.

| offset | what |
|---|---|
| `67E7` | the game mode: 0 the party menu, 2 camp, 4 adventuring (the opening's pages too), 5 a fight, 6 the treasure screen |
| `89D7`, `89D8`, `89D9` | the party's square (column, row) and facing: 0 north, 2 east, 4 south, 6 west |
| `87CC:2` | how many of each side stand in a fight: the party's byte, then the enemies' |
| `7D3C:2`, `7D3E:2` | the party's first record, a far pointer (`docs/seams.md` §10 has the record's offsets) |

**The map** is at the far pointer at `89CE` (offset, then segment): four
planes of 256 bytes, a byte a square at `row * 16 + column`. Plane 0
holds the north and east walls as its high and low nibbles and plane 1
the south and west; a zero nibble is no wall, and any other is the kind
of wall drawn there. Plane 3 holds two bits a side (north in bits 0 and
1, then east, south, west), and a side with a wall and a 1 there lets
the party through. The program's own step (overlay 32 `+0x0568`) lets
the party through a side with no wall or with a non-zero pair, and
nowhere else. **In the ruins a 0 is not always shut**: walls of kinds 3
and 4 with a 0 stopped the party, and walls of kinds 7 and 8, drawn as
wooden doors, let it through with a 0 where the area's script has a door
on the square, because the script moves the party there itself (below).
Which kinds are doors has not been worked out past those. A route
computed from the planes, eighteen keys from the mayor's house to the
east gate with no wall on the way, walked square for square.

**The status line's square is not always the stored one.** In the town
the two agree. In the ruins the area's script gives the line a square of
its own, two words of the area record (`+0x3FA`, `+0x3FC`; `0xFF` in the
first is the town's "print the stored one"), and turns the facing it
prints by a third (`+0x3FE`, quarter turns: the line prints the facing
byte plus twice it). It keeps the party on one sixteen-by-sixteen grid
whose map, wall set and geometry block stay the same over every step
driven, cut into pieces; through a door it carries the party to another
piece, which it may turn as well as move, and it adds one to the line's
square in the direction of every step, so its numbers are the party's
walk and run far past fifteen. The pieces leg 15 and its runs met, the
line's square being the stored one less the offset, or turned:

| the line's squares | stored squares | turn | reached through |
| --- | --- | --- | --- |
| `0..3,13..15` | `11..14,0..2` | 0, less `11,-13` | the gate |
| `4..9,15`, `9,16..17` | `10..15,3`, `15,4..5` | 0, less `6,-12` | stored `14,2`'s east face, kind 8 |
| `10..11,17`, `11,18..19` | `9..10,0`, `10,1..2` | 0, less `-1,-17` | stored `15,5`'s east face, kind 8 |
| `11..13,20` | `3..5,0` | 0, less `-8,-20` | stored `10,2`'s south face, kind 8 |
| `12..18,16..20`, where walked | `6..12,3..7`, the same squares | half round: the line's is `24,23` less the stored | stored `5,0`'s north face, kind 7 |
| `14,20..24` | `0,7..11` | 0, less `-14,-13` | stored `10,4`'s north face, kind 8 |
| `14,25..26` | `4,8..9` | 0, less `-10,-17` | stored `0,11`'s south face, kind 7 |

Over every still of every run, 49 of the line's squares, from `0,13` to
`18,26`: no square of the line's was ever two stored squares or two
turns, and no stored square two of the line's. The four doors leg 15
goes through each went back the way it came. The area's script (block 32
of `ECL1.DAX`) sets the line's square only where the party arrives, from
a table of fifteen arrivals (the gate's is `0,15`, the farthest `83,56`),
and counts from there. A face across which the script carries the party is
solid on the grid on both sides (stored `14,2`'s east and `10,3`'s west,
`5,0`'s north and `11,4`'s north); a border between two pieces that the
script has no door for is the wall the grid has there (the line's
`13,20`, east).

---

## Leg 0: the boot to the party menu

```
--press Return@600 --press Return@900
```

The first `Return` cuts the titles to `PLAY DEMO` and the second takes
`PLAY`. The party menu is up by frame 930, with five rows and nobody in
the party. **Nothing is asked on the way**: no question, no document.
`secret-boot` is this leg.

## Leg 1: slot A, through the opening, to the adventuring bar

```
--press Down@1500 --press Down@1560 --press Return@1620 --press A@2100
--press Down@2700 --press Down@2760 --press Down@2820 --press Down@2880
--press Down@2940 --press Return@3100
--press Return@3600 ... every 300 frames ... --press Return@5700
--press Return@6100 --press E@6400 --press N@6700
--press Return@7000 --press Return@7300 --press Return@7600
```

- Two `Down` and `Return` take `LOAD SAVED GAME`, and `A` the slot. The
  menu comes back with six members and nine rows, its highlight on the
  third; five `Down` reach `BEGIN ADVENTURING`.
- Eight `Return` turn the opening's pages: a picture, then the mayor.
  **The seventh cites journal entry 37 by number**, which the journal's
  watch puts on its log (below). The mode byte is 4 from the first
  page.
- The opening ends in experience points and a treasure screen (mode 6):
  `Return`, then `E` and `N` to leave what is there.
- Three more pages, and the adventuring bar is up by frame 7,700: the
  party at `3,3` facing south in the mayor's house, the clock at 00:00.

## Leg 2: camp

```
... leg 1 ... --press E@8200 --press V@8500 --press E@8800 --press E@9100
```

`E` at the adventuring bar is `ENCAMP`: the mode byte reads 2 and the bar
is the camp's. `V` opens the first member's sheet, `E` closes it and `E`
leaves camp. `secret-camp` is this leg, 106 checkpoints.

**The roster is not redrawn when a hit point changes behind it.** The
party list stays on the screen through camp, and what it shows is what
it showed when it was last drawn; a sheet opened and closed draws it
again.

`S` at camp saves: the slot's letter, and the files are in the save
layer's table (`docs/hosts.md` §6).

## Leg 3: out of the east gate

```
... leg 1 ...
--press Return@8200                                   # MOVE
--press Up@8350 ... five, 150 frames apart            # south to 3,8
--press Left@9100                                     # face east
--press Up@9250 ... thirteen                          # to 15,8 and into the gate
--press Y@11200                                       # leave the town
```

Movement keys are 150 frames apart. Two signs go by on the street and
neither stops the walk. The thirteenth step is into the gate, where the
program asks whether to leave the town, and `Y` puts the party in the
ruins: stored `11,2` facing east, `0,15` on the status line. From there
eleven squares are joined by sides the map marks as letting the party
through. A door it does not mark opens to a step all the same, and the
one east of stored `14,2` leads out to a street: from the arrival,
`Left Up Right Up Up Up Right Up` reach `14,2` facing south, `Left Up`
goes through it to stored `10,3`, and five more `Up` reach stored
`15,3`, the line saying `9,15`. Leg 15 goes on from there.

## Leg 4: a fight

```
... leg 3 ...
--press Left@11500
--press Up@11650 --press Up@11800 --press Right@11950 --press Right@12100
    ... those four again, 150 frames apart, to frame 24,100 ...
--press Return@24300
--press Q@24700 ... every 200 frames ... --press Q@26500
--press N@27000 --press Return@27400 --press E@27800 --press N@28200
```

- Pacing two squares north and back draws a wandering encounter: the
  program says monsters attack at frame 23,850, with 00:59 on its clock. **The same script over the same disk meets the same
  monsters at the same frame**, which `--wall none` is what makes true.
  Move one key and it is another encounter, or none yet.
- `Return` opens the field: the mode byte reads 5, and `87CC:2` reads
  `0406`, six of the party and four enemies.
- `Q` once for each member as its turn comes; the later ones fall on
  turns the computer already has and do nothing. The fight is over at
  frame 26,700 with the program's own question, `N` takes the win, and
  the party has lost hit points in three places.
- Experience points and the treasure screen follow: `Return`, `E`, `N`,
  and the party is back in the ruins in its move mode.

`secret-fight` is this leg, 371 checkpoints.

## Leg 5: a new party

```
... leg 0 ...
--press Return@1500                                   # CREATE NEW CHARACTER
--press Return@1800 --press Return@2100               # the lit race, the lit gender
--press Return@2400 --press Return@2700               # the lit class, the lit alignment
--press N@3000                                        # do not reroll
--press T@3300 --press O@3500 --press Return@3700     # the name
--press E@4000 --press Y@4300 --press Y@4600          # leave the icon editor, keep it, save
--press Down@5000 --press Return@5150 --press S@5450  # ADD CHARACTER TO PARTY, from SECRET
--press Return@5750 --press E@6200                    # ADD, and leave the list
--press Down@6600 ... six, 60 frames apart            # to BEGIN ADVENTURING
--press Return@7200
--press Return@7700 ... every 300 frames ... --press Return@9800
```

- A new character is made at the fifth level, with 200,000 experience
  points. `Return` at `REROLL STATS? YES NO` is `YES` and rolls again.
- After the name comes an editor for the figure a fight draws, a bar of
  its own; `E` leaves it and two questions follow, whether the figure
  will do and whether to save the character. `Y` to the second writes
  `<NAME>.WHO` and `<NAME>.SFX` in the save directory and goes back to
  the party menu with nobody in the party.
- `ADD CHARACTER TO PARTY` asks `ADD FROM WHERE? SECRET CURSE EXIT`.
  `S` lists the kept characters; `Return` on one marks it with an
  asterisk, and `E` leaves. The party menu then has nine rows and its
  highlight on the second, so `BEGIN ADVENTURING` is six `Down` away.
- The opening is the one a loaded party gets, to its treasure.

`secret-new-party` is this leg, 116 checkpoints. Shipped slot B is a
second party of six at this same point of the story.

**The other answer, `CURSE`**, lists the Curse of the Azure Bonds
characters (`*.GUY`) in the same save directory, and **`A` takes one
into the party**. Driven with two members removed from that game's
shipped party and their files put beside this game's saves: the list
names both, `A` marks the lit one with an asterisk, and the party menu
then has that member with its armour class and hit points. The program
reads each file's name and one byte at offset 132 to make the list, and
the whole `.GUY` when one is taken; it does not open the `.FX` beside
it.

**`Return` does not take one there.** The bar's lit word is kept from
one bar to the next by its place, as in the first two games: `CURSE`
was the second word of the bar before, so the second word, `EXIT`, is
the lit one on `ADD A CHARACTER: ADD EXIT`, and `Return` leaves. After
`SECRET`, the first word, it is `ADD`. A letter is the same either way.

## Leg 6: the town

New Verdigris is one map of sixteen squares a side, and its doors are
found by routing over the map's planes (the watch list). From the
mayor's house at `3,3`:

| square | what is there | what it offers |
|---|---|---|
| `14,6` | the armoury | `BUY VIEW POOL APPRAISE EXIT` |
| `14,11` | a wizard's house, where trinkets are sold | a `YES NO` first |
| `10,6` | a pub | `HAVE A DRINK RELAX LEAVE` |
| `10,9` | a temple | `HEAL VIEW POOL APPRAISE EXIT`; `HEAL` lists the cures for the current member |
| `7,6` | the training hall | the party menu with `TRAIN CHARACTER` and `HUMAN CHANGE CLASSES` among its rows |
| `7,9` | the vault | `DEPOSIT WITHDRAW TRADE EXIT` |
| `15,8` | the east gate | leg 3 |

Each door has a sign the walk passes without stopping, and stepping in
brings a greeting that waits for `Y`, or for `Return` at the vault.

**The party starts with gems, not coins.** The opening's treasure
screen has `SHARE`, which deals its gems out (`S`, before `E` and `N`),
and a shop's `APPRAISE` is where a gem becomes coins: `GEMS`, then
`SELL` at `YOU CAN : SELL KEEP`, fifty platinum for one.

## Leg 7: the armoury

```
... leg 1, with --press S@6400 before its E@6700 and N@7000, and its
    last three Returns at 7300, 7600 and 7900 ...
--press Return@8200                                   # MOVE
--press Up@8350 ... the route, 150 frames apart: four Up, Left,
    eleven Up, Left, Up ...                           # to 14,6
--press Y@11350                                       # yes, buy something
--press A@11650 --press Return@11950 --press Return@12250 --press E@12550
                                                      # APPRAISE, GEMS, SELL, and out
--press B@12850                                       # BUY
--press Down@13150 --press Down@13450 --press Down@13750 --press Return@14050
--press Escape@14350
--press V@14650 --press I@14950 --press E@15250 --press E@15550
```

The fourth row of the list is bought for ten of the fifty platinum, the
first member's encumbrance goes from 4 to 126, and `VIEW` and `ITEMS`
show the purchase among what the member carries, not readied.
`secret-buy` is this leg, 188 checkpoints.

## Leg 8: the vault

```
... leg 7's opening ...
--press Return@8200
--press Up@8350 ... four Up, Left, four Up, Right, two Up ...   # to 7,9
--press Return@10450                                  # past the greeting
--press D@10750 --press V@11050 --press D@11350       # DEPOSIT, a member's VIEW, DEPOSIT
--press Return@11650 --press 2@11950 --press Return@12250   # gems, two of them
--press E@12550 --press E@12850 --press E@13150
```

The deposit is what writes `VAULT.DAT` in the save directory, 28 bytes
after this one: one file, no slot's own (`docs/hosts.md` §6).
`secret-vault` is this leg, 158 checkpoints.

**After a save at camp the program asks `QUIT TO DOS YES NO`**; `N`
goes on.

## Leg 9: the aim

```
... leg 4 to --press Return@24300 ...
--press A@24700 --press N@24900 --press N@25100 --press N@25300
--press P@25500 --press M@25700
--press Left@25900 --press Up@26100 --press Up@26300 --press Right@26500
--press Down@26700 --press E@26900 --press E@27100
--press Q@27300 ... every 200 frames ... --press Q@29300
--press N@29800 --press Return@30200 --press E@30600 --press N@31000
```

- The fight opens on Dominic's turn at `29,14`, window `24,10`. `A` is
  `AIM`: the bar is `NEXT PREV MANUAL CENTER EXIT` from target to target,
  on Dominic himself at `RANGE = 0`; `N` steps nearest first through
  Malachite, Paine and Morgaine, `P` back to Paine, and the bar has
  `TARGET` on it where the target is in reach.
- `M` is `MANUAL`, `(USE CURSOR KEYS)`, the cursor where the target
  was; the four arrows step it a square each, over the party, the
  hippogriffs and an empty square, the panel showing whoever is under
  it.
- `E` from manual is back to target to target, on the square the cursor
  left, the window scrolled to `25,10`; a second `E` ends the aim at the
  combat bar.
- From there it is leg 4's: `Q` for each member, the program's question,
  `N`, and the treasure screen. The fight is over by frame 29,300.

`secret-aim` is this leg, 406 checkpoints; `docs/hosts.md` §13 has what
the scene reads at each key.

## Leg 10: a character's name

```
--press Return@600 --press Return@900
--press Return@1500 --press Return@1700 --press Return@1900
--press Return@2100 --press Return@2300
--press N@2500 --press T@2700 --press O@2760 --press R@2820
--press Return@3000
```

Leg 5's character to its name, with the keys closer together and the
name typed a letter at a time: a `Return` at each list takes its lit
row, `N` keeps the stats, and the bottom line is `CHARACTER NAME:`, the
program's line editor. `TOR` and `Return` take the name, and the
figure's editor is up by frame 3,050. `secret-name` is this leg, 40
checkpoints.

## Leg 11: a cure at the temple

```
... leg 7's opening ...
--press E@8200                                        # ENCAMP
--press E@8650 --pull cheat-wound-party@8650          # the party on one hit point each, and out
--press M@9100                                        # MOVE
--press Up@9250 ... four Up, Left, seven Up, Right, two Up ...   # to 10,9
--press Y@11800
--press A@12100 --press Return@12400 --press Return@12700 --press E@13000
                                                      # APPRAISE, GEMS, SELL, and out
--press H@13300                                       # HEAL
--press Down@13600 --press Down@13750                 # to the third cure of the list
--press Return@14050 --press Return@14350 --press Return@14650
--press E@14950 --press V@15250 --press E@15550 --press E@15850
```

With `--seam cheat-wound-party`, because no shipped party is hurt and a
fight's wounds are a long walk from this door. The temple's list is for
the current member; each `Return` buys the lit cure, and three of them
take the first member from 1 hit point to 15 and the fifty platinum the
gem fetched down to 10. `secret-heal` is this leg, with that seam.

**The training hall has not trained anybody.** `TRAIN CHARACTER` asks
`PICK CHARACTER`, where `Up` and `Down` choose and `Return` takes one,
and then the menu is back with no message and the member unchanged:
tried with the shipped party and with a new character carrying 300
platinum. What it wants is not known.
## Leg 12: a save, and its load

```
... leg 1 ...
--press E@8200                                        # ENCAMP
--press S@8500 --press C@8800                         # SAVE, into slot C
--press N@9100                                        # do not quit to DOS
--press E@9400
```

The save writes `SAVGAMC.DAT` and a `CHRDATC<N>.SAV` for each of the
six, with an `.SFX` beside three of them, ten files. `secret-save` is
this leg, and the disk it leaves is the `secret-saved` snapshot.

```
... leg 0, on that disk ...
--press Down@1500 --press Down@1560 --press Return@1620 --press C@2100
--press Down@2700 ... five, 60 frames apart ... --press Return@3100
--press V@3700 --press E@4000
```

`LOAD WHICH GAME` offers `A B C`, and `BEGIN ADVENTURING` puts the party
at the adventuring bar where it was saved, in the mayor's house, with no
opening to turn through. `secret-load` is this leg, pinned to that disk.

**There is no overland.** Where the first two games have a wilderness
or a journey between places, this one has the town's gates and, the
mayor says, a teleporter in his house; neither has shown a travel mode
(game mode 3) in anything driven.

## Leg 13: the party menu's member picker

```
... leg 0 ...
--press Down@1500 --press Down@1560 --press Return@1620 --press A@2100
--press Down@2400 --press Return@2500                 # VIEW CHARACTER
--press 3@2800 --press Return@3000 --press Escape@3300
--press Return@3600 --press 6@3900 --press S@4100 --press Escape@4400
--press Return@4700 --press 8@5000 --press 1@5200 --press Return@5400
--press Escape@5700
```

The party menu's `VIEW CHARACTER` asks `PICK CHARACTER` over `SELECT
EXIT`, with the current member's name white in the roster. Its digits
are the keypad's: `3`, `6` and `1` put the selection on the first member
and `8` steps it back to the last, so all three sheets are Guy's. With
`--seam modern-controls` the roster is numbered 1 to 6 and the number
row chooses: Epona's sheet, then Morgaine's, then, `8` being nobody in a
party of six, Guy's. `secret-pick` and `secret-pick-heroes` are this leg.

## Leg 14: Modify

```
... leg 0 ...
--press Down@1500 --press Down@1560 --press Return@1620 --press A@2100
--press Return@2400 --press Return@2600               # MODIFY CHARACTER, Guy
--press Down@2900 ... eight, 200 frames apart         # each field in turn
```

The party menu comes back from slot A with `MODIFY CHARACTER` lit, and
`PICK CHARACTER` with the first member selected, so two `Return` open
Modify on Guy, `MODIFY: ADD SUBTRACT KEEP EXIT`, strength highlighted.
`Down` steps the highlighted field, the program's own: strength,
intelligence, wisdom, dexterity, constitution, charisma, the name (its
cursor's letter white on black over it), the hit points' value, and round
to strength. The highlighted field is light magenta. `secret-modify` and
`secret-modify-block` are this leg.

## The selection block on this program

`--seam modern-controls` draws every selection as a white block with a
margin (`docs/modern-controls.md`, the Secret of the Silver Blades
section). Each run is the leg with the seam off and on, stills read side
by side:

| run | what it shows |
|---|---|
| leg 2 | the title's `PLAY`, the party menu's row, the roster's current member and the bars' lit words (treasure, party's bar, camp, a sheet's `CURE`) a block with a margin; the party menu's lone `SELECT` white letters; the script's notices one block from `P`, the treasure's notice the program's block with no margin |
| leg 13 | `PICK CHARACTER`'s `SELECT` a block; the current member's block moving to Epona at `3` |
| leg 10 | the creation lists' rows and `SELECT`, `REROLL STATS? YES NO`'s `YES`, the figure editor's `1ST-COLOR` a block, whole, where the program's own block ends in half a letter |
| leg 12 | `SAVE WHICH GAME`'s `A` and `QUIT TO DOS`'s `NO` |
| leg 1, then `E@8200 Down@8350 M@8500 M@8800 Down@9100 Down@9250` | the second member's block, the Magic bar's `CAST`, a spell list's row and `MEMORIZE` |
| leg 7, leg 11 | the armoury's goods and `BUY`, a member's items and `READY`, the temple's cures and `HEAL` |
| leg 9 (the `secret-aim` recording) | the fight's `MOVE`, the aim bar's `NEXT`, the manual aim's lone `TARGET`; the aim's box as the program draws it |
| leg 14 | each of Modify's fields a white block in turn, the name with its cursor's letter the gap in it, the hit points' value without the maximum |
| leg 1 with `--seam automap`, `Tab@8100 Down@8400 Tab@8700 Down@9000` | with the panel up the only pixels that differ from the seam-off run are the bar's margin; the roster comes back with its block |
| leg 1 with `--seam journal` and the store, `N@8100 Right@8400` | the log's row a white block, and `EXIT` after `Right` |
| leg 2 with `--seam font-sans`, `--seam font-chisel` | the same blocks in each face |

## Leg 15: through the ruins and back

```
... leg 3 ...
--press Left@11350 ... a key every 150 frames:
  Left Up Right Up Up Up Right Up Left Up        # through stored 14,2's east face
  Up Up Up Up Up Right Up Up Left Up             # through stored 15,5's east face
  Up Right Up Up Up                              # through stored 10,2's south face
  Left Up Up Left Up                             # through stored 5,0's north face
  Right Right Up                                 # and back: 5,0
  Right Up Up Right Up                           # 10,2
  Up Up Left Up Up                               # 15,5
  Right Up Up Left Up Up Up Up Up Up             # 14,2
  Right Up Left Up Up Up Left Up Right Up        # west out of the first square
  Up Up                                          # 13,8 in the town, the last key at frame 20,950
```

Out of the town as leg 3, then through four of the script's doors and
back through the same four, by the pieces in the watch list's table: the
first street to `3,15`, the second from `4,15` to `9,15` and south to
`9,17`, the third from `10,17` to `11,19`, the fourth from `11,20` to
`13,20`, and onto the piece the script turns half round at `13,19`, the
party on stored `11,4` facing south with the line saying north. `Right
Right` turns it there and the same doors in the other order put it on
the line's squares it walked out on, each the same stored square, and
west out of the first square into the town at `15,8`, the line printing
the stored square again. Nothing on the way stops the walk.
`secret-ruins` is this leg, 279 checkpoints.

**What the rest of the ruins hold, from the runs beside it**, each loaded
from a save at camp on the turned piece (stored `12,7`, the line's
`12,16`; slot C, saved as leg 12 saves):
north through stored `10,4` the party is at stored `0,7`, the line's
`14,20`, the turn back to none; south from there, stored `0,11` prints
`YOU SEE AN ARROW POINTING NORTH.`, waits for `Return`, and its south
face leads to stored `4,8`, the line's `14,25`. Leg 4's pacing meets its
wandering monsters on the first street, at the line's `0,13`. The doors
to the area's other two scripts (blocks 33 and 34, which print the
stored square) were not driven.

## The host reads on this program

| run | what it shows |
|---|---|
| leg 3 | the scene at every step through the town: the square and facing the status line prints, the faces around it (the mayor's door one square ahead at `3,4`, a door on the right at each sign on the street, the gate ahead at `15,8`) |
| leg 15 with `--seam automap` | the scene at every step through the ruins: `x`, `y` and `facing` the line's and `stored` the party's square and facing on the loaded grid, 63 stills checked against the area record and the party's bytes; on the turned piece the line's north is the stored south; `map` the page the panel draws |
| leg 4 | the fight's figures, hit points, turns and fallen, the window and its ground |
| leg 9 with `--seam modern-controls` | the aim at each of its keys, the seam fired 30 times |
| leg 10 with `--seam modern-controls` | `text-entry reading` from the empty `CHARACTER NAME:` through `TOR`, `not-reading` at `REROLL` before it and on the figure's editor after it; the line editor's entry and its return each reached once |

## The cheats on this program

Each is the leg above with one seam (`docs/seams.md` §10):

| run | what it shows |
|---|---|
| leg 4 with `--seam cheat-invulnerable` | the damage point is reached 28 times and no member loses a hit point |
| leg 4 with `--seam cheat-kill-all --pull cheat-kill-all@24700` | `87CC:2` reads `0006` at the frame of the pull; the enemies stay drawn until the round ends, the end check asks its question, and the party is back in the ruins at 01:00 where leg 4's is at 01:03, nobody hurt |
| leg 2 with `--seam cheat-wound-party --pull cheat-wound-party@8500` | the sheet reads one hit point of the member's full count, and the roster reads 1 six times once it is drawn again |

**Pull on a frame a key is pressed in.** A pull is a checkpoint of its
own in a recording, and a `contrast` pair has to checkpoint at the same
ticks (`tests/sessions/README.md`).

## The automap on this program

`--seam automap` and `Tab` at the party's bar (`docs/seams.md` §10):

| run | what it shows |
|---|---|
| leg 3 with `--press Tab@8200` | the panel over the roster from `MOVE` on, drawn again at every step of the walk: the mayor's house and its door at `3,4`, the street south and the east street with the five doors on it (`7,7`, `10,7`, `14,7` north, `7,8`, `10,8` south) and the gate east of `15,8`; the roster for the gate's question, and in the ruins the panel on the first page of their script's numbering |
| leg 15 with `--press Tab@8200` | the town as above, then the ruins on the pages of the line's squares, each square as the stored square under it draws it: the entrance mark at `0,15`; the door just walked through a leaf on both sides; `9,15` to `9,16` the page below; on the turned piece the mark the line's square and facing and the walls turned with it; the walk back drawing the squares the walk out drew, and the town's panel again west of the gate (`docs/seams.md` §10) |
| leg 4 with `--press Tab@8200` | the panel through the pacing on the first street, the fight's screen over it, and the panel at `0,13` when the treasure screen is left |
| leg 15's walk out and on to stored `12,7` with `--press Tab@8200 --save-sidecars`, then `E`, `E`, `S`, `C`, `N`, `E` | `AFMAPC.DAT` at version 3, 2,512 bytes; slot C loaded, `Tab` draws the turned piece's page from it and the walk back goes on drawing |
| leg 1 with `Tab@8100`, then `Up`, `Tab`, `Down`, `Tab`, `Tab`, `8`, `Tab`, 150 frames apart | `Up` taken with the panel up, the member unchanged; `Down` and `8` with it down step the member, and each next `Tab` draws the panel |
| leg 1 with `Tab@8100`, then leg 2's `E`, `V`, `E`, `E` 300 frames apart from 8,250 | the camp's bar and the sheet take the panel away with the roster drawn, and `EXIT` from camp brings it back |
| leg 1 with `Tab@8100`, `A`, `A` | `AREA` draws the program's own overhead map in the viewport beside the panel, and `A` again the 3D view |
| leg 1 with `--save-sidecars`, `Tab@8100`, `MOVE`, three `Up`, `E`, `E`, `S`, `C`, `N` | `AFMAPC.DAT` written beside the saves (`docs/hosts.md` §6); slot C loaded and `BEGIN ADVENTURING`, `Tab` draws the mayor's house from it |

## The message pause on this program

The program's own pause after a message is 241 ms at `--speed 386` and
the game speed slot A holds, 4 (`docs/seams.md` §10 has the
measurement). A line of a fight stands for that pause and no longer, 14
frames. Leg 4 makes 49 pauses, the first two as the encounter opens and
the rest in the fight.

| run | what it shows |
|---|---|
| leg 2 with `--seam message-pause` | no pause is reached: the opening's pages wait for `Return`, the script's print is reached 37 times, and the run is leg 2's |
| leg 4 with `--seam message-pause` | each line of the fight stands 90 frames, 1,500 ms, and the death of an enemy keeps its 241 ms. **Leg 4's keys no longer fit the fight**: a key pressed during a pause ends that pause and is used up, so eight of them (a step of the pacing, three `Q` and the four that answer the fight's end) do that and nothing else, and the fight is still being fought at the leg's last frame |
| `--press Return@600 --press D@900`, with `--seam message-pause` | the demonstration plays to its end, by frame 7,600 where it ends by frame 4,400 without the seam. The program runs it at game speed 9, so its own pauses are 540 ms, and each of its lines is held by a few of them in a row |

A script written for the seam leaves a second and a half after each
line, or presses a key to end the pause and another for the command.

## The journal on this program

What the game cites goes on the log, and with `--save-sidecars` the log
goes beside the slot's save. The party's bar has no room for an eighth
word, so **`NOTES` takes `AREA`'s place on it**: `MOVE NOTES CAST VIEW
ENCAMP SEARCH LOOK`. `N`, or `Return` with the word lit, opens the log;
`A` is then none of the bar's commands, and the move mode's bar is the
program's (`docs/seams.md` §10). `--journal-store` names the store; the sessions
use `tests/visual/secret-reader-store.txt`, entry 37 in this project's
own words with a synthetic picture.

| run | what it shows |
|---|---|
| leg 2 with `--seam journal` | the seventh page of the opening, at frame 5,122, puts entry 37 on the log, unread; one `journal-seen` callout, the message box reached 39 times, and the run is leg 2's |
| leg 1, then `E@8200 S@8500 C@8800 N@9100`, with `--seam journal --save-sidecars` | the camp's save into slot C writes `AFSEENC.DAT`, one row: entry 37, unread |
| leg 0, then `Down@1500 Down@1560 Return@1620 C@2100`, over that disk, with the same two flags | loading C reads `AFSEENC.DAT` back (`reads=1`); four `Down`, `Return` and `D` save the party to D from the party menu, and `AFSEEND.DAT` holds the same row |
| leg 1, then `N@8100 Return@8400 N@8700 N@9000 E@9300 E@9600`, with `--seam journal` and a store | the log at 8,150 with entry 37 unread; the entry's first page at 8,450 (`NEXT EXIT 1/3`), its second, then its picture (`PREV EXIT 3/3`); the log again, and at 9,650 the adventuring screen pixel for pixel the one at 8,050 above the bar's row, the bar the same with `NOTES` lit |
| the same run, then `Return@9900 E@10200 M@10500 N@10800 Left@11100 E@11400` | `Return` on the lit `NOTES` opens the log at 9,950 and `E` closes it, the screen at 10,250 the one at 9,650 to the pixel; the move mode's bar `EXIT`, where `N` changes nothing, `Left` turns the party (3,3 S to E) and `E` ends it |
| leg 1, then `A@8100`, with `--seam journal --watch 89D6`, and without the seam | with it `89D6` stays `00` and the screen is the same; without it `A` sets it to `01`, the overhead view |
| leg 1 with the store ingested from Steam's PDF (`--journal`) and the first row's keys | entry 37's two pages of the journal's own text and its picture |

## The copy protection

The program has two questions, and **the Steam copy asks neither**: in
its files as sold, each is stepped over by an unconditional jump.

| question | where the program would ask | what the Steam copy has there |
|---|---|---|
| a word of a journal entry, by entry and word number | the boot, between the titles and the party menu: a far call at image `0x02C9` into overlay 6 (file offset 22470, 766 bytes) at `+0x0112`, its answer compared with 1 | the instruction before the call, at image `0x02C7`, is a jump over it and over a four-entry check that follows it |
| a word after a heading of the rule book, by page | the camp's `SAVE`, in overlay 19 (file offset 106597, 13089 bytes): the command counts itself in a word of the area record (`+0x278`; the record's far pointer is at `67C6`), compares the count with thirty, and the call to the question is at `+0x320B` | the instruction after that compare, at `+0x3208`, is a jump over the call and the count's reset |

So no seam answers anything on this copy, and no leg types a word.
Driven: the boot (leg 0) and a save at camp ask nothing. The GOG copy's
program and overlay file are Steam's byte for byte, so it asks neither
question either. A copy whose two jumps are conditional would ask, and the code-wheel seam's two games are
where that mechanism is (`docs/seams.md` §10).

## The sessions

Thirty-seven, `secret-*`, at `--speed 386` and `--wall none`: `secret-load`
on the disk `secret-save` leaves (the `secret-saved` snapshot), and the
rest on the Steam copy with its shipped slots (`secret-slots`).

| session | seams | relation | what it is |
|---|---|---|---|
| `secret-boot` | | | leg 0 |
| `secret-camp` | | | leg 2 |
| `secret-new-party` | | | leg 5 |
| `secret-buy` | | | leg 7 |
| `secret-vault` | | | leg 8 |
| `secret-save` | | | leg 12, the save |
| `secret-load` | | | leg 12, the load |
| `secret-heal` | cheat-wound-party, pulled | | leg 11 |
| `secret-camp-cheats` | the three cheats, none pulled | `identical secret-camp` | no point acts: all 106 checkpoints |
| `secret-camp-wound` | cheat-wound-party, pulled | `contrast secret-camp` | 93 of 106 identical, divergent from tick 169,050,886, the pull |
| `secret-fight` | | | leg 4 |
| `secret-fight-cheat` | cheat-invulnerable | `contrast secret-fight` | 339 of 371 identical, divergent from tick 514,192,302 |
| `secret-boot-font-sans`, `secret-boot-font-chisel` | a text face | `contrast secret-boot` | 1 of 15 identical: the face is seen from the first text on |
| `secret-fight-kill` | cheat-kill-all, pulled | `contrast secret-fight` | 324 of 371 identical, divergent from tick 491,204,086, the pull |
| `secret-camp-message-pause` | message-pause | `identical secret-camp` | no pause is reached: all 106 checkpoints |
| `secret-fight-pause` | message-pause | `contrast secret-fight` | 315 of 371 identical, divergent from tick 476,011,182, in the two pauses that open the encounter |
| `secret-aim` | | | leg 9 |
| `secret-name` | | | leg 10 |
| `secret-fight-controls` | modern-controls | | leg 4, no key of the seam's pressed; the roster numbered and every selection a block, so 27 of 371 checkpoints are `secret-fight`'s: different from tick 11,951,486, the title's lit `PLAY`, and the same again in stretches with nothing the seam drew left, the first from 114,563,246. No relation describes a difference that does not last |
| `secret-aim-controls` | modern-controls | | the aim watched at every key of the aim; 27 of 406 `secret-aim`'s, the same way |
| `secret-name-controls` | modern-controls | | the line editor watched from entry to return; the lists' blocks, so 13 of 40 `secret-name`'s, the same again while the name is typed |
| `secret-pick` | | | leg 13 |
| `secret-pick-heroes` | modern-controls | `contrast secret-pick` | 5 of 68 identical, divergent from tick 11,951,486, the title's lit `PLAY`; the roster numbered, the digits choose |
| `secret-modify` | | | leg 14 |
| `secret-modify-block` | modern-controls | `contrast secret-modify` | 5 of 53 identical, divergent from tick 11,951,486; Modify's fields white blocks |
| `secret-boot-journal` | journal | `identical secret-boot` | the watch never reached: all 15 checkpoints |
| `secret-camp-journal` | journal | `contrast secret-camp` | entry 37 cited by the opening and logged, `NOTES` on the party's bar and never chosen: 85 of 106 identical, divergent from tick 152,744,366, the bar's first draw |
| `secret-town-journal` | journal | `contrast secret-town` | `NOTES` on the party's bar, never chosen, the move mode's bar the program's, and the walk step for step: 85 of 138 identical, divergent from the same tick |
| `secret-notes-keys` | | | leg 1, then the next session's keys with no seam |
| `secret-notes` | journal, a store | `contrast secret-notes-keys` | `N` at the party's bar, entry 37 read to its picture, the log again by `Return` on the lit `NOTES`, then the move mode: 85 of 131 identical, divergent from tick 152,744,366 |
| `secret-town` | | | leg 3, to the ruins at frame 11,600 |
| `secret-camp-automap` | automap | `identical secret-camp` | `Tab` never pressed: all 106 checkpoints |
| `secret-town-map` | automap | `contrast secret-town` | `Tab` on `MOVE`'s frame, 8,200: 90 of 138 identical, divergent from tick 163,085,086 |
| `secret-ruins` | | | leg 15, out through four of the ruins' script's doors and back into the town, 279 checkpoints |
| `secret-ruins-automap` | automap | `identical secret-ruins` | `Tab` never pressed, the town and the ruins' pages recorded: all 279 checkpoints |
| `secret-ruins-map` | automap | `contrast secret-ruins` | `Tab` on `MOVE`'s frame, 8,200: 90 of 279 identical, divergent from tick 163,085,086 |
