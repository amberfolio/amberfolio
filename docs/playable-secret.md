# Playable: Secret of the Silver Blades

How to drive a player-supplied copy of Secret of the Silver Blades from
the party menu into the game, leg by leg: the keystrokes, what the run
shows, and the traps. `docs/playable.md` is the same for Pool of Radiance
and has the method in full; `docs/first-light.md` stops at the party
menu. Nothing in this repository can run any of it (PLAN.md §6).

The legs here are the ones driven so far. What is still to be driven is
on #608: a new party, the shops, the temple and the training hall, the
ruins past their first street, and where its import looks.

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
on this program are the three debug cheats, the two text faces and the
message pause (`docs/seams.md` §10). Sixty
frames to the virtual second; `TICKS = FRAME * 19,886` at `--speed 386`
is close enough for `--until`.

Every script below is on a fresh copy of the Steam disk with its shipped
slots A and B (the `secret-slots` snapshot). Snapshot the directory and
restore it between runs.

The `.txt` beside a still is the screen's text (`docs/hosts.md` §10),
which reads on this program. The scene (`.scene.json`) does not yet.

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
| the adventuring bar | `MOVE AREA CAST VIEW ENCAMP SEARCH LOOK` | `Return` on the lit `MOVE` starts the program's own move mode: the bar is `EXIT`, `Up` steps, `Left` and `Right` turn, `E` ends it |
| the treasure bar | `VIEW TAKE POOL SHARE EXIT` | `E` leaves; with treasure left the program asks whether to go back, and **`N` is the one that leaves** |
| the camp bar | `SAVE VIEW MAGIC REST ALTER FIX EXIT` | letters; the program has its own `FIX` |
| a character's view | a sheet over `HEAL CURE EXIT` (what a member can do varies) | `E` leaves |
| a fight | the field, the member whose turn it is, `MOVE VIEW AIM QUICK DONE` | `Q` gives that member to the computer for the fight, so a party of six takes six |
| the end of a fight | `CONTINUE BATTLE: YES NO`, when no enemy stands | `N` takes the win |

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
the party through. **A 0 there is not always shut**: walls of kinds 3
and 4 with a 0 stopped the party, and a wall of kind 8, drawn as a
wooden door, let it through with a 0. Which kinds are doors has not been
worked out past those. A route computed from the planes, eighteen keys
from the mayor's house to the east gate with no wall on the way, walked
square for square.

**The status line's square is not always the stored one.** In the town
the two agree. The ruins are wider than sixteen squares, and the map at
the pointer is a part of them that the program changes as the party
crosses out of it: the party arrives at stored `11,2` with the line
saying `0,15`; at stored `14,2` the line says `3,15`; and one step east
of that, through a door, the party is at stored `10,3`, and a step
further at stored `11,3` with the line saying `5,15`.

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
  **One of the pages cites a journal entry by number**, which is where
  S3's journal reader will start from. The mode byte is 4 from the first
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
one east of stored `14,2` leads out to a street. Nothing further of the
ruins has been driven.

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

## The copy protection

The program has two questions, and **the Steam copy asks neither**: in
its files as sold, each is stepped over by an unconditional jump.

| question | where the program would ask | what the Steam copy has there |
|---|---|---|
| a word of a journal entry, by entry and word number | the boot, between the titles and the party menu: a far call at image `0x02C9` into overlay 6 (file offset 22470, 766 bytes) at `+0x0112`, its answer compared with 1 | the instruction before the call, at image `0x02C7`, is a jump over it and over a four-entry check that follows it |
| a word after a heading of the rule book, by page | the camp's `SAVE`, in overlay 19 (file offset 106597, 13089 bytes): the command counts itself in a word of the area record (`+0x278`; the record's far pointer is at `67C6`), compares the count with thirty, and the call to the question is at `+0x320B` | the instruction after that compare, at `+0x3208`, is a jump over the call and the count's reset |

So no seam answers anything on this copy, and no leg types a word.
Driven: the boot (leg 0) and a save at camp ask nothing. A copy from
another seller is measured when one is here (#606); a copy whose two
jumps are conditional would ask, and the code-wheel seam's two games are
where that mechanism is (`docs/seams.md` §10).

## The sessions

Eleven, `secret-*`, each on the Steam copy with its shipped slots (the
`secret-slots` snapshot) at `--speed 386` and `--wall none`.

| session | seams | relation | what it is |
|---|---|---|---|
| `secret-boot` | | | leg 0 |
| `secret-camp` | | | leg 2 |
| `secret-camp-cheats` | the three cheats, none pulled | `identical secret-camp` | no point acts: all 106 checkpoints |
| `secret-camp-wound` | cheat-wound-party, pulled | `contrast secret-camp` | 93 of 106 identical, divergent from tick 169,050,886, the pull |
| `secret-fight` | | | leg 4 |
| `secret-fight-cheat` | cheat-invulnerable | `contrast secret-fight` | 339 of 371 identical, divergent from tick 514,192,302 |
| `secret-boot-font-sans`, `secret-boot-font-chisel` | a text face | `contrast secret-boot` | 1 of 15 identical: the face is seen from the first text on |
| `secret-fight-kill` | cheat-kill-all, pulled | `contrast secret-fight` | 324 of 371 identical, divergent from tick 491,204,086, the pull |
| `secret-camp-message-pause` | message-pause | `identical secret-camp` | no pause is reached: all 106 checkpoints |
| `secret-fight-pause` | message-pause | `contrast secret-fight` | 315 of 371 identical, divergent from tick 476,011,182, in the two pauses that open the encounter |
