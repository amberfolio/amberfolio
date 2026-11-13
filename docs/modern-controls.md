# Modern controls (#423, #425, #434, #439, #453, #455, #473, #479)

The fact tables of the `modern-controls` seam, the handlers each rests
on, and what each piece does on each program. It is `docs/seams.md`
§10's entry for the seam, kept in a document of its own because it is as
long as the rest of §10 together, as the explored overlay's
(`docs/explored-overlay.md`) and the journal's (`docs/journal.md`) are.

Everything below is an address, an offset, a length, arithmetic or a
measurement. No game code, data, text or byte sequence appears here.

`seam_modern_controls.cpp`. One seam to a player: the keys and the colours a
later game would have, switched on together. Seven sources hold the pieces,
each with the facts it rests on and the handlers that act on them, and each
hands the registration the points it owns; the registration holds the id, the
`about`, the fingerprint and the union of the points. Not a PLAN.md §5 item:
players' requests, built after v1's six. A setting; keys and looks. Nothing
to pull.

| piece | source | what a player gets | points |
|---|---|---|---|
| list arrows | `seam_list_arrows.cpp` | Up and Down step the pick-lists, the party-member picker and the selected member, as Home and End do | 3 |
| bar keys | `seam_bar_keys.cpp` | Left and Right step a command bar's highlight, Enter takes it, Esc answers No | 1 |
| menu cursor | `seam_menu_cursor.cpp` | a cursor on the main menu | 2 |
| hero keys | `seam_hero_keys.cpp` | at the main menu, the number row's 1 to 8 select a member and the party list shows each number | 5 |
| edit keys | `seam_edit_keys.cpp` | an extended key types nothing at a text prompt | 1 |
| selection block | `seam_select_yellow.cpp` | every selection a block, yellow (white on Curse of the Azure Bonds), a key letter a white one, with a margin all round | 11 |
| move mode | `seam_move_mode.cpp` | on the party's own bar walking is a mode: `Move` starts it, `Exit` ends it, and in between Left and Right step the bar; `Area` is gone | 5 (3 on Curse) |
| text entry | `seam_text_entry.cpp` | nothing on the screen: a host can ask whether the program is reading a line of text | 2 |
| aim watch | `seam_aim_watch.cpp` | nothing on the screen: a host's scene says where the combat aim cursor is, who is under it and the range | 5 |

The sections below keep each piece's facts under the name it was a seam
under (`list-arrows`, `bar-keys`, `menu-cursor`, `hero-keys`, `edit-keys`,
`select-yellow`), and the move mode's under `move-mode`. **None of those is
an id**: `--seam bar-keys`, a config
line or a recording naming one is refused as an unknown seam
(`unknown_seam`), and "the seam on" or "off" in a piece's section means
`modern-controls` on or off. The `fired`, `reached` and `declined` counts
quoted below were taken per piece; the engine reports them for the one seam,
summed over the pieces.

- **One definition, the union of the points, in one order.** The
  thirty-six points are the pieces' own, in the order list arrows (3), bar
  keys (1), menu cursor (2), hero keys (5), edit keys (1), selection block
  (12), move mode (5), text entry (2), aim watch (5). Five pieces have a point at the same instruction,
  overlay 25's key read at `0x0572` (list arrows, bar keys, menu cursor, hero
  keys, move mode), and the engine offers an address's points in table
  order, so the order is behaviour: the move mode is last, and rewrites the
  party bar's keys after every other piece has passed on them. The point
  count with every seam on is sixty-eight, one face counted (`max_points` is
  ninety-six).
- **Two programs, several modules.** Every piece keys on the baseline
  edition (`docs/seams.md` §5), and nine of them on Curse of the Azure Bonds too, with
  points of their own (below), so the definition names both. The modules are the
  pieces': overlay 25 for five of them, overlay 16 for the menu cursor's loop
  call, overlay 19 for Modify's score, overlay 14 for the move mode's party
  bar (15 on Curse), overlay 13 for the aim watch, the resident image for the rest, and
  each point names its own.
- **A piece cannot ask another.** The menu cursor took its colours from the
  selection colour; with the two one seam the answer is always yes and the
  cursor's row is always a block in the selection's colour with a white
  key. The journal's reader asks
  `modern_controls_on(box)` (`seam_builtin.h`) for its bars' keys and
  highlight and for its cursor row (`docs/seams.md` §10 on the journal).
- **Off is the plain machine, on is all eight.** There is no setting between,
  so no piece's handlers run with another's off.
- **A seam is armed while any module its points name is resident** (`docs/seams.md` §4),
  and this one has points in the resident image, so with the seam on the
  panel reads `on armed` from the first step. A piece whose overlay (25, 16,
  19 or 14) is out does nothing until it is loaded; `fired` counts what the
  others did meanwhile.

**State**: the move mode's few words (its section); the menu cursor is held
in an enable byte the program owns while it is drawn. **Host services**: none. **Keys**: each
piece's, in its section, and on the key card as rows naming `modern-controls`
(`hosts/common/src/key_card.cpp`).

**Fidelity**: off, the engine is not consulted (`docs/seams.md` §7). On, the seam is never
idle: the main menu's cursor is drawn from the first frame the menu is, the
roster's numbers from the first time its party list is, the party's bar in
menu mode from the first time it is drawn, and a highlight as a block from the
first time one is, so a run with it on differs from the
plain one at the first main menu whatever keys are pressed, and it has no
`identical` pair (`docs/seams.md` §8.5, as for the faces). `quiet-modern-controls` is a
`contrast` to `quiet` (58 of 90 checkpoints identical, divergent from tick
147,668,400, the first frame the main menu is drawn in) and
`quiet-all-on-sight` one to `quiet-all` (the seam beside every other seam
but the faces). Each piece's pairs are in its section, and
`tests/sessions/README.md` lists them all. Unit: `SeamModernControls.*` (the
one definition) and each piece's suite.

**On Curse of the Azure Bonds** (#555) the seam is available with nine
of its pieces: the list arrows, the bar keys, the menu cursor, the hero
keys, the edit keys, the selection block, the move mode, text entry and
the aim watch, each the same mechanism with that program's facts in a
row of its own (`list_arrows_facts`, `bar_keys_facts`,
`menu_cursor_facts`, `hero_keys_facts`, `edit_keys_facts`,
`select_yellow_facts`, `move_mode_facts`, `text_entry_facts`,
`aim_watch_facts`, and `party_select::facts` for the bars that are not
raw) and its points in the definition's `programs`, thirty-five of
them, in the baseline's order (the list arrows and the selection block
each with one more, for Modify's name field). The facts more than one piece reads are
the profile's (`program_profile.h`): among them the party summary's
module (`summary_overlay`: the roster drawer, the hit points' draw and
the picker, resident on Pool of Radiance and overlay 25 here), the
roster drawer's stub entry (`screen_paragraph`, `roster_draw_offset`),
the character sheet's module (`view_overlay`), the save slots'
(`slots_overlay`) and the colour of a selection (`selection_colour`:
white here, the program's own highlight, where the baseline's is
yellow).

| fact | Pool of Radiance | Curse of the Azure Bonds |
| --- | --- | --- |
| the menu overlay: the menu-bar routine, the pick-list, the Yes/No prompt | overlay 25 (file offset 182479, 4682 bytes, word `0x3C60`) | overlay 27 (file offset 245901, 4957 bytes, digest `2447cf5d…1d57`, word `0x1050`) |
| the routine; its read call | `0x03BD`; `0x0572` | `0x03D8`; `0x05F1`, a far call to the program's key read at image `0x6812` (after the key-pending call at `0x05E5`, to image `0x86EB`) |
| the frame: return IP and CS, raw mode; Enter-allowed, group count, bar copy (`string[40]`) | `BP+2`, `+4`, `+0x0C`; `BP-0x8F`, `-0x8E`, `-0x53` | the same |
| the bar highlight (one-based, every bar's) | DGROUP `0x6B2B` | DGROUP `0x72AE` |
| the pushback slot (an extended key's second half) | DGROUP `0x8501` | DGROUP `0x8CBF` |
| Enter at a drawn bar | handed back as `0x0D`, and many callers ask again | **handed back as the highlighted group's first letter**, by the routine itself |
| the pick-list's return; its out-parameter | overlay 25 `0x0FE0`; `BP-0x57` | overlay 27 `0x10F3` (a near call from inside the overlay, `CS` pushed first); `BP-0x57` |
| the party-member picker's return; its out-parameter | image `0x38AA` (resident); `BP-0x2B` | overlay 25 (file offset 226981, 13331 bytes, digest `58601d36…a6a6`, word `0x0EE0`) `0x2EE5`; `BP-0x2B` |
| the party cursor | resident, through `0085:0052`: `G` back (head to tail), `O` forward (the tail stays), any other key to the head | overlay 20 `0x32CF`: the same rule |
| the Yes/No prompt's call | overlay 25 `0x111E` | overlay 27 `0x1231` (near, from inside the overlay) |
| the script runner's call | overlay 7 `0x16EB` (word `0x2C0`) | overlay 8 `0x1811` (word `0x4D0`) |
| the line editor; its read's return; its one return | image `0x7A73`; `0x7AC0`; `0x7BF3` (`retf 8`) | image `0x53F6`; `0x5447`; `0x5584` (`retf 0Ah`) |
| the aim overlay | overlay 13 (word `0x690`) | overlay 14 (file offset 78696, 18872 bytes, digest `0b4a75f0…e874`, word `0x08E0`) |
| target to target: routine; meeting point; return; attack flag; bar string | `0x2AA0`; `0x2BB6`; `0x2C31`; `BP+8`; `BP-0x2A` | `0x3027`; `0x314C`; `0x31CC`; `BP+0x0A`; `BP-0x2A` |
| manual: routine; the line's meeting point; Target's meeting point; return; attack flag; line byte | `0x2DFA`; `0x2F84`; `0x310A`; `0x3178`; `BP+0x10`; `BP-0x39` | `0x3395`; `0x3522`; `0x36B3`; `0x3721`; `BP+0x12`; `BP-0x39` |
| the party summary's module | the resident image | overlay 25 (file offset 226981, 13331 bytes, digest `58601d36…a6a6`, word `0x0EE0`); its stub entries in paragraph `0xED` |
| the roster drawer; called at | image `0x1307`; `0x0BA:0x0767` | overlay 25 `0x0798`; its stub entry `0x00ED:0x002A` |
| the drawer's frame: column, row, member; the heading's copy | `BP-5`, `BP-6`, `BP-4`; `BP-0x0E` | the same |
| the drawer's points: the row cleared; the name paths meet; the armour class's column pushed; the heading copied | image `0x138F`; `0x13CB`; `0x140F`; `0x135A` | overlay 25 `0x0830`; `0x086F`; `0x08B3`; `0x07FB` |
| the main menu's call into the routine (the one caller a digit selects at and the menu cursor moves at) | overlay 16 (word `0x790`) `0x02FD` | overlay 18 (word `0x0A80`) `0x0393`: raw, `G` and `O` to the party cursor (overlay 20 `0x32CF`) and then the drawer |
| the glyph blitter: entry; its call, first pushed to last | paragraph `0x709` `0x01DF`; column, row, colour, count, character, fold (`retf 0Ch`) | paragraph `0x4CE` `0x01DF` (image `0x4EBF`); column, row, colour, **paper**, count, character, fold (`retf 0Eh`) |
| the string routine: entry; its call's colour, paper, row, column from the top; its call into the blitter, the instruction after | `0x0626` in the blitter's paragraph; `+4`, none, `+6`, `+8`; image `0x7724` | image `0x5029`; `+6`, `+4`, `+8`, `+0x0A`; image `0x509B` |
| the region clear the cut is made with | paragraph `0x3F1` `0x0137` | the profile's, image `0x17FD` (paragraph `0x133` `0x04CD`), the same four words |
| the bar leaf's glyph calls: the highlighted group's, a letter's, the rest's | overlay 25 `0x0273`, `0x02CA`, `0x0305` | overlay 27 `0x0285`, `0x02DF`, `0x031D`; the leaf's and its caller's frames the same |
| the pick-list's highlight-on string call | overlay 25 `0x09FD`; the colour at `+0x20` of its context | overlay 27 `0x0AFC`; the same |
| how the program draws its highlight | the group's characters and the row in `color_hi` on black | **as paper**: black ink on `color_hi`'s paper, a block in the bar's bright already |
| the roster's selected member's string call | image `0x13AF` | overlay 25 `0x0853` |
| Modify: a score's colour chosen; its string calls; the hit points' colour chosen; their string call | overlay 19 `0x0918`; `0x0976`, `0x0A41`; image `0x153F`; `0x155E` | overlay 20 (file offset 159837, 14697 bytes, digest `b1c2646b…d739`, word `0x0B00`) `0x09C5`; `0x0A2A`, `0x0AF8`; overlay 25 `0x09E6`; `0x0A08`. The locals and the highlighted argument where Pool of Radiance's are |
| Modify's name field: the highlighted name's string call; the cursor's letter or mark; the instruction after the field's second key read | none: the editor has no name field | overlay 18 (word `0x0A80`) `0x2775`, the name from the selected member's record in light magenta on black at row 1, column 1, pushed as immediates; `0x27AF` and `0x27E5`, white on black over it; `0x295A`, which stores an extended key's scan code |
| the blitter's EGA row writes | image `0x74A2`, `0x74C8`, a store of DL to each of two pages | one: image `0x5A93`, the bit mask out of AH (the ink through it, the paper through its complement), one frame below the blitter's, in a path called near; one page, `0xA000` |
| the step to the next cell; the fill's epilogue; the picture drawer's epilogue | image `0x7667`; `0x71FB`; `0x618C` | image `0x4F98` (the EGA path's; the program's other adapters have their own); `0x4E4B`; `0x3542`. The fill's and the picture's frames the same |

The modules' digests are the overlay file's bytes at the records' file
offsets, each record found once in the image (overlay 17's at `0x0A10`,
20's at `0x0AF0`, 25's at `0x0ED0`). Every call above was read from its
instruction's bytes in the store copy's `GAME.OVR` and image.

The modules' words are the stub records' (the record's file offset and
length, then the word sixteen bytes in), each found once in the image.
Every call site of the routine was read: 41, every one a far call through
overlay 27's stub entry (`0104:002A`, image `0x106A`) except the
pick-list's and the Yes/No prompt's, which are near calls from inside
the overlay; each call's bytes were checked in the store copy's
`GAME.OVR`.

**The callers, by the instruction after their call.** The list arrows'
allowlist, raw callers that hand a raw key to the party cursor and whose
Up and Down do nothing else (each read from its return on: the out-flag
test, then the compares):

| caller | module (word) | return offset |
| --- | --- | --- |
| the camp bar, its Magic bar, its Alter bar, the party-order screen | overlay 16 (`0x9E0`) | `0x24FD`, `0x152B`, `0x1D83`, `0x18BE` |
| the post-combat Take bar, the treasure bar | overlay 6 (`0x460`) | `0x0F87`, `0x1239` |
| the shop's bar | overlay 7 (`0x490`) | `0x07DF` |
| the temple's bar | overlay 5 (`0x430`) | `0x0E91` |
| the script prompts | overlay 8 (`0x4D0`) | `0x1811` |
| the character editor, Modify, at a score (Home and End step the field, not the member; Up and Down do nothing there) | overlay 18 (`0xA80`) | `0x29AA` |

The bar keys' callers that keep Left and Right:

| caller | module (word) | return offset | what an arrow does there |
| --- | --- | --- | --- |
| the adventuring bar | overlay 15 (`0x9A0`) | `0x092C` | turns the party; in menu mode the move mode makes them `,` and `.` itself, after this piece has passed |
| the combat command bar | overlay 9 (`0x580`) | `0x0947` | **starts a move in that direction**, which Pool of Radiance's bar does not: the bar keeps the arrows' scan codes and hands them to the move |
| the combat move loop | overlay 9 | `0x0C17` | moves the fighter |
| the target bar (`Aim:`) | overlay 14 (`0x8E0`) | `0x31CC` | **goes to the manual aim**, which Pool of Radiance's target bar (not raw there) does not |
| the manual aim | overlay 14 | `0x3721` | moves the cursor |
| the character editor (`Modify:`) | overlay 18 (`0xA80`) | `0x29AA` | lowers and raises the score |
| the two share prompts, press Enter | overlay 6 | `0x0D14`, `0x174B` | any extended key ends them (judgement, as on Pool of Radiance) |

The callers with raw mode off whose screen shows the party list, where
Up and Down step the selected member and the roster is drawn again
(#469, `party_select::facts`), each read from its call's pushes (raw
mode `0`) and the screen behind it:

| caller | module (word) | return offset |
| --- | --- | --- |
| a locked door's bar | overlay 15 (`0x9A0`) | `0x0CD7` |
| a stuck door's bar | overlay 15 | `0x0E16` |
| the save slot bar (from camp and from the main menu) | overlay 17 (file offset 112433, 17552 bytes, digest `4b261fff…0704`, word `0x0A20`) | `0x3FA7` |

Curse's camp has no Portraits and Monsters bar (its Alter bar is
raw). Left out as on Pool of Radiance: the load slot bar (overlay 17
`0x38B7`), the Yes/No prompt, the View bar, combat's. And two Pool of
Radiance has no counterpart of, which need nothing of their own: the
main menu's `Add from where?` bar (overlay 18, return `0x36A3`) and a
spell's own questions (fire shield's `Hot Cold` and `Abort spell?`).
Neither is raw, so Left and Right step them and Return takes the lit
command as at any bar, and the routine ends on Escape with no command,
which `Add from where?` takes as `Exit`. Up and Down select no member
there: Add does not use the selection, and fire shield's caster is the
selected member, asked in combat and out.

- **Enter is the program's own.** Curse's routine, with the bar drawn,
  hands a Return back as the first letter of the highlighted group
  (`0x0D` only for a group with no letter), so the Enter table, the
  script runner's allow-Enter byte, the pick-list's and the picker's
  Enter have nothing to do there, and the bar keys do not read Enter.
  Driven with the seam off: Return at the camp bar, `SAVE` lit, opens
  `SAVE WHICH GAME`.
- **Esc** answers No at the Yes/No prompt and at a script prompt whose
  command letters are `Y` and `N`, as on Pool of Radiance.
- **The heroes' numbers** are Pool of Radiance's: the same drawer, frame
  and columns, the number drawn by the program's blitter at the old
  column in white on black paper, the name two columns right, the
  armour class and its heading one right, on the main menu only. A
  digit at the main menu selects as there: Curse's main menu hands `G`
  and `O` to the party cursor and draws the roster again.
- **The selection block.** Curse already draws its own highlight as a
  block, in the bar's bright (white), and the pick-list's row the same
  way. The seam draws every selection as on Pool of Radiance, a block
  with the key letter a white one, **in Curse's own white and not the
  baseline's yellow** (the profile's `selection_colour`, the colour table
  in the selection block's section): at the group's call it takes the
  program's colour off the paper, decides the character's colour as
  there, and hands it to the ink with black paper, and the row write
  inverts the mask for a selection's glyph, so a block is the paper in
  the colour and the letter in black, as the baseline's inverted store
  makes it. A bar's lit word is the white block the program draws, key
  and rest alike, with the margin round it; the roster's selected member,
  Modify's score, the menu cursor's row and the journal's selections are
  white blocks too. Everything outside the lit word is drawn unselected,
  the pick-list's `Exit` too, which the program would have drawn as a
  white block. The margin is the baseline's (`selection_margin.h`), on
  the one page, and reads in white as in yellow: it parts the block's
  letters from its edge, and the block from the frame and the row above
  it. A string a seam draws in a batch with `selection_mark` is a block
  the same way: the string routine's colour word, at `BP+0x0C` of its
  frame, carries the mark.
- **Inside a batch.** The roster drawer is overlaid, so its points are
  in a module the program moves; the engine offers an `inside_calls`
  point there as anywhere, found through the program's word at the step
  (`docs/seams.md` §3), so the roster the automap gives back, and the one
  a step at a bar that is not raw draws, carries the numbers and the
  block too.
- **The menu cursor** is Pool of Radiance's design on the same loop: the
  main menu's commands are records with an enable byte each, which the
  loop's redraw writes, and the cursor lives in Drop's or Load's while it
  is drawn (the facts are in the menu cursor's section). The bar of
  thirteen letters the loop hands the routine is only its key table, with
  no colour, as on Pool of Radiance.
- **Modify's name field**, which Curse's editor has and Pool of
  Radiance's does not. Highlighted, the program draws the name in light
  magenta and then the letter under its cursor in white on black over
  it (a mark past the end); the seam makes the name's colour the
  selection's at its call, so the name is a white block, and leaves the
  cursor's call alone, so the cursor is the gap in the block. The field
  reads its own keys, outside the menu-bar routine: its Left and Right
  move the cursor, Home and End step the field. **Up and Down step the
  field** there and at every score, Up to the one before and Down to the
  one after, as Pool of Radiance's editor does with its rows: at a score
  through the editor's call into the routine (the list arrows' table
  above), in the name at the instruction after the second read, where
  `0x48` becomes `0x47` and `0x50` `0x4F`. With the seam off Curse's
  editor drops both.
- **Driven, the pieces this port added**, on the Steam copy, slot A,
  `--wall none`, stills read: the main menu's party list numbered 1 to 6
  in white, the names two columns right, the armour class and its
  heading one right, `HP` where it was, Mathew a white block; `3` at the
  main menu selects Travis, and the camp's list beside the viewport is
  the program's own but for Travis's block; the camp bar's `SAVE` a
  white block, its margin painted above over the frame's black and
  right, the other words white and green; at the save
  slot bar from camp, Down, Down, Up, Up select Mark, Travis, Mark,
  Mathew, the roster drawn again each time with the block, the bar
  still up; creation's `PICK RACE` row a block that Down moves
  to `ELF`, and the list's bar `SELECT EXIT` with `SELECT` lit; Modify's
  strength `18(00)` and, after six Ends, its hit points a block. The
  main menu with no party: `CREATE NEW CHARACTER` a white block from the
  first frame, Down, Down, Down, Down move it to `ADD`, `LOAD`, `EXIT`
  and round to `CREATE`, Up, Up to `EXIT` and `LOAD`, and Return there
  asks `LOAD WHICH GAME`; with slot A's party, nine rows, Down eight
  times walks every row and a ninth wraps, and Return on `BEGIN
  ADVENTURING` begins.
  With the seam off the bar's and the list's highlight are the program's
  white block, the selected member is white letters, and the list is not
  numbered.
- **Driven** on the Steam copy, slot A, `--wall none`: at the camp bar
  Down, Down select Mark and then Travis and Right, Right light `MAGIC`
  with Travis still selected (seam off: the selection goes back to
  Mathew at every arrow); Escape at `QUIT TO DOS` answers No (seam off:
  the question stays); at creation Down lights `ELF` at `PICK RACE` and
  Down, Up, Down step the class list; `T`, Right, `O`, Up, `R`, Down,
  `K`, Left saves `TORK` (seam off `TMOHRPKK`); View's Trade opens the
  picker, and Down, Down, Up select Mark, Travis, Mark; Right and Left
  at the brawl's command bar give the same stills on and off, the Right
  starting Travis's move. The aim watch, read from the seam's words:
  target mode with the range shown at the target bar, manual after `M`,
  none after a key; text entry reading at `CHARACTER NAME:` and not
  before or after.
- **Fidelity**: three of the pieces draw on sight on Curse too, the
  menu's cursor, the numbers and the block, so there is no `identical`
  for the seam here either: `curse-quiet-controls` `contrast curse-quiet`
  (19 of 58, then divergent from tick 48,382,638, the title's bar, the
  first highlight drawn, where the block's margin is the difference;
  `curse-quiet`'s two Lefts and its Up at the party's bar step the
  highlight to `Search` and select Philippe, its Returns take `Search`,
  and the party stays at 7,13); `curse-camp-arrows` `contrast
  curse-camp-keys` (19 of 81), `curse-name-arrows` `contrast
  curse-name-keys` (19 of 118) and `curse-menu-cursor` `contrast
  curse-menu-keys` (19 of 49) and `curse-modify-arrows` `contrast
  curse-modify-keys` (Modify's fields stepped and the name field; 19 of
  161), from the same tick. Each is its
  baseline's keys at their ticks, recorded again with the seam on; the
  move mode's pair is in its section. Unit: `SeamModernControlsOnCurse.*`,
  `SeamMoveModeOnCurse.*`, `SceneOnCurse.*` (the aim watch's frames
  read through the scene),
  `JournalReaderOnCurse.TheLogsCursorRowIsAWhiteBlockUnderModernControls`,
  `SeamCallProgram.FindsAnInsideCallsPointInAModuleTheProgramMoves`.

## The list arrows (#423, #435, #447, #469)

`seam_list_arrows.cpp`. Not a PLAN.md §5 item: a player's request, built
after v1's six. A setting, no key, nothing to pull. The module, the read
point at overlay 25 `0x0572` and the test of who called the menu-bar
routine are shared with the bar keys (`seam_menu_bar.h`).

| point | module | what the handler does |
|---|---|---|
| image `0x0FE0` of overlay 25, the instruction after the pick-list's call into the menu-bar routine | overlay 25, through the manager's word | with the routine's out-parameter set, AL `0x48` becomes `0x47` and `0x50` becomes `0x4F` |
| image `0x38AA`, the instruction after the party-member picker's call into the same routine | the resident image | the same, with the out-parameter in the picker's frame |
| image `0x0572` of overlay 25, the call into the program's key-read routine inside the menu-bar routine (the point `bar-keys` has too, `seam_menu_bar.h`) | overlay 25, through the manager's word | reads the keystroke at the head of the BIOS ring before the program does. An extended Up or Down, from a caller in the allowlist below, becomes Home or End; the keypad's 8 or 2, from the same callers, becomes its 7 or 1 |

| fact | value |
|---|---|
| the module | overlay 25: file offset 182479 (`0x2C8CF`), 4682 bytes (`0x124A`), digest `175454bc…3901`; the program's load-segment word is at image `0x3C60` |
| the key, at both points | AL, as the routine returns; the next instruction stores it |
| the out-parameter | a byte the caller passes by address: **1** a raw key (an extended key's scan code, or a translated keypad digit), **0** a bar command or a confirmation. The list's is at `BP-0x57`, the picker's at `BP-0x2B` |
| what the list acts on | `0x47` up a row, `0x4F` down a row, `0x49` and `0x51` a page when one is there; Up and Down `0x48`, `0x50` fall through to "ask again" |
| what the picker acts on | `0x4F` the next member (the head after the last), `0x47` the previous one (the tail from the head) |
| who calls the list | driven: race, gender, class and alignment at creation, the spell list, the Items screen's list; from its callers, not driven: the shops, training, coin selection, the camp's Display |
| who calls the picker | Trade's receiver, "Cast Spell on whom", a script's party pick |
| the party cursor | the resident thunk at `0x108A2`, called through the stub at `0085:0052` (`9A 52 00 85 00`): `G` steps the selected member back, `O` forward, **any other key moves the selection to the head of the party**. It is reached from sixteen call sites in twelve routines (found by searching GAME.OVR and the image for the far call); nine of those routines are the allowlist below |
| the keystroke at the third point | the head word of the ring at 40:1Eh, scan code high and character low: Up `0x4800`, Down `0x5000`, Home `0x4700`, End `0x4F00`. The keypad's 8 and 2 with Num Lock on carry the same scan codes **and a character**: `0x4838` and `0x5032`, which is how the seam tells them from Up and Down; it writes `0x4737` and `0x4F31` (the keypad's 7 and 1) over them. The number row's digits (scan `0x02` to `0x0B`) are never touched |

- **The out-parameter is read first, and only when AL is an arrow.** With
  it **clear**, `0x50` is the bar's own `P`, the Prev command, and is
  left alone (`SeamListArrows.LeavesTheBarsOwnPrevAloneWhereTheKeyIsACommand`).
  A byte that is neither zero nor one is not the frame these facts
  describe: the handler declines and touches nothing. A list driven with
  Home and End alone costs the seam not one byte read.
- **The scope is positive, not inferred.** The arrows move the party in
  3D, in the wilderness and in combat, and nothing outside can tell from
  here when an arrow is free. The first two points are reached from inside
  a pick-list and a picker and nowhere else, so no other screen's arrow is
  ever offered. The third is reached from every command bar, and is a
  table of callers (below): a caller is named or it is not touched.
  Driven: Up, Down and Left in the 3D view at 4,12 S with the seam on and
  off give the same positions and identical stills (252 of 252), and the
  first two points are never reached there.
- **The program's own stepper does the rest**, so wrapping, title
  skipping and paging are the program's, not this seam's.
- **Keypad and digits.** The menu-bar routine's raw mode turns the digits
  1-9 into the movement letters the keypad's arrangement implies, with the
  out-parameter set, so 7 and 1 already stepped a list and 8 and 2 come
  back as `0x48` and `0x50`. The program cannot tell the number row from
  the keypad, so with this seam on 8 and 2 step a list too.
- **The keypad's 8 and 2 at the bars** (#447). With Num Lock on the
  keypad delivers its digit as the character (`0x4838`, `0x5032`). The
  routine translates a digit **after** the point reads the key, with the
  table at DGROUP `0x288C` (`1` to `9` become `O P Q K` space `M G H I`; the
  table is in the program's initial data and in a running image alike), and
  it looks at the character and never at the scan code: the key comes from
  the program's read routine as its low byte alone, and an extended key is
  the pair zero then the scan code. A keypad 8 therefore reaches the party
  cursor as `H` and a 2 as `P`, which it does not know, and the selection
  goes to the head. The third point rewrites the ring's head word `0x4838`
  to `0x4737` and `0x5032` to `0x4F31` at the same callers as the arrows;
  the table turns the 7 into `G` and the 1 into `O` and the cursor steps
  as it does for Home and End. Told from the arrows by the character, and
  from the number row by the scan code, which is Up's, Down's, Home's or
  End's and never `0x02` to `0x0B`. The hero keys have a point at
  the same instruction and both run, this piece's first (the table's order),
  and each keeps its own keys: the number row's `8` selects the eighth member
  and the keypad's `8` steps back one (`SeamHeroKeysWithListArrows.*`; driven
  in slot C's camp).
- **`fired` counts the keys a list or picker read**, not the arrows
  rewritten: a handler that arrives and chooses to do nothing has been
  served (`docs/seams.md` §3a). An arrow-free run reads `fired=9` and this piece rewrites
  nothing.
- **Qualified as overlay 25**, resolved from the manager's word at every
  step (`docs/seams.md` §4); its points do nothing while the overlay is out, and the seam
  stays armed by its others.
  The store releases' GAME.OVR differs from the repack's only inside
  overlay 2 (`docs/seams.md` §5), so the digest holds on both. The word's address came
  from the search `docs/seams.md` §8.1 describes (one match; the same search returns
  `0x360` for overlay 8 and `0x760` for overlay 15).
- **How the facts were checked, by two routes.** The module row is the
  overlay file's own table and the manager's record of the same two
  numbers, which the search above found once; the two points are the
  instructions after the two calls in the disassembly, and a driven run
  reached each: the list's with the manager's word armed and `fired=9`
  through creation, the picker's by casting a cure on a party member
  from the adventuring bar and reading the highlight step on Down, Down
  and Up (the head, the tail from the head, back).
- **Driven:** race, gender and class lists at creation, the spell list,
  the Items screen's list, the picker via a cast. **Read and tested, not
  driven:** the shops, training, coin selection and encounter lists; the
  picker from Trade, which did not open one from the Items screen's `T`
  (the cast's did), and from a script.
- **Rejected:** a host-side remap of the arrows, because a host cannot
  say when an arrow is free; a rewrite at the menu-bar routine's read
  point for every caller, because Up and Down mean something at the
  adventuring bar, in combat, in Modify and in the rest-time menu, which
  is why the third point is an allowlist and not an exclusion table (a
  caller nobody read would be stepped by default, and the cost of a wrong
  guess is a party that does not move, or a selection that is reset);
  rewriting in the BIOS buffer for the lists, because they see the key
  through the routine's return and the buffer would carry a key nobody
  pressed.
- **Not here:** the main menu's Up, Down and Enter (#434, the `menu-cursor`
  piece); the keypad's 8 and 2 at the adventuring bars, where they walk; a
  held key scrolling (#426).

**The command bars** (#435). Many raw-mode callers of the menu-bar
routine hand every raw key to the party cursor, which steps the selected
member on Home and End and **on any other key puts the selection back on
the first member**. There Up and Down were not dropped, they threw the
selection away; Home and End were the only keys that stepped it. The third
point rewrites Up as Home and Down as End in the ring, before the program
reads them, at the callers below. The bar, its highlight and its commands
are untouched, and nothing is drawn. A caller is identified exactly as
`bar-keys` does it (`seam_menu_bar.h`, shared): the routine's far return
address in its frame, whose segment must be the one the overlay manager
says the caller's module is at now, and whose offset is the instruction
after the call.

**The allowlist.** A caller is in it when raw Home and End reach the party
cursor there and Up and Down do nothing else. Each by two routes: the
disassembly of the caller from its return offset on (the out-flag test, or
the lack of one, and the compare chain: no compare against `0x48` or `0x50`
outside an out-flag guard), and a driven run with the seam on. The driven
runs print the return address and the manager's word at the point (a
temporary print, removed) and read the party panel for the selected member.

| caller | module (manager's word) | return offset | Up and Down today | checked |
|---|---|---|---|---|
| the camp bar | overlay 15 (`0x760`) | `0x1F24` | the cursor: selection to the head | disassembly; driven (`camp-down`, `camp-down-arrows`) |
| camp's Magic bar | overlay 15 | `0x1447` | the same | disassembly; driven: Down steps the member |
| camp's Alter bar | overlay 15 | `0x1CA4` | the same | disassembly; driven: Down steps the member |
| the party-order screen | overlay 15 | `0x17DA` | not picked up: the same. Picked up: ignored | disassembly; driven on and off |
| the post-combat treasure bar | overlay 5 (`0x260`) | `0x1024` | nothing: the cursor is called on `G` and `O` only; `P` is under the out-flag | disassembly; driven, one-member party (the arrow is rewritten, the member cannot move) |
| the post-combat Take bar | overlay 5 | `0x0D91` | nothing: the compares are `M`, `I`, `E`, `0`, `G`, `O` | disassembly and the ported source; **not driven**: it is up only when both coins and items are pooled, and the fights reachable here drop coins |
| the shop's bar | overlay 6 (`0x290`) | `0x061F` | nothing: `P` is under the out-flag | disassembly; driven at the armourer |
| the temple's bar | overlay 4 (`0x230`) | `0x0DAA` | nothing: `H` (Up's scan code) and `P` (Down's) are under the out-flag | disassembly; driven at Sune's temple (Up does not open Heal) |
| the script prompts | overlay 7 (`0x2C0`) | `0x16EB` | the cursor: selection to the head | disassembly; driven at the armourer's "show you our wares" |

- **The party-order screen** is in, and its Home and End *move the
  member*: with one picked up (Return), `G` moves it up the order and `O`
  down. Up and Down do the same with the seam on (driven: THIEF picked up
  at slot C's camp, Down puts it below PRINCESS FATIMA, Up puts it back).
  Seam off, Up and Down there do nothing.
- **Where a letter is a scan code.** Up is `H` and Down is `P`, and the
  temple's bar has a Heal and the shop, temple and treasure bars a Pool.
  Each of those compares the key with the letter **under** the out-flag,
  so a raw Up or Down is never a command there; the two bars checked
  driven (the temple's and the shop's) did not act on one.
- **The module words** for overlays 4, 6 and 7 (`0x230`, `0x290`, `0x2C0`)
  are the manager's records, found by the search `program_profile.h`
  documents: one match each, and the same search returns the known
  `0x260`, `0x360`, `0x690`, `0x730`, `0x760`, `0x790`, `0x8D0` and
  `0x3C60`. Driven: the word read at the point equalled the frame's
  segment at every allowlisted caller reached (camp `0x2837`, the
  temple `0x2D53`, the shop `0x3158`, the script prompt `0x290B`,
  post-combat `0x306A` on one run).

**Left out, and why** (every other caller of the menu-bar routine is in
the `bar-keys` audit above, which has its verdict on arrows):

| caller | return offset | why |
|---|---|---|
| the adventuring bars, city and wilderness (overlay 14) | `0x09D5`, `0x0C45` | the move mode's: walking, Up and Down move the party; in menu mode it makes them Home and End itself |
| the main menu (overlay 16) | `0x02FD` | #434's: its Up, Down and Enter are the `menu-cursor` piece's |
| the combat move loop, the aim cursor, the command bar (overlay 8, 13) | `0x0AC8`, `0x3178`, `0x0819` | no party cursor: the fighter and the cursor are moved |
| the stat editor (overlay 16) | `0x216E` | Up and Down are its rows |
| the rest-time menu (overlay 20) | `0x076E` | Up and Down are Inc and Dec; driven, stills identical (272 of 272) |
| the game-speed screen (overlay 15) | `0x1B91` | Down and Up are its two commands |
| the temple's appraise bars (overlay 21) | `0x1C47`, `0x1DC1` | no party cursor; `0x47` is its Gems command and Up would start an appraisal |
| the two share prompts (overlay 5) | `0x0AF8`, `0x14C7` | any extended key ends them |
| every caller that is not raw and not in the table below | | the routine throws an arrow away; Home and End do nothing either |
| the pick-lists and the picker | | their own points, above |

- **Fired.** `fired` counts every key the menu-bar routine read at a bar
  the point could be offered, plus the two earlier points' arrivals; it
  is a count of looks, not rewrites (`docs/seams.md` §3a).

**At a bar that is not raw** (#469). A caller with raw mode off (the byte
at `BP+0x0C`, which is **zero**) has the routine throw every extended key
away, so there is no caller to hand a Home or an End to. Where such a
caller's screen shows the party list, the third point does what the caller
would have done, in `seam_party_select.h`, shared with `hero-keys`:

- the target is the party cursor's own rule on Up (`G`: the member
  before; from the head, the tail) and Down (`O`: the member after; the
  tail stays), from the member DGROUP `0x5D92`/`0x5D94` names, read off the
  party list (head `0x5D96`, each record's next at `+0x104`);
- the target is written to `0x5D92`/`0x5D94` (a place the program would
  have put it itself) and the roster drawer is called through the engine's
  call batch: resident paragraph `0xBA`, offset `0x0767`, one far pointer,
  the target's, pushed segment first, cleaned by the routine (the
  automap's and the journal's give-backs call it the same way);
- **the key is answered first**, with the one the program throws away
  (`seam_key_read.h`). The engine offers the point again when a batch ends,
  and a key left at the head of the ring would be acted on twice;
- a step that goes nowhere (Down from the tail, Up in a party of one, a
  selection that is no member) draws nothing and is answered the same way;
- the extended key has to be the arrow itself (character zero); the keypad's
  8 and 2 are not taken at these bars, where the routine does not turn a
  digit into a direction;
- the frame is checked before anything is written: the caller is the
  table's by return address and the manager's word, its raw-mode byte is
  zero, DS is the data segment, the party list reads. With the map's panel
  on the roster's cells the seam steps aside.

| caller | module (manager's word) | return offset | what the screen shows | verdict |
|---|---|---|---|---|
| a locked door's bar | overlay 14 (`0x730`) | `0x0EBF` | the party list beside the viewport | **in.** Disassembly: the call at `0x0EBA` pushes raw mode 0 and animation 0; the ported source, `move_process.c`'s first inlined bar, agrees. Driven: slot C of the shipped slots, the cave's locked door after sixteen moves (the one square on the way where guards wait is avoided) and one more into it; Down selects the second member, Up the first and Up the last, the list redrawn each time and the bar still up; `3`, `9` and Right change nothing |
| a stuck door's bar | overlay 14 | `0x0FFE` | the same | **in.** Disassembly: the call at `0x0FF9` pushes raw mode 0 and animation 0, and the source's second inlined bar agrees. **Not driven**: no stuck door is reachable from the shipped slots |
| camp's Portraits and Monsters bar | overlay 15 (`0x760`) | `0x1DDF` | the camp screen's party list | **in.** Disassembly: the call at `0x1DDA` pushes raw 0, animation 1. Driven: slot C's camp, Alter, then its Portraits and Monsters bar: Down and Up as above |
| the save slot bar | overlay 17 (`0x7D0`) | `0x1DA1` | the camp screen's party list | **in.** Disassembly: the call at `0x1D9C` pushes raw 0 and animation 1 where the mode is camp. Driven: slot C's camp, Save; Down, Up as above, the slot letters untouched |
| the load slot bar | overlay 17 | `0x16E2` | the main menu, whose list exists only once members were added | **out.** Disassembly reads raw 0 (the call at `0x16DD`). Not driven; the list on the main menu behind it is not one this audit has seen |
| the Yes/No prompt | overlay 25 | `0x111E` | whatever the asking screen shows | **out.** One routine under every screen, some with a party list and some without; the caller's identity does not say which, and a redraw of a list that is not on the screen is a wrong picture |
| the icon editor's two bars | overlay 16 | | its own screen, no party list | **out.** And digits are its commands |
| the View bar | overlay 19 | `0x0C9C` | the character sheet | **out.** No party list |
| combat's Done bar, game-speed bar, aim bar and the combat script prompts | overlays 8 and 13 | `0x0F70`, `0x120C`, `0x2C31` | combat | **out.** Never in combat |

The camp's own game-speed bar (`0x1B91`) is **not** in this class: the call
at `0x1B8C` pushes raw mode **1**, and its Up and Down are its commands
(above). The table's rows were each checked by the two routes the section
above names, the disassembly from the return offset back to its call and a
driven run with the seam on, except where it says otherwise. The number
row is the program's at these bars, which throw a digit away. Unit:
`SeamHeroKeysAtABarThatIsNotRaw.*` (Up and Down step; a digit is left).

**State**: none. **Host services**: none. **Keys**: Up and Down, and
keypad 8 and 2, in a list or the picker; Up and Down, and keypad 8 and 2
with Num Lock on, at the nine bars in the allowlist; Up and Down at the
four bars that are not raw, above.

**Fidelity**: this piece's handlers read nothing and write nothing unless
AL is an arrow in a list or the picker, or the ring's head is an extended Up
or Down (or the keypad's 8 or 2) at an allowlisted caller; the unit suite
pins that. A run with the seam on is not idle (the seam's entry above), so
its pairs are `contrast`s against the seam-off baseline: `list-down-arrows`
agrees with `list-down` for 58 of 84 checkpoints and diverges from the first
main-menu frame, and ends with the highlight moved by each arrow, the
character the one `list-keys` makes where the baseline's is the first row of
every list. `camp-down-arrows` (against `camp-down`, 58 of 96) ends with the
selection stepped to the next member where the seam-off run puts the first
back, and `camp-pad-arrows` (against `camp-pad`: End, Num Lock, then the
keypad's 2, 2 and 8) with MULE, THIEF, PRINCESS FATIMA, THIEF where the
seam-off run holds MULE and then HULK. At the door bar `door-pick-3` is a
contrast to `door-pick` (both with the seam on, the keys of one a 3, a Down
and two Ups where the other presses a 9, a Right and two Lefts): 125 of 142
checkpoints identical, divergent from tick 268,507,888. Unit:
`SeamListArrows.*`; stand-ins: `list_arrows_probe_off`,
`list_arrows_probe_on`, `list_arrows_bars_probe_off`,
`list_arrows_bars_probe_on`.

## The bar keys (#425, #438)

`seam_bar_keys.cpp`. Not a PLAN.md §5 item: a player's request, built after
v1's six, and the keyboard half of #379's selection. A setting, no key,
nothing to pull.

| point | module | what the handler does |
|---|---|---|
| image `0x0572` of overlay 25, the call into the program's key-read routine inside the menu-bar routine (`0x03BD`) | overlay 25, through the manager's word | reads the keystroke at the head of the BIOS ring before the program does. Left or Right becomes `,` or `.` unless the caller is in the exclusion table below, raw mode or not. Enter, from a caller in the Enter table (the script runner only when its allow-Enter argument is clear) and where the highlighted group's letter is its last match, becomes that letter. Esc, at the Yes/No prompt or at a script prompt whose command letters are `Y` and `N`, becomes `N` |

| fact | value |
|---|---|
| the module | overlay 25: file offset 182479 (`0x2C8CF`), 4682 bytes (`0x124A`), digest `175454bc…3901`; the program's load-segment word is at image `0x3C60` (the same module as `list-arrows`) |
| the loop | the key-pending call at `0x0566` (resident image `0xA6FD`), and, only if it answered a key, the key-read call at `0x0572` |
| the keystroke | the head word of the ring at 40:1Eh: scan code high, character low. Left `0x4B00`, Right `0x4D00`, Enter `0x1C0D`, Esc `0x011B`. A key behind the head is never read |
| the routine's frame | caller's saved BP at `BP+0`, return IP at `BP+2` and CS at `BP+4`; the raw-mode argument is at `BP+0x0C` (tested as a byte; the seam does not read it) |
| what the routine parsed | locals below BP: the Enter-allowed byte (`BP-0x8F`, the colours are not both zero), the group count (`BP-0x8E`), a pair of positions per group from the same base (first at `BP-0x8F+2g`, last the byte after), and the bar as a Pascal string at `BP-0x53`, characters numbered from one |
| the highlight | DGROUP `0x6B2B`, the one-based group index every bar shares (#304) |
| the pushback slot | DGROUP `0x8501`, the second half of an extended key; non-zero means the head of the ring is not the key about to be read |
| `,` and `.` | step the highlight back and forward, wrapping, and redraw; the program's own |
| a letter | upper-cased, compared with **every** character of the bar, no early exit: **the last match wins** and sets the highlight to its group. Command letters are `0-9A-Z` only |
| an extended key | read as a zero, then its scan code. **Raw mode clear: thrown away.** Raw mode set: returned with the out-parameter set, and the caller decides |
| Enter | returned as `0x0D` when the Enter-allowed byte is set; what then happens is the caller's |
| Esc | returned as a zero, in raw mode or not; what then happens is the caller's. Both callers below loop on it |
| the script runner | overlay 7 (file offset 29907, `0x74D3`; 8730 bytes, `0x221A`), through the stub at `102B:0061`: entry `0x1684`, its call into the menu-bar routine at `0x16E6` (return offset `0x16EB`). The manager's word for the module is image `0x02C0`. The runner's saved BP is the one at the routine's `BP+0`; its allow-Enter argument is the word at its `BP+8`, read as a byte. The hotkeys are the script's `~`-marked letters, upper-cased in the bar, and every other letter of the bar is lower-cased |

**The callers whose Enter is nothing, and the one whose Enter is Rest by
decision.** A caller is identified by the far return address in the frame:
its segment is the one the program's overlay manager says its module is at
now (the word is read at every Enter), and its offset is the instruction
after the call. A caller is in the table when it **asks again** on a
`0x0D` it was handed (#459): every compare misses, and the loop's exit test
does not hold it. Checked: **D** is a driven run, the seam off (Return
changes nothing) and on (Return takes the lit command); **S** is the
disassembly from the return offset on and the ported source, both read for
a `0x0D` compare and for the loop's exit test, and no more than that.

| caller | module (manager's word) | return offset | what it does with `0x0D` | checked |
|---|---|---|---|---|
| the Yes/No prompt | overlay 25 (`0x3C60`) | `0x111E` | loops until the answer is in `{N, Y}` (a class at `0x10B1`: two bits, `0x4E` and `0x59`); the loop's head is **after** the instruction that sets the highlight to the second group, so the highlight survives each loop | D |
| the adventuring bar, city | overlay 14 (`0x730`), file offset 91851, 4268 bytes | `0x09D5` | compares the letter with A, C, V, E, S and L and with nothing else; falls to the status line and the loop | D |
| the adventuring bar, wilderness | overlay 14 | `0x0C45` | the same chain without A | D |
| the camp bar | overlay 15 (`0x760`), file offset 96305, 8158 bytes | `0x1F24` | compares with S, V, M, R and A and loops; the loop ends on the `changed` flag, or on `0x00` or `E` (a class at `0x1E3B`). The Encamp Fix's `F` is taken at the routine's return, before this compare | D |
| camp's Magic bar | overlay 15 | `0x1447` | compares with C, M, S, D and R and loops; the loop ends on the caller's `changed` flag, or on `0x00` or `E` (a class at `0x13D1`) | D |
| camp's Alter bar | overlay 15 | `0x1CA4` | compares with O, D, S, I and P and loops; the loop ends on `0x00` or `E` (a class at `0x1BF0`) | D |
| the script runner, **when its allow-Enter argument is clear** | overlay 7 (`0x2C0`) | `0x16EB` | loops until the key is a letter or digit, or Enter **if the argument is set**; with it clear Enter asks again | D |
| alter's Portraits and Monsters bar | overlay 15 | `0x1DDF` | compares M and P; loops until `0x00` or `E` (a class at `0x1BF0`) | D: Return toggles Monsters |
| camp's game-speed bar | overlay 15 | `0x1B91` | compares F and S (P and H under the out-flag); loops until `0x00` or `E` (a class at `0x1A77`) | D: Return takes Exit |
| the portrait bar, `HEAD BODY KEEP` | overlay 16 (`0x790`) | `0x3449` | compares H, B and K and nothing else; loops until K | D: `bar-portrait` pair |
| the shop's bar | overlay 6 (`0x290`) | `0x061F` | compares B, V, T, P, S, A, E, G and O; repaints the party list and loops while the done flag is clear | D: Return takes Buy |
| the temple's bar | overlay 4 (`0x230`) | `0x0DAA` | compares H, V, T, P, S, A, E, G and O; repaints and loops while the done flag is clear | D: Return takes Heal |
| the post-combat treasure bar | overlay 5 (`0x260`) | `0x1024` | compares V, T, P, S, D, E and `0x00`, G and O; loops while the done flag is clear | S |
| the post-combat Take bar | overlay 5 | `0x0D91` | compares M, I, E and `0x00`, G and O; loops while the done flag is clear. The check that ends it on empty coins or total is on every pass, not on this key | S |
| the combat command bar | overlay 8 (`0x360`) | `0x0819` | the loop ends on a class at `0x0646`, which does not hold `0x0D` (`0x10`, `0x13`, space, `-`, `2`, A, C, D, M, Q, T, U, V); asks again | D: Return takes Move |
| the combat Done bar | overlay 8 | `0x0F70` | compares G, D, Q, B and S; loops until `0x00` or `E` (a class at `0x0E25`) | D: Return takes the lit command |
| the combat game-speed bar | overlay 8 | `0x120C` | compares S and F; loops until `0x00` or `E` (a class at `0x10AF`). The source's header says Enter also exits; the disassembly's class does not hold it | D: Return takes Slower, three times, speed 1, 2, 3 |
| the View bar | overlay 19 (`0x860`) | `0x0C9C` | compares I, S, T and D; loops until `0x00` or `E` (a class at `0x0A4C`) | D: Return takes Drop |
| the temple's appraise bar | overlay 21 (`0x900`) | `0x1C47` | compares G and J, then `E` or `0x00` ends it; any other key falls to a recompute of the sheet and the loop | S |
| the load-game slot bar | overlay 17 (`0x7D0`) | `0x16E2` | loops until the key is in a class at `0x159F`, which holds the letters A to J; `0x0D` is not in it | D: Return loads the lit slot |
| the rest-time menu, **by decision** | overlay 20 (`0x8D0`) | `0x076E` | **takes it as `R`, Rest** (`0x0D` is mapped before the compares). With the seam on Enter follows the highlight instead | D |
| the door bars, locked and stuck, **by decision** (#479) | overlay 14 (`0x730`) | `0x0EBF`, `0x0FFE` | one question, no loop: a key that is not B, P or K is no choice, the same as Exit, and the door stays. With the seam on Enter takes the lit command, which the bar opens on its first (Bash) | S |
| the save-game slot bar, **by decision** (#494) | overlay 17 (`0x7D0`) | `0x1DA1` | loops on Enter; nothing asks before a slot is written. With the seam on Enter writes the lit slot, as typing its letter would; the slot lit on arrival is the last bar's highlight | D: Return saves to the lit slot |

- **Each by two routes.** The first row group (the Yes/No prompt, the
  adventuring bars, camp) was read in the disassembly and driven with the
  seam off, where a Return at the bar leaves the screen as it was (slot C's
  camp bar, its Magic bar and its Alter bar, and the Yes/No
  prompt after Save; slot C's city bar; slot J's wilderness bar; the
  arena master's question in slot A), and with it on, where it takes the
  highlighted command (`bar-enter-keys`, `bar-yn-keys`, and driven for the
  Magic and Alter bars and the two adventuring bars). The audit's rows (#459)
  were read the same way from the return offset (`ovrdis`, with each
  `in_class` bitmap read out of the overlay's code at the offset the call
  pushes), and the ones marked D were driven on the shipped slots with a
  Return at the bar, once with the seam off and once with it on, reading
  the screen text beside each still. S rows have both routes on the
  program's text and none on a screen: the post-combat bars need a pooled
  reward the fights reachable here do not leave, and the appraise bar needs
  gems.
- **The new modules' words** (#459): overlay 17 `0x7D0`, overlay 19
  `0x860` and overlay 21 `0x900`, found by the search the other words were
  (the overlay's file offset and length, packed, in the resident image: one
  match each; the word is sixteen bytes into the record, less the
  header). The search reproduced `0x230`, `0x260`, `0x290`, `0x2C0`,
  `0x360`, `0x690`, `0x730`, `0x760`, `0x790`, `0x8D0` and `0x3C60`. The
  driven rows are the proof they are right: the Return was rewritten only
  where the word equalled the frame's segment.
- **The highlight a bar starts on is the last bar's.** The index is one
  byte shared by every bar and clamped to the group count, so a bar opens
  lit on whichever group the last choice left. The rest-time menu is
  entered by `R` at the camp bar, which is that bar's fourth group, so it
  opens with `Mins` lit, not `Rest`: with the seam on a Return there takes
  `Mins` (selects the minutes field, which is already the field) and the
  menu stays up. Left once reaches `Hours`, twice `daYs`, and so on. A
  word whose capital is mid-word, `daYs`, is a group that begins at the
  capital, so its Enter is `Y`.
- **The script runner's Enter, by two routes** (#438). The runner's
  disassembly (overlay 7 from `0x1684`): the loop's exit test is the
  letter-or-digit class, else `0x0D` with the byte at `BP+8` non-zero, and
  the call at `0x16E6` is followed by `0x16EB`. And a driven run with a
  temporary print at the point (the return address, the manager's word,
  the runner's BP and the byte at `BP+8`): at the arena master's `Yes No`
  and at the partner question after it the call returns `290B:16EB`, the
  word reads `0x290B`, and the byte is `00`; at the press-Enter notice
  before them it is `01`. The byte is the program's own choice, by its
  source: the opcode that asks a question (`0x2B`) sets it only when the
  script offers one choice, so every `PRESS <ENTER>/<RETURN> TO CONTINUE`
  prompt has it set and every question of two or more choices has it
  clear. The attitude picker (`~Haughty ~Sly ~Nice ~Meek ~Abusive`) and
  the encounter prompt (`Combat Wait Flee Parlay`, with `Advance` for
  `Parlay`) go through the same runner with it clear, and so does the
  post-combat *treasure left* question, which is `Yes No`; those three are
  read, not driven. With it set, **Enter is the program's own, and that is
  the prompt's first choice**; the seam leaves it. With it clear, the
  Return that the runner would have asked again for takes the highlighted
  letter, so what a player sees highlighted is what Return answers
  (`bar-script-keys`: 151 of 164 checkpoints identical, divergent at the
  Return at the arena master's question). The byte is read through the
  routine's saved BP; it is refused unless the saved BP is above the
  routine's own and the byte is inside conventional RAM
  (`EnterNeverReadsTheRunnersFramePastConventionalRam`).
- **The last-match guard.** The letter is the first character of the
  highlighted group, read from the routine's own parse. Enter is rewritten
  only if the last position in the bar holding that letter is that same
  character; otherwise the handler declines and the Enter is dropped as
  the program always dropped it. No bar the table names repeats a letter,
  so the guard is never reached on this program (unit test:
  `EnterIsRefusedWhereALaterGroupHoldsTheSameLetter`).
- **A spliced command is a group like any other.** The Encamp Fix's `Fix`
  and the journal's `Notes` are in the bar before the routine copies it,
  so Enter on a highlighted `Fix` or `Notes` is the typed letter exactly:
  frames from the Return to the end are identical to a typed `F` and a
  typed `N` (121 stills each, driven).
- **Left and Right are rewritten at every caller but the ones that use
  them** (#432). The routine throws an arrow away at a non-raw caller, so
  `,` and `.` in its place cost nothing there. A raw caller is handed the
  arrow as its scan code, `0x4B` or `0x4D`, with the out-parameter set,
  and most do nothing with it but hand it to the party cursor (resident
  `0x108A2`, through its thunk), which steps the selected member on `G`
  and `O` and **on any other key moves the selection to the head of the
  party**. A caller is in the exclusion table below when it uses the arrow
  on purpose; it is identified by the same far return address as in the
  Enter table, and every other caller, a frame the table does not know
  included, has its arrows stepped. Every call site of the routine in the
  program's source was read, about forty, and each is below.
- **A letter coincidence is a use of the arrow, and is not excluded.** A
  raw caller that compares letters without testing the out-parameter acts
  on an arrow as the letter that shares its scan code: Right is `M` and
  Left is `K`. Two callers do, and **by decision they are stepped like the
  rest**: the post-combat Take bar, where Right took Money, and the
  temple's keep-or-sell prompt, where Left kept the gem. With the seam on,
  Right at the Take bar no longer takes Money and Left at the temple no
  longer keeps; `M` and `K` still do.

  **The callers that keep their arrows**, by the return address in the
  frame and the manager's word for the module. All seven are in
  `seam_bar_keys.cpp`'s table:

  | caller | module (manager's word) | return offset | what it does with an arrow |
  |---|---|---|---|
  | the adventuring bar, city | overlay 14 (`0x730`) | `0x09D5` | Left and Right turn the party (`K`, `M` with the out-flag set); in menu mode the move mode makes them `,` and `.` itself, after this piece has passed |
  | the adventuring bar, wilderness | overlay 14 | `0x0C45` | the party's own facings and steps |
  | the combat move loop | overlay 8 (`0x360`) | `0x0AC8` | the scan code is the direction: Right steps the fighter east, Left west |
  | the combat aim cursor | overlay 13 (`0x690`) | `0x3178` | Left and Right move the cursor |
  | the stat editor (Modify, main menu) | overlay 16 (`0x790`) | `0x216E` | Left lowers the highlighted score, Right raises it (`K`, `M` with the out-flag set) |
  | the treasure share's press-Enter prompt | overlay 5 (`0x260`) | `0x0AF8` | any extended key ends it |
  | the NPC share's press-Enter prompt | overlay 5 | `0x14C7` | the same |

  The last two do not use the arrows on purpose, but their bar says
  `press <enter>/<return> to continue` and the program lets any arrow end
  it as well; stepped, an arrow would do nothing at all, so they keep
  theirs. Judgement, and easy to take out of the table.

  **The callers that are stepped**, one line each (return offset where it
  is a fact the table or a test needed):

  | caller | what it does with an arrow |
  |---|---|
  | the camp bar (overlay 15, `0x1F24`) | the out-flag set, hands it to the party cursor, which resets the member; the loop's exit test is `0x00` or `E` |
  | camp's Magic bar (`0x1447`) | the same |
  | camp's Alter bar (`0x1CA4`) | the same. Its inner Portraits and Monsters bar (`0x1DDA`) is not raw |
  | the party-order screen (`0x17DA`) | with no member picked up, the cursor routine; with one picked up, only `G` and `O` act |
  | camp's game-speed screen (overlay 15) | tests the out-flag; only `P` and `H` (Down and Up) act |
  | the main menu (overlay 16, `0x02FD`) | the bar is not drawn (its colours are zero); a raw key outside `G` and `O` is dropped. Stepping moves the shared highlight byte unseen. Up, Down and Return here are `menu-cursor`'s |
  | the combat command bar (overlay 8, `0x0819`) | a raw key outside a short set the bar takes is cleared and the bar asked again |
  | the combat aim bar (overlay 13, `0x2C31`) | not raw |
  | the combat done bar | not raw |
  | the combat script prompts (`ecl_stage`) | not raw |
  | the other script prompts (`ecl_menu_run`) | a raw key is the party cursor's reset and then ignored |
  | the pick-lists (overlay 25) | a raw key other than Home, End, PgUp, PgDn is looped on |
  | the party-member picker | only `G` and `O` act; the loop ends on Enter, Esc, `E` or `S` |
  | the post-combat loot bar | no `K` or `M` compare |
  | the post-combat Take bar (overlay 5, `0x0D91`) | **Right is `M`, Money**; by decision stepped |
  | the shop bar | no `K` or `M` compare |
  | the temple bar | no `K` or `M` compare |
  | the temple's appraise bar (overlay 21) | no `K` or `M` compare |
  | the temple's keep-or-sell prompt (overlay 21) | **Left is `K`, Keep**; by decision stepped |
  | the rest-time menu (overlay 20 `0x8D0`, `0x076E`) | Left and Right pick the days/hours/minutes field; **by decision stepped**, like any bar. `Y`, `H` and `M` still pick the field, and Up and Down still add and take away. Its Enter is Rest, and is the highlighted command's by the same decision (above) |
  | the game-speed bar, the icon editor's two bars, the move and process bars, the View bar, the save and load slot bars, the Yes/No prompt | not raw |

  **Each exclusion by two routes**: the caller's disassembly (the call,
  the raw argument pushed as one, the arrow compare in the loop after it),
  and a driven run with the seam on. Driven with a temporary print of the
  return address and the manager's word at the point: the adventuring bar
  (`0x09D5`, `0x730`), the rest-time menu (`0x076E`, `0x8D0`, stepped since), the combat
  move loop (`0x0AC8`, `0x360`), the aim cursor (`0x3178`, `0x690`), the
  main menu (`0x02FD`, `0x790`), the combat command bar (`0x0819`,
  `0x360`) and the aim bar (`0x2C31`, `0x690`). Not driven: the stat
  editor and the two share prompts, whose offsets are the disassembly's
  alone, and whose words are from the manager's records (below).
  **The words** are the manager's record of each module, found by
  searching the resident image for its file offset and length from the
  overlay table and taking the word sixteen bytes in, one match each:
  overlay 5 `0x260`, 8 `0x360`, 13 `0x690`, 14 `0x730`, 15 `0x760`, 16
  `0x790`, 20 `0x8D0`, 25 `0x3C60`. The known four (`0x360`, `0x730`,
  `0x760`, `0x8D0`) reproduced the values other seams carry.
  Driven with the seam on and off, identical: the city's 3D with Left,
  Left, Right, Right (96 of 96 stills) and the wilderness with Up, Left and
  Right (232 of 232).
- **Up and Down at those bars** are the `list-arrows` piece's (#435), which
  has a point at the same instruction and its own allowlist of the callers
  where Home and End step the party cursor; the two pieces share the module,
  the read point and the caller test in `seam_menu_bar.h`. The rest-time menu
  is not in that allowlist: Up and Down are its Inc and Dec.
- **The trade at camp.** With the seam on, Left and Right at the camp
  bar, Magic, Alter and the party-order screen no longer put the selected
  member back on the first one. Nobody presses an arrow to do that, and
  Home and End still step the member (driven at camp: `End`, `End`,
  `Home` the same stills on and off, 110 of 110; then a Left leaves the
  second member selected where the seam-off run puts the first back).
- **The point is the read, not the poll.** A point at the poll call would
  see the ring before the poll looks; a key can land between the two. At
  the read call the key is waiting and nothing can change the head before
  the read, so what the handler sees is what the program reads. The
  pushback slot is checked because the poll answers from it first.
- **The other seams' claims are ahead of this point.** The automap's and
  the journal's claims are at the key-pending routine, which the loop
  calls first, so a key either wants is gone from the ring before this
  point is reached. One race remains: a key landing between a claim and
  the poll's own look reaches this point, and with the journal reader up
  would be the reader's. The handler does nothing while the reader is
  open (`LeavesTheKeyAloneWhileTheJournalReaderIsOpen`), because **the
  reader's bars are the reader's own** and not the program's: with
  `modern-controls` on the reader steps their highlight on Left and Right and
  takes the command on Return itself (`docs/seams.md` §10 on the journal, #471). Driven with
  `automap` and `journal` on, Tab, arrows, `Notes` and Return in the
  reader are identical to the seam off.
- **Posted under Enter's scan code.** A letter made of an Enter carries
  scan `0x1C`: the program reads the character and nothing else.
- **`fired` counts the keys the menu-bar routine read**, at the main menu
  and every bar after it, not the rewrites: a handler that looks and
  chooses to do nothing has been served (`docs/seams.md` §3a).
- **Enter at a bar the routine does not draw** (Enter-allowed clear) is
  left alone: the routine would have dropped it. So is Enter while the
  highlight index is still zero, before any bar has been stepped or chosen
  from: no group is highlighted, so there is no command to take. A group
  count or position the routine could not have written declines
  (`point_not_recognized`).
- **Rejected:** a table of the callers that may be stepped, which is
  what #425 built (every raw caller left alone), because the callers that
  use the arrows are few and the rest would each need the same proof;
  posting a second key with `inject_keystroke`, because the
  program drains its keyboard after every key it reads (`docs/seams.md` §8.4) and the
  head would be read first anyway; a table of every caller, because a
  caller that confirms on Enter would be handed a letter it never asked for
  (below); rewriting AL in the key-read routine, because an arrow comes
  back as a zero and then its scan code, two returns for one key.

**Every caller's Enter, one line each** (#459). The routine has 37 call
sites in the overlays and one in the resident image (the party picker),
38 in all; the first group is the Enter table above.

*Enter is dropped, and now taken (in the table, 19 callers, and the
script runner when its argument is clear):* the Yes/No prompt; the
adventuring bar in the city and in the wilderness; the camp bar and its
Magic and Alter bars; alter's Portraits and Monsters bar; camp's game-speed
bar; the portrait bar; the shop's bar; the temple's bar; the post-combat
treasure bar and Take bar; the combat command bar, Done bar and game-speed
bar; the View bar; the temple's appraise bar; the load-game slot bar.

*Enter meant Rest; taken by decision, so Enter follows the highlight:*
the rest-time menu (overlay 20, `0x076E`). The player who never moves the
highlight finds it on `Mins` (above), so a Return there no longer rests.

*Enter acted by accident; taken by decision (#487):* the combat aim bar
(overlay 13, `0x2C31`). The key is returned to the aim loop, whose compares
miss and whose redraw then repeats the last Next or Prev step (the step is
a stale local). Left and Right step this bar's highlight, so a player who
lights `Manual` or `Target` and presses Return means that command.

*Enter was dropped where a slot is written; taken by decision (#494):* the
save-game slot bar (overlay 17, `0x1DA1`). The selection block draws its lit
slot as chosen and the load bar takes Enter, so Return writes the slot the
block is on, as typing its letter would, with nothing asking first. The slot
lit on arrival is the last bar's highlight, not one the player picked; Left
and Right move it.

*Enter chose a member under a lit `Exit`; taken by decision:* the
party-member picker (resident image `0x38AA`, the routine's one caller
outside the overlays; named by the address its frame's paragraph `0xBA` and
offset `0x2D0A` make). Its loop ends on Enter, Esc, `E` or `S`, and Enter
and `S` both leave the selected member chosen. It never sets the bar's
highlight, so it opens on the last bar's: View's `Trade` is that bar's
second group for a member with items and no spells memorized (or the
reverse), and `Trade to?` then opens with the block on `Exit`, where Return
traded. Enter takes the lit command: `E` leaves, and `S` is what Enter
was. Driven: slot A, `V`, `T`, Return goes back to the View bar (seam on)
and opens the coin list (before); `Left` first, then Return, opens the coin
list.

*Enter means something already; left alone:*

| caller | return offset | what Enter does there |
|---|---|---|
| the pick-lists (overlay 25) | `0x0FE0` | confirms the row while the bar's highlight is on its first command, where the list opens it; **with the highlight moved, Enter takes that command** (Exit, Next, Prev, or the caller's own); **at the Items list, Enter takes the lit command on the first command too** (below) |
| the party-order screen (overlay 15) | `0x17DA` | in the toggle class (`0x174E`): picks a member up, puts it down |
| the combat move loop (overlay 8) | `0x0AC8` | `0x0D` ends it (the bar has no commands: no group to take) |
| the combat aim cursor (overlay 13) | `0x3178` | confirms, as `T` does |
| the temple's keep-or-sell (overlay 21) | `0x1DC1`, `0x2114` | any key but `K` sells: **Enter sells** |
| the two share prompts, *press Enter* (overlay 5) | `0x0AF8`, `0x14C7` | the prompt asks for it and ends on it |
| the script runner, allow-Enter set (overlay 7) | `0x16EB` | the first choice |
| the disk-swap prompt (overlay 17) | `0x236F` | one question: any key but `S` is Insert, a retry |
| the icon editor's bars (overlay 16) | `0x39FE` | **taken**, by the maintainer's decision: states 1, 4 and 5 drop Enter; states 2 and 3 take it as a pick of the default (a block or colour slot named `0x0D`), and the highlighted entry is the pick a player means |

**The pick-list's callers** (#496). The list routine (overlay 25,
`0x0D9A`) opens with the bar's highlight on its first command, and hands
its caller the key that ended it, with the lit row as its current item: a
`0x0D` on Enter, a command letter on a letter, a zero on Esc or Exit. What
a `0x0D` means is the caller's. The routine has 15 call sites, every one a
far call through overlay 25's stub entry; none is in the resident image or
inside overlay 25. Each caller is named by the instruction after its call
into the list routine:

| caller | module | return offset | what it does with `0x0D` |
|---|---|---|---|
| character creation's four lists | overlay 16 | `0x077A`, `0x0A10`, `0x0B65`, `0x0EA7` | ends the list on it: picks the row |
| the party's Add list | overlay 16 | `0x3100` | adds, as `A` does |
| the temple's healing list | overlay 4 | `0x0B75` | heals, as `H` does |
| the post-combat Take list | overlay 5 | `0x0BE6` | handed up; its loop takes, as `T` does |
| the shop's list | overlay 6 | `0x01C9` | handed up; its loop buys, as `B` does |
| a script's encounter list | overlay 7 | `0x180B` | the key is not read; the row is the answer |
| camp's spell-effects display | overlay 15 | `0x13BB` | the key is not read; the list closes |
| View's Trade and Drop coin lists | overlay 19 | `0x237B`, `0x267A` | the key is not read; a current row picks the coin |
| the temple's Take coin list | overlay 21 | `0x0E90` | a current row and a key not zero pick the coin |
| the spell picker | overlay 22 | `0x01DC` | in the set that ends it: picks the spell |
| **the Items screen** | overlay 19 | `0x1294` | **compares R, U, T, D, H, J, S and I only; a `0x0D` falls to the loop's tail and the list opens again** |

So at the Items screen alone, Enter takes the lit command on the first
command as well, `Ready` where the list opens, and readies the lit item as
`R` does. The seam finds the list's caller one frame up: the menu-bar
routine's saved BP is the list's (both prologues are the standard one, and
the list calls the menu-bar routine with CS pushed, a far frame), and the
list's frame holds its own caller's far return address at `BP+2` and
`BP+4`, as the menu-bar routine's does. A saved BP that is not above the
routine's own, or whose return address would not be in conventional RAM,
is not a list frame, and the list keeps its Enter. Both routes: the
callers' disassembly from each return offset on (the compares, or their
absence) and the decompile's reading of each caller; the call sites are a
byte search of every overlay and the resident image for the stub entry.
Driven: Enter at the Items list readies the lit item (seam on) and does
nothing (seam off).

*Enter is dropped, and left out by decision (1 caller):*

| caller | return offset | why not |
|---|---|---|
| the stat editor, Modify (overlay 16) | `0x216E` | the `0x0D` compare is under the out-flag, which Enter never sets (the keypad's 5 is a space in the table at `0x288C`), so it is dropped; but the bar is `Keep Exit`, the highlight is stale, and `Exit` discards the edit. The arrows are its controls. Not driven |

*Not applicable (1):* the main menu bar (overlay 16, `0x02FD`): its colours
are zero, so the routine does not take Enter (the Enter-allowed byte is
clear); Up, Down and Return are `menu-cursor`'s.

**How the facts were checked, by two routes.** The module row is the
overlay file's own table and the manager's record of the same two numbers
(`list-arrows`). The point and the frame offsets are the disassembly of the
routine (the call at `0x0572`, the reads of `BP+0x0C`, `BP-0x8F`, `BP-0x8E`
and `BP-0x53`, and the scan that sets the highlight) and a driven run: a
Left at the Yes/No prompt moves the highlight to `Yes` and Return then
answers it (the program exits), and Return at character creation's *keep
this character?* prompt answers its `No` and the character is rolled again. The caller offsets are
the instructions after each call in the callers' disassembly and the
offsets two other seams already place their points at (`journal`'s
`0x09D5` and `0x0C45`, `encamp-fix`'s `0x1F24`), and for the exclusions the
addresses above, four of them seen live.

**Esc answers No** (#438), at two callers and nowhere else. Esc is read by
the routine as a zero, and both callers loop on that. At **the Yes/No prompt** (return `0x111E`) the rewrite is
unconditional: that prompt is one routine whose bar is always `Yes No`, and
every question the program asks through it (quit to DOS, keep this
character, continue the battle, flee, and more) has `N` as its other
answer. At **the script runner**
(`0x16EB`) it is conditional on the bar: its command letters, every `0-9A-Z`
in the routine's copy of it, are exactly `Y` and `N`, which is what a
script's `~Yes ~No` makes (the arena master's two questions; the
post-combat *treasure left* question, by the source). A script bar with
a third letter, a digit, or one answer only is left alone. The key is posted
as the letter under Enter's scan code, as Enter's is. Driven: Esc at the
quit question after Save returns to the camp bar (`bar-esc-keys`: 91 of 99
checkpoints identical, divergent at the Esc), and Esc at the partner
question answers No and the adventuring bar returns (`bar-script-keys`).
The Esc before the quit question, at the save-slot bar, is that bar's own
and is untouched in both runs.

**Where Esc is ignored or not No at a two-answer prompt, left alone.** Read
in the program's source, not driven. The temple's keep-or-sell prompt
(overlay 21, return `0x1DC1`) keeps the gem only on `K` and sells on any
other key, so Esc **sells** it; nothing here changes that. Every other
two-answer prompt the source shows is either the Yes/No prompt or a script
question, both covered above, or a bar whose Esc leaves it (the save-slot bar,
the sub-bars of camp).

**What no check pins.** The callers' modules are identified by the manager's
word and the return offset, not by digest. `journal` and `encamp-fix` pin the
same two modules by digest.

**State**: none. **Host services**: none. **Keys**: Left and Right at
every caller but the seven above; Enter at the twenty callers in the Enter
table and at the script runner when its allow-Enter argument is clear; Esc
at the Yes/No prompt and at a script prompt whose letters are `Y` and `N`.

**Fidelity**: this piece's handler reads the ring's head word and writes
nothing unless it is one of the four keys at a caller it names; the unit suite
pins that. A run with the seam on is not idle (the seam's entry above), so
its pairs are `contrast`s against the seam-off baseline, each agreeing with
it up to the first main-menu frame (58 checkpoints) and differing from there:
`bar-enter-keys` (against `bar-enter`: the Return at the camp bar takes
ALTER, and its sub-bar is up at the end where the baseline still shows the
camp bar), `bar-yn-keys` (`bar-yn`: the Right steps the slot bar's highlight
and the Return answers the quit prompt No), `bar-camp-keys` (`bar-camp`: the
Right steps the highlight to ALTER and the Return takes it),
`bar-esc-keys` (`bar-esc`: the second Escape answers the prompt No and the
camp bar is back), `bar-script-keys` (`bar-script`: the Return answers the
arena master's question No and the Escape answers the partner question; the
press-Enter notice before it, which allows Enter, is the program's own) and
`bar-portrait-keys` (`bar-portrait`: the Return takes BODY and the body is
the next one). Besides the screens, the ring holds the rewritten word where
the seam-off run holds the key. Unit: `SeamBarKeys.*`; stand-in:
`bar_keys_probe_off`, `bar_keys_probe_on`.

**One consequence a script feels.** A Return the program used to drop at a
Yes/No prompt now answers it. Character creation's roll asks whether to
keep the character; the leg-0 script's Return at that prompt answers `No`
and the character is rolled again. So does an Esc there.

## The menu cursor (#434)

`seam_menu_cursor.cpp`. Not a PLAN.md §5 item: a player's request, built
after v1's six, and the main menu's half of what #425 and #423 did for the
bars and the lists. **It draws something the game never had** (the menu
has no highlight at all), so it is held to PLAN.md §5's "native in feel":
the cursor is the game's own pick-list highlight, drawn by the game's own
string routine. A setting, no key, nothing to pull.

| point | module | what the handler does |
|---|---|---|
| image `0x0572` of overlay 25, the call into the key-read routine inside the menu-bar routine (the point `bar-keys` has) | overlay 25, through the manager's word | with the routine's caller the party-setup loop, **Up** or **Down** move the cursor and are answered; **Return**, with the cursor drawn, becomes the letter of the command under it |
| image `0x02F8` of overlay 16, the loop's far call into the menu-bar routine (#453) | overlay 16, through the manager's word (`0x0790`) | with no cursor drawn, the first command shown is lit |

| fact | value |
|---|---|
| the caller | overlay 16's loop (the manager's word is at image `0x0790`); the call is at `0x02F8` and the instruction after it, the return address the frame holds, at `0x02FD`. The loop passes the bar at DGROUP `0x05F0`, both bar colours **zero** and raw mode **set** |
| who reaches the loop | the party-setup screen at the start of the program, and the same function again where a script re-enters it: **a training hall**. Driven at the cleric hall in the civilised district (5,0, off the lobby at 6,2), where `Y` to *do you want to train* opens it; the caller test below was passed there as at the start |
| the commands | eleven records at DGROUP `0x0619`, forty-two bytes each: a Pascal string and, at `+0x29`, an enable byte (`0x0642`, `0x066C`, ... `0x07E6`). In order: Create, Drop, Modify, Train, View, Add, Remove, Load, Save, Begin, Exit. A record whose byte is not zero is drawn |
| what the loop writes | each time it redraws: Drop, Modify, View, Remove, Save and Begin on and Load off with a party member, the reverse with none, and Train on where the place trains. **Create, Add and Exit it never writes**; they are on from the start. So the menu shows **four** commands with no party (Create, Add, Load, Exit), **nine** with one, **ten** at a hall (Load is off, Create and Add are on) |
| the drawing | rows from text row `0x0C`, packed, one to a command: the first letter in colour `0x0F` at column 2, the rest of the word in `0x0A` from column 3 (screen text: ink `F`, then `A`) |
| the keys | the ring's head word: Up `0x4800`, Down `0x5000`, Return `0x1C0D`. Up and Down are raw keys the loop drops; Return is dropped by the routine (the bar has no colour); a letter in the bar takes its command |
| the redraw | any command taken, a disabled command's letter included, ends in the loop's **full redraw** (frame, party, rows). Home and End (`G`, `O`) go to the party cursor and back to the call **without** one |
| the string drawer | image `0x076B6` (`docs/seams.md` §3): column, row, colour, then a far pointer to a Pascal string. The cursor's word is drawn as two Pascal strings the handler places (`place_bytes`), the first letter and the rest |
| overlay 16 | file offset 104831 (`0x1997F`), 17497 bytes (`0x4459`), digest `232c6aa6…45a0`; the manager's record is at image `0x0784` and the load-segment word sixteen bytes in, `0x0790` |
| the loop's call | the instruction at `0x02F8`. At it the stack holds, from the top: the out-parameter's offset and segment (a byte in the loop's frame, so the segment is SS), the animation flag, raw mode (one), both bar colours (zero), the prompt colour (`0x0D`), and the bar's far pointer, DGROUP `0x05F0`. **A byte argument is pushed as a whole register**, so each of those words has the register's leftover in its high half and the handler reads the low byte only |

- **The loop's addresses by two routes.** The disassembly of the loop (the
  pushes before the call: the bar at `0x05F0`, `0x0D`, `0`, `0`, `1`, `0`;
  `lcall 0x3C5:0x2A`, the thunk for overlay 25's `0x03BD`, at `0x02F8`)
  and the machine: the handler's caller test (the frame's far return
  address against the manager's word and `0x02FD`) is passed at the first
  menu of every driven run, after the add screen and the view screen, and
  at the hall. The records, their stride and their enable bytes are the
  program's data segment as the loop's source reads it and as a run's
  screen text lists them (four rows, nine rows, ten rows, in the order
  above).
- **Visible from the start** (#453). The loop's call at `0x02F8` is reached
  every time the loop asks for a key, which is as soon as the menu is drawn
  and again after each key it throws away. The handler reads the enable byte
  the cursor lives in: with the cursor not drawn (the redraw wrote it as one)
  it lights the first command shown and writes the byte; with it drawn it
  does nothing. The batch it queues offers the point again when it is done,
  and finds the cursor drawn. A menu with neither or both of the two bytes
  on, or with nothing shown, declines and draws nothing. A Return with no
  cursor on the screen (the seam switched on while the menu was up) is still
  the program's own: dropped.
- **Where the cursor is, and why nothing else is kept.** In the program's
  own memory, in the place its redraw also writes. The loop rewrites the
  enable bytes of **Drop and Load** every time it redraws, and exactly one
  of them is on (Drop with a party member, Load without). The byte that is
  on holds `1`. While the cursor is drawn the seam holds **`2` plus the
  row** in it. Every reader of an enable byte tests it against zero (the
  loop's draw and dispatch; the program's source names no other reader), so
  the program cannot tell, and the redraw that wipes the cursor's pixels
  puts the byte back: the cursor lasts exactly as long as it is on the
  screen. Nothing is remembered by the seam and nothing has to be cleared.
  Not in `scratch()`, `docs/seams.md` §3's last resort: it was not needed.
- **Why not the bar's highlight byte `0x6B2B`.** It was the first design
  and it cannot say whether a cursor is drawn: a typed letter sets it, so
  the menu would grow a cursor on its return from a command and Return
  would take a command nobody could see. `,` and `.` (which the `bar-keys` piece
  gives to Left and Right) step it with nothing drawn. And it is not where
  the menu left it: the screen a command opens runs a bar of its own on the
  same byte (measured at the menu: `2` after the add screen, `1` after the
  view screen). The seam never writes it. Nor are **the pixels** the state:
  the composed frame lags the planes by up to a frame, and a seam that is
  only looking must never read the video window (`docs/seams.md` §3, §8.4).
- **Moving.** The cursor is on the **first** command shown, so the first
  press moves it: Down, Down takes the third. Up from the first goes to the
  last. It wraps at both ends, over the **rows shown**: a command the
  menu does not draw has no row and is skipped. After a command is taken
  the menu is redrawn, the cursor is hidden again, and the next Up or Down
  starts from the first.
- **Drawing.** The word it moves to is drawn the way the loop draws it with
  its rest in yellow (the look the `select-yellow` piece gives every
  selection): its first letter in `0x0F` at column 2 and the rest in `0x0E` at
  column 3, by the program's own string drawer, so the text is never in this
  repository. Both colour words carry `selection_mark` (`0x8000`) in their
  high half, which the drawer does not read and the selection block's stores
  turn into blocks (#483). The word it leaves is put back the way the loop draws it: its
  first letter in `0x0F` at column 2 and the rest in `0x0A` at column 3, two
  Pascal strings the handler places (`place_bytes`, `docs/seams.md` §3), 41 bytes at most. One batch of at most three calls,
  with the lit word first so that the cursor is on the glass before the old
  word is put back. Frame by frame (`--dump-every 1`, a press at 7,600
  and another at 7,615) a move is a left-to-right sweep a glyph at a time:
  six frames to light a word, five more to put the old one back, and no
  frame in which a word is blank or half missing; each cell goes straight
  from one colour to the other, as the program's own text appears.
  **Making the program redraw its menu** (the loop has a
  flag for it) was considered and not built: it repaints the frame, the
  party and every row on each press, and the cursor would then need a
  second point after the redraw to be painted on it, which needs to know
  whether to, which is the state this design does not have.
- **Answered.** The key is replaced by `-` (`seam_key_read.h`), which the
  routine reads and throws away, so it goes back to waiting. A batch offers
  its point again when it is done (`docs/seams.md` §3), and the handler then finds the `-`
  and nothing to move.
- **A second press while the cursor is drawn.** A move costs the program's
  own glyph-at-a-time drawing: **26,960 steps (about 90 ms of the machine's
  time) for a first press and 60,706 (about 200 ms) for one that puts a word
  back**, measured. A key pressed in that time waits behind the `-`, and
  the program's read keeps the last key waiting and drops the rest, which
  ate the third of three quick Downs. So the handler takes its own `-` off
  the ring when an Up, Down or Return is behind it, and handles that. It is
  the only thing it ever takes off the ring; three Downs three frames apart
  now move three rows. The cursor's byte is written when the move is
  decided, not when its pixels are done, so a Return right behind a Down
  takes the row the Down chose.
- **Interplay.** The `bar-keys` piece's Left and Right at this menu step the
  shared highlight byte with nothing drawn; they do not touch the cursor. Its
  Enter table does not include this caller. The `list-arrows` piece does not
  reach the main menu (#435 extends it to the horizontal bars and leaves this
  caller to this piece). Driven with the three on: Down, Right, Left, Down,
  Home, Return gives the same stills as Down, Down, Home, Return.
- **Rejected:** Up and Down as the party cursor (Home and End) here, the
  maintainer's choice, which is the other half of #435; a host-side remap,
  for the reason the `list-arrows` piece gives; keeping the cursor between commands,
  which needs a place that survives a redraw and is not the program's;
  starting from the bar's highlight; and the keypad's 8 and 2, which the
  routine turns into the movement letters itself (they arrive as raw `H`
  and `P` and the loop drops them).
- **Not here.** The pick-lists (the `list-arrows` piece), the horizontal bars
  (the `bar-keys` piece), a held key (the hosts drop OS key repeats,
  `hosts.md`).

**On Curse of the Azure Bonds** (#555) the menu is the same loop over
the same kind of records, so the cursor is the same design with Curse's
facts (`menu_cursor_facts`): it lives in Drop's or Load's enable byte
while it is drawn, the loop's redraw puts the byte back, and nothing is
kept in the seam or in `scratch()`. Its row is a white block, key and
rest, the program's own highlight colour (the selection block's colour
table).

| fact | Pool of Radiance | Curse of the Azure Bonds |
| --- | --- | --- |
| the loop's module | overlay 16 (file offset 104831, word `0x0790`) | overlay 18 (file offset 131047, 22202 bytes, digest `5cc199bf…6373`, word `0x0A80`), the profile's `roster_overlay` |
| the loop's call into the menu-bar routine; the instruction after it | `0x02F8`; `0x02FD` | `0x038E`, a far call through overlay 27's stub entry (`0104:002A`); `0x0393` |
| the call's words from the top | the out-parameter's offset and segment (SS), animation 0, raw 1, both colours 0, the prompt colour `0x0D`, the bar's far pointer | the same nine words; the prompt's far pointer is pushed before them |
| the bar the loop hands the routine | DGROUP `0x05F0` | DGROUP `0x052A`, thirteen letters, no colour |
| the command records | eleven at DGROUP `0x0619`, forty-two bytes each, the enable byte at `+0x29` | twelve at DGROUP `0x0553`, the same stride and enable byte; Human Change Classes is the fifth |
| what the loop writes | Drop, Modify, View, Remove, Save, Begin on and Load off with a member, the reverse with none; Train where the place trains | the same, and Human Change Classes where the place trains and the selected member is a human who may change class; Create, Add and Exit never written |
| the two bytes the cursor lives in | Drop (`0x066C`) and Load (`0x0768`), the second and eighth records | Drop (`0x05A6`) and Load (`0x06CC`), the second and ninth records |
| what reads an enable byte | the loop's draw and dispatch, each against zero | the same (the draw at `0x02CF`, the dispatch from `0x045B`), each against zero |
| the rows | from text row `0x0C`, the key in `0x0F` at column 2, the rest in `0x0A` from column 3 | the same |
| the string drawer the cursor draws with | image `0x076B6`: column, row, colour, the string | image `0x5029` (paragraph `0x4CE`, `0x0349`): column, row, colour, **paper** (black), the string |
| Enter at the loop's bar | dropped: the bar has no colour | the same: the routine takes Enter only from a bar with a colour |
| what an arrow does there | handed back raw; `G` and `O` go to the party cursor, any other is dropped, no redraw | the same; a `G` or `O` that changes whether the selected member may change class, where the place trains, redraws the whole menu |
| who reaches the loop | the start, and a training hall's script | the start, and the script opcode that re-enters it: a training hall, and the end of the game |
| the cursor's row | the key a white block, the rest a yellow one | a white block end to end |

The records and the bytes were read twice: in the data segment of the
unpacked image (twelve Pascal strings forty-two bytes apart, the enable
bytes of Create, Add, Load and Exit set and the rest clear, as a program
that has not yet run its menu leaves them) and in the loop's own
instructions in the store copy's `GAME.OVR` (the compare of the record's
enable byte at `0x02CF`, the two calls into the string drawer, the bar's
offset pushed at `0x0375` and the far call at `0x038E`). Driven, Steam
copy, `--wall none`: with no party `CREATE NEW CHARACTER` a white block
from the first frame, Down, Down, Down, Down to `ADD`, `LOAD`, `EXIT` and
round to `CREATE`, Up from it to `EXIT`, and Return on `LOAD SAVED GAME`
asks `LOAD WHICH GAME: A B`; with slot A's party, nine rows, every one
reached and both ends wrapped, and Return on `BEGIN ADVENTURING` begins.
**Not driven**: the menu at a training hall, which nothing from the
shipped slots reaches; it is the same call, so the same point.

**State**: none of the seam's own; the cursor is held in the enable byte of
Drop or Load while it is drawn (above). **Host services**: none. **Keys**:
Up, Down and Return at the main menu.

**Fidelity**: the cursor is seen as soon as the menu is drawn, so there is
no on-and-untriggered run that is the run with the seam off (`docs/seams.md` §8.5), and the
pairs are `contrast`s. `menu-letters-cursor` agrees with `menu-letters` for
58 of 77 checkpoints and diverges from tick 147,668,400, the first frame the
menu is drawn in. `menu-down-cursor` agrees with `menu-down` for 58 of 70 and
diverges from the same tick, and the slot prompt is up at the end where the
seam-off run shows the plain menu. Unit: `SeamMenuCursor.*`; stand-in:
`menu_cursor_probe_off`, `menu_cursor_probe_on`. Driven (#453): the bare
menu, a party menu and a hall's; `fired` reads two for a bare menu with no
key pressed.

## The hero keys (#439)

`seam_hero_keys.cpp`. Not a PLAN.md §5 item: a player's request, built after
v1's six. A setting; keys and a display change. Nothing to pull.

| point | module | what the handler does |
|---|---|---|
| image `0x0572` of overlay 25, the call into the key-read routine inside the menu-bar routine (`bar-keys`' point) | overlay 25, through the manager's word | a number-row `1` to `8` at the head of the BIOS ring, from the main menu: puts the selection on the target's successor (the head for the last member and for a party of one) and rewrites the key to Home. With nobody behind the digit, the key becomes the one the program throws away |
| image `0x138F`, the instruction after the roster drawer clears a member's row | resident image, `inside_calls` | on the main menu's list (column one): the name's column goes up two, and the program's glyph routine draws the member's number at the old column in white. Beside the viewport (column `0x11`) this and the three points below do nothing |
| image `0x13CB`, where the drawer's two name paths meet, before the armour class | resident image, `inside_calls` | a name that now ends past column `0x20` has its tail cleared by the program's clear routine; the column goes back |
| image `0x140F`, the push of the armour class's column (AX `0x20` to `0x22`, by the value's width), same frame check | resident image, `inside_calls` | AX goes up one: the value is right-aligned in `0x21` to `0x23` |
| image `0x135A`, after the drawer has copied the heading `AC  HP` into its frame (`BP-0x0E`, a length-prefixed string) for row two | resident image, `inside_calls` | the copy becomes ` AC  HP`: the heading's `AC` moves one right and `HP` stays; refused unless the copy is the original string |

| fact | value |
|---|---|
| the party | a linked list. Head: far pointer at DGROUP `0x5D96`; a record's next member: far pointer at `+0x104`; the selected member: far pointer at `0x5D92` (offset, then segment). At most eight members |
| the cursor | the resident routine behind `0x108A2`. `G` from the head goes to the tail, from anyone else to the member whose next is the current one; `O` goes forward (the tail stays); any other key goes to the head |
| how a caller reaches it | the menu-bar routine in raw mode returns `0x47` (Home) as `G`, `0x4F` (End) as `O`, and the characters `1` to `9` through the table at DGROUP `0x288C` (`7` is `G`, `1` is `O`, `8` `H`, ...), with the out-parameter set. The callers below hand `G` and `O` to the cursor and redraw the list from the selection |
| the number row | scan `0x02` to `0x09` for `1` to `8`, character `'1'` to `'8'`. The keypad's digits with NumLock on are scan `0x47` to `0x51` with the same characters, which is the only way to tell them apart |
| the data segment | image paragraph `0xC7C` on (DS `0x0CDC` where the image is at `0x60`); checked against DS before the party is read |
| the roster drawer | image `0x1307` (paragraph `0xBA`, offset `0x0767`; the automap and the journal call it through the same address; it takes one far pointer, the selected member, segment pushed first, and cleans it). Frame: column byte at `BP-5` (`1` on the main menu, `0x11` beside the viewport), row byte at `BP-6` (`4` for the first member), the member's far pointer at `BP-4` and `BP-2`. Names are drawn from the column (white for the selected member, else by status), the armour class at `0x20`+ (right-aligned, by the value's width), the hit points at `0x24`+; the heading `AC  HP` is copied into the frame (`BP-0x0E`) and drawn on row two from column `0x21` |
| the routines it calls | the glyph blitter at paragraph `0x709` offset `0x1DF`, `retf 0Ch`, six words first to last: column, row, colour, `1`, character, `1`; the clear at paragraph `0x3F1` offset `0x137`, `retf 8`, four words: left, top, right, bottom. Both far, called at the paragraph they were linked at (`docs/seams.md` §8.4) |

**The one caller that takes a digit** is the main menu (overlay 16, the
manager's word `0x790`, return offset `0x02FD`; the same far-return
identification as `bar-keys`), the title screen's and every other screen the
same loop serves. Its Up and Down are the menu cursor's.

- **Every other bar selects with Up and Down** (#479): the camp, Magic and
  Alter bars, the script prompts, the treasure and Take bars, the shop, the
  temple and the bars that are not raw through `list-arrows`, and the
  party's own bar through the move mode. So the number row is the program's
  again there, which is the keypad's layout: at the party's bar in walking
  mode `8`, `4`, `6` and `2` walk and turn and `1` and `7` select a
  neighbour, and in menu mode the move mode turns `8`, `4`, `6` and `2` into
  the arrows' steps.
- **Each by two routes.** The main menu's disassembly from its return
  offset: `G` and `O` in the out-parameter's branch go to the party cursor
  (resident `0x85:0x52`, the thunk of `0x108A2`) and then to the roster
  drawer (`0xBA:0x0767`). And a driven run with six members added: the
  digit selects.
- **The landing.** The cursor's `G` is the program's, so the seam's whole
  job is to put the current pointer where `G` lands on the target: from a
  successor `G` goes to its predecessor, from the head to the tail. Proved
  for each party of one to eight members, from every starting selection and
  to every target, against the rule restated in the test
  (`LandsOnTheFirstAMiddleAndTheLastMemberOfAnyParty`), and driven on the
  real list for the first, middle and last of six. Nothing but the
  selection and the ring's head word is written.
- **A digit with nobody behind it** is the key the program throws away
  (`seam_key_read.h`): the routine goes back to waiting. `9` and `0`, the
  keypad's digits and every other caller are left alone.
- **With the journal reader up the seam steps aside**: the reader takes
  every key, and that is checked at the key point. When the map or the
  reader is put away the roster comes back through a batch of the program's
  own calls, with every number in it: the two display points are
  `inside_calls` (`docs/seams.md` §3) for this, and draw by pushing a call frame by hand
  whose far return is the point itself, which a handler inside a batch may
  do where it may not start one.
- **The display.** The name moves two columns right (the number and a blank)
  and the number is drawn in white in the program's own glyph routine, so
  `font-sans` and `font-chisel` letter it as they letter the names. The
  program draws the name in both of its paths (white for the selected
  member, a status colour otherwise) from the column byte, so the byte is
  what is moved: up two after the row is cleared, back after the name, with
  the byte's own value as the only state (`1` and `0x11` are the program's;
  `3` and `0x13` mean the number is drawn, `2` and `0x12` that the cut is
  done). On the main menu the names start at column one and every name
  fits; the armour class moves one right, to `0x21` to `0x23`, and the
  heading's `AC` with it, and the cut (the program's clear over `0x21` to
  where a name would have ended) is never needed. **No other column
  moves**: the hit points and their heading stay. **Beside the viewport the
  list is the program's own**: no number, no name moved or cut, the
  armour class and the heading where the program draws them.
- **Where the numbers are drawn**: the main menu's party list, driven with
  six members added. Every other party list is the program's.
- **Why a point at the name's call and not the program's draw routines.**
  The routine that draws a name in a status colour and the string drawer
  are called from the character sheet, combat and the camp's report as
  well; the points are inside the roster drawer so nothing else moves.
- **`max_points` is ninety-six.** With every seam on, one face counted
  (they are alternatives), the build has sixty-eight points, thirty-six of
  them `modern-controls`'. At thirty-two the last seam a player switched on
  was refused with `too_many_points`
  (`SeamHeroKeys.EveryBuiltInSeamFitsTheEngineAtOnce`).
- **Rejected:** writing the selection and redrawing the list ourselves,
  because the main menu redraws after its own step and the program's cursor
  already lands where the digit says; `call_program()` for the number
  and the cut, because the automap's and the journal's give-backs draw the
  roster inside a batch, where no point may start one; drawing a number
  with a font of our own, because the face seams would not reach it; the
  party-order screen, for the reason above.

**State**: none: the two display points keep their one value in the drawer's
own frame and put it back, and the key point decides from the machine.
**Host services**: none. **Keys**: the number row's `1` to `8` at the main
menu.

**Fidelity**: the main menu's party list is changed from the first time it
is drawn with a member in it, so a run with the seam on is not the run with
it off (`docs/seams.md` §8.5). `quiet-modern-controls` is a `contrast` to `quiet` and
`quiet-all-on-sight` one to `quiet-all`. What a digit does at the main menu
is pinned by the unit suite and the stand-in programs. Off, the engine is
not consulted (`docs/seams.md` §7). Unit: `SeamHeroKeys.*`; stand-ins: `hero_keys_probe_off`,
`hero_keys_probe_on`.

## The selection block (#453, #483)

`seam_select_yellow.cpp`. Not a PLAN.md §5 item: a player's request, chosen
from six mock-ups, and the colour half of #379's selection; then drawn as a
block so that it is not told by colour alone (#483). A look and a setting:
no key, nothing to pull. Twelve points: seven rewrite one colour before the
program draws with it, two, the blitter's row stores, invert the rows of
what those seven coloured, and three paint the block's margin where it lies
outside the block's cells.

| point | module | what the handler does |
|---|---|---|
| image `0x0273` of overlay 25, the glyph call in the bar leaf's highlighted arm | overlay 25, through the manager's word | colours the character the way the **lit word** is coloured (below) |
| image `0x02CA`, the leaf's glyph call for a command letter outside the highlight | overlay 25 | colours it as an unselected character; changes a swapped bar's green letter to white |
| image `0x0305`, the leaf's glyph call for any other character outside the highlight | overlay 25 | the same; changes a swapped bar's white to green, or a character of the lit word to yellow |
| image `0x09FD`, the string call in the pick-list's highlight-on leaf | overlay 25 | the row's colour becomes yellow |
| image `0x13AF`, the string call in the roster drawer's selected-member arm | the resident image, `inside_calls` | white becomes yellow |
| image `0x0918` of overlay 19, after an ability score's colour is chosen | overlay 19 (`0x0860`) | a highlighted draw's light magenta becomes yellow |
| image `0x153F`, after a hit-point value's colour is chosen | the resident image | the same, for the highlighted hit points |
| image `0x74A2` and `0x74C8`, the blitter's two EGA row stores, one per page | the resident image, `inside_calls` | inverts DL when the glyph is a selection's (below) |
| image `0x7667`, the blitter's step to the next cell | the resident image, `inside_calls` | notes the cell a block or not, and settles the margin of it and the three cells its margin can lie in |
| image `0x71FB`, the rectangle fill's epilogue | the resident image, `inside_calls` | forgets the blocks and margin in the rectangle, and settles it and the ring around it |
| image `0x618C`, the picture drawer's epilogue | the resident image, `inside_calls` | settles every cell the picture covers, on each page that holds a margin |

| fact | value |
|---|---|
| overlay 19 | the character sheet's: file offset 135226 (`0x2103A`), 11026 bytes (`0x2B12`), digest `3011cde3…11e1`; the manager's record at image `0x0854` and the load-segment word sixteen bytes in, `0x0860` (one match in the resident image; the same search returns the known words for overlays 4 to 7, 14, 15 and 16) |
| the bar leaf | overlay 25 `0x01DA`, called by the menu-bar routine with its own frame: the caller's BP is the near argument at `BP+6` and the highlighted group's number a byte at `BP+8`; the character's one-based position is a byte at `BP-1` |
| the caller's frame (the menu-bar routine) | `color_hi` at `+0x0E`, `color_lo` at `+0x10`; the bar as a Pascal string at `-0x53`, its length again at `-0x64`; the group table at `-0x8F`, a first and a last position to a group (`menu_groups` in the facts of `bar-keys`) |
| the glyph call | six words from the top of the stack: fold flag, character, count, **colour**, row (`0x18`), column. The flags and the colour and the row are bytes pushed as whole registers, so the high half of each is the program's leftover |
| the string call | five words from the top: string offset, segment, **colour**, row, column |
| the blitter's EGA path | for each of the glyph's eight rows and each of the four planes: a plane the colour lights gets the row, fetched into DL and stored once to each page (`0x749A` and `0x74C0` are the instructions after the fetches, the faces' points; `0x74A2` and `0x74C8` the stores); a plane it does not light gets zero. The cell is opaque, so a row stored inverted is the paper in the colour and the letter in black |
| the frames a store follows | the blitter (paragraph `0x709`, `0x01DF`) and the string routine (`0x0626`, same paragraph) keep BP frames: the caller's BP at `BP+0`, the far return address at `BP+2` (offset) and `BP+4` (segment), the arguments from `BP+6`; the string routine's colour is the word at `BP+0x0A`. The string routine pushes CS and calls the blitter near, returning to image `0x7724` |
| the calls a store recognises | each a five-byte far call, so the return is the call's offset and five: the bar leaf's three glyph calls (overlay 25 `0x0273`, `0x02CA`, `0x0305`, above); the string calls of the pick-list (overlay 25 `0x09FD`) and the roster's selected member (image `0x13AF`); Modify's score (overlay 19 `0x0976`), exceptional strength's percentage (overlay 19 `0x0A41`) and hit points (image `0x155E`), whose routine's first argument at `BP+6` says highlighted; and a batch's own return, `F000:0800` (seam.h) |
| `selection_mark` | `0x8000` in the colour word a seam pushes for a string it draws as a selection (`seam_builtin.h`); the string routine reads the low byte |
| the blitter's step to the next cell | paragraph `0x709`, `0x05D7`: `inc` of the column argument, reached by every adapter's path once a cell is drawn, with the blitter's frame (row `BP+0x0E`, column `BP+0x10`, colour `BP+0x0C`) and, on the EGA, the map mask back on every plane |
| the rectangle fill | paragraph `0x709`, `0x00B7` (both clear routines, `0x3F1:0x0137` and `0x709:0x0DD3`, fill through it), epilogue at `0x016B`; from `BP+6` the fill, the page, then the bottom, right, top and left cells. The page selects segment `0xA000` plus `0x200` paragraphs a page |
| the picture drawer | paragraph `0x464`, `0x1951`, epilogue at `0x1B4C`; from `BP+6` the picture's far pointer, then its row and its column, words; a picture starts with its height in lines and its width in bytes. On the EGA it writes `0xA000` through a line table of its own, so its page is not in its call. Overlay 34's tile drawer (`0x015D`) lays an 8x8 tile through it, and the frame border (image `0x3F10`) every cell of a frame through that, the Fix's report's inside its batch |
| the display pages | the data segment's words `0x4A16` and `0x4A18`, the two segments the blitter stores every glyph row to |
| the list leaf | its context's near pointer at `BP+6`; the highlight's colour at `+0x20` of it |
| the roster drawer | image `0x1307`; its frame has the column and row bytes at `BP-5` and `BP-6` and the member's far pointer at `BP-4`; the call's own words repeat all three |
| Modify's two draws | the first argument at `BP+6` is non-zero for a highlighted draw; the colour is a local, at `BP-0x2B` for a score (overlay 19) and `BP-1` for hit points. Modify is the only caller that passes it |

**The selection's colour, per program** (the profile's `selection_colour`).
On Curse of the Azure Bonds the block is the program's own highlight
colour, white, and not the baseline's yellow (#555): the program draws
its bars' and lists' highlights as white blocks already, so a lit word
there is drawn as the program draws it, with the margin round it.

| where | Pool of Radiance | Curse of the Azure Bonds |
| --- | --- | --- |
| a bar's lit word | yellow, the key letter white | white, key and rest (the bar's bright is the selection's colour, so the key is white too) |
| a one-command notice | yellow from its first letter | white from its first letter |
| a pick-list's row | yellow | white, on the ink, where the program put it on the paper |
| the roster's selected member | white made yellow | white, left as the program draws it, and made a block |
| Modify's selected score and hit points | light magenta made yellow | light magenta made white |
| Modify's highlighted name | none: the editor has no name field | light magenta made white, the cursor's letter left white on black, the gap in the block |
| the main menu's cursor row | the key white, the rest yellow | white end to end |
| the journal's log row and its bar's lit command | yellow, the bar's key white | white |
| the margin | painted in yellow, over black | painted in white, over black |
| what a store takes for a selection's colour | yellow, from the calls named above | white, from the same calls |

On Curse the hero keys' numbers, white on black, sit two columns left of
a selected member's white block: the number is a white glyph on black
and the name a white block with the letters black, so the two read as a
key and a selection, as the yellow did, by shape rather than colour.

- **The lit word is a word, and a group is not** (#453). The program's
  highlight is its *group*: from a command letter to two characters before
  the next. On a bar whose keys are not the first letters of its words it
  covers a word and a half. The icon editor's top bar is the one found: two
  of its keys are digits that end a word, so the first group runs into the
  next word. The seam lights **the run of characters between spaces that
  holds the group's command letter, and every whole word after it the group
  holds too** (`bar_call` reads the bar, the group table and the
  character's position out of the two frames): a script's option is a
  phrase, `Tell the truth?`, one group with one key, and is lit whole
  (#483), where the icon editor's half word stays out, yellow with each
  of its command letters in the bar's bright, and every other character is
  drawn as the bar draws the unselected: a command letter in the bright, the
  rest in the dim. The three bar points all make this decision for their own
  character, so a character of the word the group left out (the digit's word
  before it) is lit by the point it goes through. **The program's group table,
  its highlight byte and its key matching are untouched.** A word with two
  command letters draws both white; no bar of the program's own with that
  shape was found (a scan of the program's strings for bars whose words hold
  two letters finds only the main menu's own record, which the loop draws
  itself and not through this leaf), and a script's bar is built at run time out of
  the script's own `~`-marked letters, so the rule is general rather than
  tabled.
- **Where the bright is already yellow**, the lit word's letters are drawn
  white, so the key still stands out. **No bar in the build was found to
  pass yellow as `color_hi`**: the camp's quit prompt and its ten siblings
  pass `0x0E` as the *prompt* colour (the first of the three colour
  arguments a `confirm_yn` call pushes), and white as `color_hi`. The rule
  is covered by the unit suite and was not seen driven.
- **A bar with one command letter is lit by its group** (#460, #483). A bar
  with one command letter has one group, so there is nothing for the
  highlight to move to, but its one command is what Return takes. The seam
  lights the program's **group**, the characters drawn through the
  highlighted arm, as a word is lit (the key in the bright and the rest
  yellow, as blocks), but for the blanks after its last letter: the group runs
  to the bar's end, and a load bar with one save is `A ` (#498). **A notice is
  yellow from its first letter**: where a word follows the key's, the bar is a
  sentence, its capital only starts it, and there is no other key to set it
  apart from, so the whole group is one yellow block; the walking bar's `Exit`
  keeps its white `E`. Every other
  character is drawn as the program drew it, the swapped pair below put right
  (the program's bright green becomes white and its dim white green). It left the whole bar as drawn until #483: the
  maintainer asked for the walking bar's `Exit` and the notice to be lit like
  everything else. **The pick-list's `Exit` is the exception**, recognised by
  the menu-bar routine's return into overlay 25 at `0x0FE0` (the pick-list
  routine's call, which `list-arrows` and `bar-keys` name too): under a list
  Return takes the row, which is the selection, so that bar is left as
  drawn. The script runner (overlay 7)
  hands every one-choice prompt to the menu-bar routine as such a bar: the
  prompt is `PRESS <ENTER>/<RETURN> TO CONTINUE` in the script, the runner
  lower-cases every letter and digit of a menu's text but the `~`-marked
  hotkey, and the bar the routine copies is `Press <enter>/<return> to
  continue`, one capital and so one group that runs to the end of the bar,
  drawn in white from end to end (the one-choice prompt passes white for both
  colours). Read at the draw, at the arena master's notice in slot A (the
  status row reads 7,1), where the issue's reading of the text as capitals
  was wrong: the face draws lower case in capital shapes, so the screen shows
  capitals whatever is stored. Before #460 the word-shaped highlight lit
  `ress` yellow after a white `P`, as a menu with a selected word; the group
  is the whole notice, so it is lit end to end.
  **The rule is the number of command letters, not the case.** The first
  rule tried, "no lower-case letter in the bar", is wrong twice: it does not
  touch this notice, which has lower case, and an all-capitals bar can be a
  real choice (the save and load slot bars, `A B C D E F G H I J` and the
  load bar's `A B C E J`, a letter to a word; the seam draws those the same
  either way, for a word that is one key stays white). **The check against
  every bar** (the decompile's call of each of `command_input`, `confirm_yn`
  and `menu_run`, and every string it passes): the program's own bars are all
  one capital to a word and have two command letters or more (the
  adventuring, camp, Magic, Alter, View, shop, temple, training, combat,
  Modify, icon editor and portrait bars; `Yes No`; the pick-lists draw
  through another point); the two post-combat press-Enter prompts are
  written `press <enter>/<return> to continue` with no capital at all, so the
  group table has no group, and they were already declined; the main menu's
  record has no colour and is not drawn. A script's menu is parsed from its
  `~`-marked letters, every other letter lower-cased, so a script's choice of
  two or more is mixed case with two command letters or more, and only a
  one-choice prompt has one. One bar of the program's own with a single
  command is on screen in the sessions: the ` Exit` under each of character
  creation's pick-lists (a list's selection is its row, which is yellow). It
  is left white beside the yellow row (`list-keys-yellow`), by the exception
  above. A bar built at run time of the parts the program
  has on hand (the item screens, the combat bars) is one capital to a word,
  and one with a single word has no choice to move between. `bar-script-keys`
  runs the notice and the question after it; the unit suite has the notice's
  shape, a lone word, the swapped notice and an all-capitals bar that keeps
  its treatment.
- **A bar handed its colours the wrong way round is drawn the right way
  round** (`color_lo` white and `color_hi` green). Four callers do it, found
  by reading every call of the three routines out of the disassembly (the
  pushes before each `lcall` of the menu-bar routine's thunk, the Yes/No
  routine's and the pick-list's, in every overlay and in the resident image)
  and then confirmed for the two reachable ones by driving them:

  | caller | what it is | confirmed |
  |---|---|---|
  | overlay 16 `0x3444` | the portrait screen's `HEAD BODY KEEP` bar | the disassembly and the decompile agree; driven, seam off and on |
  | overlay 4 `0x0157` | the temple's `PAY FOR CURE` question | the disassembly and the decompile agree; driven (leg 5), seam off and on |
  | overlay 22 `0x0CB2`, `0x0D18` | the two detect-magic questions (a combat-only spell cast outside combat, and a monster's item) | the disassembly and the decompile agree; **not driven**, no leg reaches them |

  The pair is recognised **at the drawer and not by caller**: when
  `color_lo` is white and `color_hi` is green, a bar's letters are drawn
  white and the rest green. A table of callers would go stale (a script's
  menus pass the colours the script says, which the build cannot list), and
  three points cover every bar. The cost is a caller that means white and
  green deliberately; no such caller is known, and the swapped ones all
  draw a bar inside out. Two things the port's source says are wrong for
  the real program and were checked against the disassembly: the party-member
  picker's bar (resident `0x38A5`) is listed there with the colours
  swapped and pushes the ordinary ones, and the camp's quit question is
  listed with yellow for `color_hi` and pushes it for the prompt. Where the
  two disagree the disassembly is the program. Two post-combat prompts
  (overlay 5 `0x0AF3` and `0x14C2`) pass white for both, which is not the
  swapped pair: the unselected words are white as before, and the lit word
  is yellow with its key white.
- **A selection is a block** (#483). The store follows the frames back to
  the call that drew the glyph and inverts DL when that call is a selection:
  a bar character in the lit word (the bar points' own reading of the leaf's
  frame, through the blitter's saved BP), the pick-list's row and the
  roster's selected member in yellow, Modify's score with its routine's
  highlighted argument set, or a batch's string whose colour carries
  `selection_mark`. **A colour alone is not a selection**: yellow is a hurt
  character's hit points and the prompt colour of a few questions, and white
  is every key letter. The colours stay what the seven points made them, so
  the lit word is a yellow block and its key letter a white one. **The
  stores and not the fetches**, because the faces swap DL at the fetches and
  the engine offers an address's points in table order, which puts
  `modern-controls` before the faces: at the fetch the face's row would
  overwrite the inverted one. A store has nothing to decline; every glyph
  goes through it. The screen text reads a block as its glyph, black ink on
  the colour's paper (`screen_text.h` takes either colour as the ink).
- **The margin outside the block** (#483, `selection_margin.h`). Every face
  lays a letter in columns 1 to 7 and rows 0 to 6 (text_face.h), so the
  block keeps its left and bottom margin in its own cells and its letters
  touch its top and its right. Those two are painted in the neighbouring
  cells: row 7 of the cell above each block cell, and column 0 of the cell
  after the block's last with the corner above it, **only over black**, on
  both pages. `selection_margins`, observation on the machine like the
  automap's, keeps which cells are blocks and which margin pixels this
  machine painted; at each cell the blitter finishes, after each fill, and
  after each picture the program lays at a cell (the frame's chain over a
  bar, a tile at a time), the cells whose margin may have changed are
  settled: what a cell wants is painted, and what it no longer wants is put
  back to black, **only where it is still this machine's** (a cell the
  program draws or fills is its own again). What it holds is checked on the
  screen before a cell is settled: a picture's call does not say which page
  it is drawn on, so a picture settles every page that holds a margin, and a
  margin pixel it drew over is forgotten and painted again before the next
  frame is shown. A block the program covered some other way (the automap's panel
  over the roster) is checked on the screen before a margin is painted
  beside it: its column 0 and row 7, paper in every glyph, must be its
  colour but for two pixels, or it is forgotten. The graphics controller's
  and the sequencer's registers are read first and written back after; the
  adapter answers a read of each. A face's glyphs were moved one column right
  for it, to match the program's own font, which already left column 0
  paper. Measured in the game's face: `G` and `,` hang a pixel or two into
  row 7, and `M`, `W` and `X` reach column 0; painting only over black leaves
  them. **The screen text reads past it**: a cell that does not read is read
  again with its column 0 and row 7 blanked out of it and of every glyph,
  and the answer kept only if it is one character or a blank
  (`screen_text.h`). Chosen from mock-ups of the block grown over anything
  and over black only, which looked the same: the frame's chain under a bar
  has 16 non-black pixels in its bottom row.
- **Who else asks.** The `menu-cursor` piece draws a row of its own in the
  selection's colour, with a white key. The journal's Notes list draws a cursor row of
  its own and its reader's bars a highlight, both in the program's bright
  without this seam; each asks `modern_controls_on(box)` (`seam_builtin.h`),
  which is `status("modern-controls").state == on`, and draws the
  selection's colour (`selection_colour`) where it is on. The engine already lets a seam ask this (`text_face::drawing()` does
  for the faces); nothing was added to it, and neither keeps the answer. The
  reader's bars take the highlighted command in that colour, its key letter white,
  and no yellow on a bar of one word (#471, `docs/seams.md` §10 on the journal). All three
  draw through the string routine in a batch, which the stores cannot tell
  from a seam's other text by colour, so each sets `selection_mark` in a
  selection's colour words; the store heeds the mark only in a call that
  returns to the batch, because a program's own push has a leftover in that
  half.
- **A selection the program draws that is not here, and why.** The pick-list
  is one routine at one call site (`0x0EF6` calls the highlight-on leaf), so
  the character-creation lists, the spell lists, the shops, training, coins
  and the item screens are all covered by the one point; the creation lists
  and the list of characters to add were driven, the rest are read. The
  party-order screen and the member picker show their selection through the
  roster drawer. **Left out:** the icon editor's cell cursor (a box drawn
  on a grid of pixels in a colour that has to show against the art), the
  combat grid's aim cursor (a sprite), and a hurt character's yellow hit
  points in the roster (a status, not a selection).
- **The seam checks the frame it was told is there** and declines, touching
  nothing, when it is not: the glyph or string call's other words, the
  colours the caller's frame says it passed, the bar's text at the
  character's position, a group in the table. A colour is never rewritten to
  what it already is, and only the **low byte** of a pushed word is read or
  written.
- **`max_points` is ninety-six, and with every seam on the build carries
  sixty-eight points** (the faces count once, being alternatives),
  thirty-six of them `modern-controls`': three, one, two, five, one, these
  twelve, the move mode's five, the text entry's two and the aim watch's
  five.
- **Rejected:** a face of the same shape (a seam with a parameter: seams have
  none); lighting the program's group as it is (the half of a word on the
  icon editor's bar, the maintainer's screenshot); a table of the callers
  that swap their colours; recolouring the unselected words of every bar
  (the program's own mix of white and green is the game's and stays);
  yellow for the roster's selected number (a number is a key and stays
  white). For the block (#483), from mock-ups of five treatments over all
  three faces: the key letter white on black inside the yellow block; the
  letters squeezed to six rows so the block has a margin above and below
  (it reads better in the faces, and the game's own face loses its `E`'s
  bar); a change of palette (a colour still); brackets or a pointer
  (packed bars have no room for one, and it would move every word).

**State**: the margin's bookkeeping, `box.margins()` (`selection_margin.h`):
observation, which `reset()` drops, the serialization never sees and a replay
rebuilds. **Host services**: none. **Keys**: none.

**Fidelity**: seen the first time a highlight or a selected member is drawn,
so a run with the seam on is not the run with it off (`docs/seams.md` §8.5), as with the
faces. Unlike a face it differs in `cpu` and `ram` as well as `devices` and
`display`, because the seam rewrites a colour the program has pushed and the
program goes on to use it, and a store leaves DL inverted. Exercised: `list-keys-yellow` (against `list-keys`,
58 of 84 checkpoints identical: the pick-lists' highlighted rows), and every
bar session, which draws the camp bar, the roster and the quit prompt in
yellow (`bar-yn-keys`), the arena master's notice and question
(`bar-script-keys`) and the menu's cursor row (`menu-down-cursor`). Unit: `SeamSelectYellow.*` (the bar's words on made-up bars, one
shaped like the icon editor's, with the junk high halves a real push has,
and each store's frames laid out for every call it recognises and some it
must not); stand-ins: `select_yellow_probe_off`, `select_yellow_probe_on`.
**Driven** (Release, SDL dummy drivers, seam off and on, stills read): the
adventuring bar and party list, the camp bar, the quit question, the
pick-lists, Modify (a score and the hit points), the portrait bar, the icon
editor's bar, the temple's pay question, the main menu and the Notes list;
the blocks (#483) on the adventuring and camp bars, the roster in camp and
on the party menu, the main menu, a pick-list and Modify, in the game's
face and with `font-sans`, the screen text reading each block's paper.

## The edit keys (#455)

`seam_edit_keys.cpp`. A player's report, not a PLAN.md §5 item: typing a
character's name, the arrows type letters. Right types `M`, Up `H`, Down `P`,
Left `K`, Home `G`, End `O`, PgUp `I`, PgDn `Q`, Insert `R`, Delete `S`, and
the function keys `;` to `D`. It is the original program's behaviour on any
PC. A setting, no key, nothing to pull.

| point | module | what the handler does |
|---|---|---|
| image `0x7AC0`, the instruction after the line editor's call into the program's key read (`1709:0A30`) | the resident image | with AL zero, which is the first half of an extended key, and the keyboard read's pushback slot armed, empties the slot |

| fact | value |
|---|---|
| the line editor | resident `1709:09E3`, image `0x7A73`. It loops on the program's key read (`1899:0059`, image `0x89E9`) and acts on AL: `0x20` to `0x7A` is appended and drawn, `0x08` is Backspace, `0x0D` and `0x1B` accept, anything else is ignored. The call is the five bytes at `0x7ABB`, the instruction after it stores AL |
| how an extended key arrives | the key read (`1A40:030F`, image `0xA70F`) takes the BIOS word and, when the character is zero, **keeps the scan code in a one-byte slot of the data segment (`0x8501`) and answers zero**. The next read finds the slot armed, empties it and answers the scan code. The editor ignores the zero and appends the scan code |
| the data segment | `0xC7C` paragraphs after the image segment, as every seam reads it |

- **The addresses by two routes.** The resident disassembly (post-fixup:
  `lcall 0x1899:0x59` at `1709:0A2B`, then the store of AL) and the bytes of
  the unpacked image (`9A 59 00 99 08 88 86` at image `0x7ABB`, the segment
  still unrelocated), which also hold the editor's prologue at `0x7A73`. The
  key read's disassembly shows the slot taken and cleared at its head and
  written from AH when the BIOS read's AL is zero (`1A40:0323`); the slot is
  the one `seam_menu_bar.h` and the automap already name. A watch cannot
  show it armed: it is set and taken inside a frame. Driven, the handler's
  arrivals are the keys the editor reads: B, Right, O, Up, B, Down, Left
  and Return is eight, and `fired=8`.
- **Who calls the editor**, by two routes: the program's source, and a scan
  of the resident image and of every overlay for a far call to the entry
  (and of the resident segment for a near one). Four, and no other.

| caller | where | what it asks for |
|---|---|---|
| the character's name at creation | overlay 16, call at `0x1E5A` | a name, fifteen characters |
| a script's free-text answer | overlay 3, call at `0x09C8` | a line, forty characters |
| a script's number prompt | the resident number input, call at image `0x7C23`, itself called from overlay 3 at `0x097B` | up to six characters, parsed as a number; a letter makes the program ask again |
| the copy-protection challenge | overlay 2, call at `0x02FD` | the code word, six characters |

  The icon editor and a save's name are not line editors. The seam is
  inside the editor, so all four are covered with one point and no caller
  is named.
- **The number input has the flaw too**, through the editor: an arrow's
  letter is not a number, so the program asks again. It is covered by the
  same point. **The View > Drop money amount editor does not read through the
  editor** and accepts digits and Backspace only, so no arrow is wrong there.
  The one exception is an Alt-letter chord, whose scan codes `0x30` to `0x32`
  are the digits 0 to 2. That is not covered and not claimed.
- **The point is after the read, not before it.** The editor has no poll: it
  goes straight into a blocking read, so a key that arrives while it waits is
  delivered with no point reached (`seam_key_read.h`). A rewrite of the BIOS
  ring's head before the read, which `bar-keys` does, would miss it. After
  the read the handler sees every key the editor is handed, and it takes
  nothing off the ring, so there is nothing to put back.
- **Why the slot and not AL.** The second read's AL is the scan code and its
  slot is already empty, so by then nothing says it is an extended key's
  second half. The first half is the only place that does, and a zero the
  editor already ignores.
- **The scope is the editor's own call.** The point is inside the editor, so
  the bars, the lists, the walk and combat read the same key read and never
  reach it. Driven with this point alone armed: slot A's four walking arrows and
  creation's lists with Down and Up never reach it (`reached=0`) and leave
  the run as it was.
- **Driven.** At the name prompt, `B`, Right, `O`, Up, `B`, Down, Left,
  Return (screen text, row 24 while typing): seam off `CHARACTER NAME:
  BMOHBPK`, on `CHARACTER NAME:  BOB`. `B`, Home, End, PgUp, PgDn, Insert,
  Delete, F1, F2, `O`, `B`: off `BGOIQRS;<OB`, on `BOB`; `fired=12`. At the
  copy-protection challenge, `A`, Right, `B`, Up, `C`: off `INPUT THE CODE
  WORD:  AMBHC`, on `ABC`. Not driven: the script prompts, which no leg
  reaches; they are the stand-in's and the unit suite's.
- **What it costs.** A key with no character is dropped at these prompts, so
  Alt and the function keys type nothing, which is the point. Typing,
  Backspace, Return and Esc are keys with a character and are never touched.
- **Rejected:** a rewrite of the ring's head before the read (above); setting
  AL to a character the editor ignores at the second read, which cannot tell
  the second half from a key; and a host-side filter, for the reason
  the `list-arrows` piece gives (the seam is the only mechanism, and a host cannot tell
  from outside which key read is the editor's).

**State**: none. **Host services**: none. **Keys**: none; the rows on the
key card say what a player no longer gets.

**Fidelity**: this piece's handler reads AL and reads the slot only when AL is
zero, and writes only the slot. A run with the seam on is not idle (the
seam's entry above), so its pair is a `contrast`: `name-arrows-edit` against
`name-arrows`, 58 of 88 checkpoints identical, divergent from the first
main-menu frame, and the name in the character's record is BOB where the
baseline's is BMOHBPK. Off, the engine is not consulted (`docs/seams.md` §7). Unit:
`SeamEditKeys.*`; stand-ins: `edit_keys_probe_off`, `edit_keys_probe_on`.

## The move mode (#479)

`seam_move_mode.cpp`. Not a PLAN.md §5 item: the maintainer's request. The
later games in the series made walking a mode: their adventure bar starts
with `Move`, the arrows step the bar until it is chosen, and the party walks
until `Exit`. This program's bar has no such command, so its arrows walk and
nothing but `,` and `.` steps the bar. With the piece on, the party's own bar
has two modes:

- **Menu mode.** The first command is `Move`, in `Area`'s place in the city
  and in front of `Cast` in the wilderness. Left and Right step the
  highlight, Up and Down select the member before and after, Return takes
  the lit command, and the bar's letters are the program's; a letter the
  bar does not hold does nothing. Nothing walks. With the automap's panel
  over the party list, Up and Down do nothing: the map takes the Home and
  End they become, as it takes the program's own.
- **Walking mode.** The bar is `Exit` alone. The arrows, the keypad and the
  number row walk and turn as the program has them do; Return, Esc and `E`
  go back to menu mode; every other letter does nothing.
- **The party arrives in menu mode with `Move` lit**: after a load, a fight,
  camp, a shop, a script's question, View, any screen with a bar of its
  own. Walking lasts across steps and the events a step runs, and ends at
  the next bar that asks for a choice. Two kinds of bar are part of the
  walk: a locked or stuck door's, and **a notice**, a bar of one command
  that only waits for Return, such as the prompt a script raises after a
  sign or a line of story. A pick-list's bar is a choice however few
  commands it has, because its rows are. The program's other waits for a
  key, and `message-pause`'s, do not go through the menu-bar routine, and
  leave the mode alone.
- **`Area` is gone**, by decision: the automap (Tab) is the overhead view,
  and with the automap off the overhead view is not reachable.

| point | module | what the handler does |
|---|---|---|
| the city's call into the menu-bar routine, `0x09D0` | overlay 14 (`0x730`) | the bar the call is handed (the program's string, `Notes` on it if the journal is on) is copied into the loop's frame at `BP-0x32`, made this mode's bar, and the call's far pointer (`SS:SP+14`) aimed at the copy. Menu mode: the first group, `Area`, becomes `Move`. Walking: the copy is `Exit`. The highlight is put under `Move` when the party has just arrived or `Move` was last lit; `Exit` is lit. With the overhead view on (`0x6AAC`), first clears it and calls the program's screen composer (`0xBA:0x27D9`) through the batch, and the point is offered again |
| the instruction after it, `0x09D5` | overlay 14 | `M` off the bar starts walking; `E` off the walking bar stops it and is handed back as `-`, which the program loops on, so the party does not camp. The highlight goes back to the program's numbering, `Move` lit kept in a word of the seam's, and the value it was entered with when `Notes` was chosen |
| the wilderness's call, `0x0C40`, and the instruction after it, `0x0C45` | overlay 14 | the same, with `Move ` inserted in front of the bar's first group (27 + 5 + the journal's 6 is 38 of the slot's 40), every group after it one up, and the highlight moved for it both ways |
| image `0x0572` of overlay 25, the key read (`bar-keys`' point), the last of the five pieces there | overlay 25 | at the two adventuring callers only. Menu mode: Left and Right become `,` and `.`, Up and Down Home and End, the digits `4`, `6`, `8`, `2` the same four, and a letter that is not one of the bar's command letters the key the program throws away (`seam_key_read.h`), so a typed `H`, `K` or `P` does not walk. Walking: Esc becomes `E`, every letter but `E` is thrown away; Return is `bar-keys`' (the lit command, `Exit`'s `E`). **Any other caller ends walking** and has `Move` lit at the party's next bar, but for the door bars (`0x0EBF`, `0x0FFE`) and a notice: a bar the routine parsed into one group (its count at `BP-0x8E`) from any caller but the pick-list (`0x0FE0`). Both leave the piece's words alone |

| fact | value |
|---|---|
| the adventuring input routine | overlay 14 `0x0989`; view mode byte `0x49FA` (1 the city, 2 to 4 the wilderness) |
| the bars | Pascal `string[40]`s in the data segment, `0x04B6` (city) and `0x04DF` (wilderness), raw mode set |
| the call's arguments | on top the out-parameter's far pointer, five words, the bar's far pointer (`SS:SP+14`), the prompt's (`SS:SP+18`); the routine cleans them. The prompt is a string temporary at `BP-0x33` loaded with the empty string, so only its length byte is read; `BP-0x32` to `BP-0x05` hold nothing the call reads, and the loop fills them afresh after it |
| what the city does with a letter off the bar (out-parameter `BP-0x04` zero) | `A` toggles `0x6AAC` when the area allows it, `C`, `V`, `E` (ends the routine: camp), `S`, `L`; **nothing else matches**, so `M` and `-` go round the loop. The wilderness: the same without `A` |
| what it does with a raw key | `H`, `K`, `M`, `P` walk and turn (relative in the city, where only `H` ends the routine; absolute facings in the wilderness, where all four end it); anything else goes to the party cursor. A typed letter not on the bar comes back raw, so a typed `H` walks |
| the overhead view | `0x6AAC`; the program clears it on a new game, where an area does not allow it and when a script's message comes up; only `A` sets it |
| the highlight | `0x6B2B`, shared by every bar (#304) |
| a notice | the menu-bar routine's group count, `BP-0x8E` of its frame, is one. A script's continue prompt is its menu command with one item: the routine parses it into one group and lights it whole. `bar-script-keys`' walk meets one at 7,1, lit whole as its one group, and the question after it, a bar of two commands, ends the walk |

- **The program's string is never written.** The copy is a local nothing
  reads and the pointer an argument the routine pops, so a seam switched
  off with the bar up leaves the program's own bar for the next call, and
  one switched on with the bar up has no after-point for that call. Whether
  a call carries the copy already (offered again after a batch) is read off
  the pointer (`SwitchedOffAndOnWithTheBarUpTheProgramsBarIsIntact`).
- **The journal** splices `Notes` into the program's string first (registry
  order) and takes it out at the return, so the menu copy carries it and the
  walking copy does not.
- **Why `Move` lit is a word of the seam's.** In the wilderness `Move` is a
  group the program does not have, so outside the call there is no value of
  the shared byte that means it. Every other value is the program's
  numbering outside the call, so the next bar of any kind finds what the
  program would have left there, save for that one.
- **The single-group walking bar is lit like any other**: `Exit` is what
  Return takes, so the selection block lights its group, `E` a white block
  and `xit` a yellow one (#483).
- **Driven** (slot A in the city, slot J in the wilderness, journal and
  automap on): Right, Right, Left step the bar; Down and Up select the next
  and previous member; Return on `Move` gives `EXIT`; Up walks (4,12 to
  4,13), Right turns (S to W), `C` does nothing; Esc gives the menu with
  `Move` lit; `M` and Return go round again; `E` camps, camp's Exit comes
  back with `Move` lit. In the wilderness two Ups walk 3,32 to 3,30 N and
  Esc and Right step the menu.
- **Not driven**: a party in the overhead view when the bar comes up, which
  the program's own messages already clear and a save does not keep; the
  unit suite pins the clearing.

**State**: five words of the seam's own (seam.h, "A seam's own few words"):
walking, arrived, `Move` lit, which bar the call was handed and the
highlight it was entered with.
They are configuration: `enable()` starts them at zero, which is menu mode
with `Move` to be lit. **Host services**: none. **Keys**: as above, and on
the key card.

**Fidelity**: the party's bar is changed from the first time it is drawn, so
the pair is a `contrast`: `move-walk-mode` against `move-walk`, 58 of 96
checkpoints identical, divergent from the first main-menu frame; the
seam-on half walks once in walking mode, comes back to the menu and opens
View off the bar, where the seam-off half walks and turns. Off, the engine
is not consulted (`docs/seams.md` §7). Unit: `SeamMoveMode.*`.

**On Curse of the Azure Bonds** (#555) the party's bar in a town or a
dungeon has the same two modes, with that program's facts in a row of its
own (`move_mode_facts`, `curse_move_mode`) and three points: either side
of the bar's one call, and the key read, the last of the four pieces
there. Each fact was read from the instruction's bytes in the store
copy's `GAME.OVR` and image.

| fact | Pool of Radiance | Curse of the Azure Bonds |
| --- | --- | --- |
| the adventuring input routine | overlay 14 (`0x730`) `0x0989` | overlay 15 (`0x9A0`) `0x08DF`, a frame of `0x13C` bytes |
| the party's bars: string; call and return | the city's `0x04B6`, `0x09D0` and `0x09D5`; the wilderness's `0x04DF`, `0x0C40` and `0x0C45` | **one**, `0x03D8`, `0x0927` and `0x092C`, which the overhead view keeps too: `Area` first, so `Move` takes its place as in the city, and the highlight's numbering is the program's |
| the frame: out-parameter, prompt, the copy | `BP-0x04`, `BP-0x33`, `BP-0x32` | the same. The copy's bytes, `BP-0x32` to `BP-0x0A`, overlap only View's buffer (`BP-0x2E` on), which View empties before it reads it |
| a letter off the bar | `A`, `C`, `V`, `E` (camp), `S`, `L`; nothing else | `A` (the overhead view, or `Not Here`), `C`, `V`, `E` (camp), `S`, `L`; nothing else, so `M` and `-` go round the loop |
| a raw key | `H`, `K`, `M`, `P` walk and turn; anything else to the party cursor. A typed letter not on the bar comes back raw | `H` walks, `K` and `M` turn, `P` turns round; anything else to the party cursor (overlay 20 `0x32CF`) and the roster is drawn again. **A typed letter not on the bar is thrown away by the routine itself** |
| the digits, raw | through the table at `0x288C` | through the table at `0x27D0`, the same keypad layout |
| Return at the bar | `bar-keys`' Enter table | the routine's own: the lit group's letter, `M` on `Move` and `E` on `Exit` |
| Esc at the bar | the program's | handed back as `0`, which the loop goes round on; walking, the piece makes it `E` |
| the overhead view | `0x6AAC` | `0x7228`. The composer (`0x00ED:0x00CF`) clears it itself in the adventuring mode and draws the 3D view |
| the door bars | `0x0EBF`, `0x0FFE` | `0x0CD7`, `0x0E16` |
| a notice: the group count; the pick-list's call | `BP-0x8E`; overlay 25 `0x0FE0` | the same; overlay 27 `0x10F3`, a near call from inside it. A script's continue prompt comes from the script runner's call (overlay 8 `0x1811`) with one group, as a sign's does |
| the highlight | `0x6B2B` | `0x72AE` |

- **The overland is not a walk.** Curse's travel mode is a journey menu,
  a bar of the script's (`ENTER CITY`, `JOURNEY ON`, `CAMP`), which is
  another caller to this piece: it has no `Move`, Left and Right step it
  (the bar keys), and it ends a walk. The party's next bar in a town or a
  dungeon is in menu mode with `Move` lit.
- **The automap and the journal** need nothing new. Curse's automap names
  the party's bar by the call's far return as well as by the pointer
  (`note_one_bar`: `0x092C` in the segment overlay 15's word names), so
  Tab opens the panel over the copy; the journal splices `Notes` into the
  program's string before the copy is made and takes it out after, so the
  menu bar carries it and the walking one does not.
- **Driven** on the Steam copy, slot A, with the journal and the automap
  on: at the opening room `Move` is lit; Tab opens the panel, Down does
  nothing behind it, Tab gives the roster back with Mathew selected and
  Down selects Mark; Return gives `EXIT` alone, Escape the menu with
  `Move` lit; `N` opens the journal and its Exit gives the bar back; Left
  wraps to `NOTES`, Return opens it, and the bar comes back with `Move`
  lit. In the pair below the walk goes on through five continue prompts,
  the innkeeper's two at 6,13 and the signs' at 6,12, 4,7 and 5,3, the
  bar `EXIT` again after each Return. On the overland (slot D of `curse-wild`) Right, Right and Left
  step the journey bar, which is the program's.
- **Not driven**: a party in the overhead view when the bar comes up
  (the piece switched on there); the unit suite pins the clearing.
- **Fidelity**: `curse-move-walk-mode` `contrast curse-move-walk`, 19 of
  115 checkpoints identical, divergent from tick 48,382,638, the title's
  bar. The seam-on half takes `Move`, turns, comes back to the menu on
  Escape, takes `Move` again and walks from the opening room to the sign
  at 5,3 through five continue prompts, ends the walk with Escape,
  selects Mark with Down, and opens View for him with Right, Right and
  Return; the seam-off half toggles the overhead view, makes the same
  walk and turns. Unit:
  `SeamMoveModeOnCurse.*`.

## Text entry (#504)

`seam_text_entry.cpp`. Not a control: it notes when the program is inside
its line editor, so a host can put a keyboard up (`docs/hosts.md` §12). It
draws and changes nothing.

| point | module | what the handler does |
|---|---|---|
| image `0x7A73`, the line editor's first instruction (`1709:09E3`, `push bp`) | the resident image | sets the seam's word 5 |
| image `0x7BF3`, the editor's one return (`1709:0B63`, `retf 8`) | the resident image | clears it |

- **The addresses by two routes.** The resident disassembly, where every
  jump in the routine lands between those two instructions and the `retf`
  is the only return, and the unpacked image's bytes, `55 89 E5` at
  `0x7A73` and `CA 08 00` at `0x7BF3`.
- **All four callers** (the edit keys' table above) go through the
  entry. Driven on the store release: the code word's prompt reads as
  reading from the moment `INPUT THE CODE WORD:` is up, and not after the
  word is entered; the character's name reads as reading from the empty
  `CHARACTER NAME:` prompt until Return, and not on the portrait bar after
  it. A script's two prompts have not been driven.
- **State**: word 5 of the seam's own, observation, dropped by `enable()`
  and `reset()`. `move-mode` holds words 0 to 4. **Host services**: none.
  **Keys**: none.
- **Fidelity**: the handlers read and write nothing of the machine's, so
  every session with `modern-controls` on verifies as it did. Unit:
  `SeamTextEntry.*`, `AbiTextEntry.*`.

## Aim watch (#523)

`seam_aim_watch.cpp`. Not a control: it notes where the combat aim bar's
routine keeps its frame while the bar waits for a key, so the scene can say
where the cursor is (`docs/hosts.md` §13). It draws and changes nothing.

The aim command is overlay 13 (file offset 75771, 15474 bytes; the manager
keeps its segment in the word at image `0x690`). It aims two ways, each a
routine with a `push bp; mov bp,sp` frame that waits for a key through the
menu-bar routine:

| way | routine | in its frame, from BP |
|---|---|---|
| target to target | `0x2AA0` | the target's record, a far pointer, at `+0x0C`; the acting figure's at `+0x10`; the weapon's reach at `+0x0A`; an attack (not a spell's aim) at `+8`; the range it prints at `-0x30`; its bar string at `-0x2A`, which holds the word Target only where the target may be aimed at, until it is rebuilt as the whole bar |
| manual | `0x2DFA` | the cursor's column at `-0x33` and row at `-0x34`; the range word at `-0x38`; the byte at `-0x39`, set where the line from the acting figure arrives and then cleared wherever Target is not offered; the record the panel shows (the figure on the square, or a body lying there), a far pointer, at `+0x14`, zero for none; an attack at `+0x10` |

| point (overlay 13) | what the handler does |
|---|---|
| `0x2BB6`, where target to target's branches meet, the bar string decided and not yet rebuilt | notes target mode, SS and BP, whether the bar string is empty (Target not offered) and whether this is an attack (a range printed) |
| `0x2C31`, that routine's return from the menu-bar routine | notes the wait over |
| `0x2F84`, where manual's line branches meet, before the range is halved | notes whether the line arrived and this is an attack (a range printed) |
| `0x310A`, where manual's Target branches meet, before the bar is built | notes manual mode, SS and BP, and whether Target is offered |
| `0x3178`, that routine's return from the menu-bar routine | notes the wait over |

- **The offsets by two routes.** Overlay 13's disassembly (each frame built
  at its entry; the target pointer handed to the sheet drawer and compared
  with the acting figure's; the range byte printed and compared with the
  reach; the cursor's bytes clamped to 49 and 24 and compared with a body's
  column and row) and the module's bytes at each point. The two returns
  are also the ones `bar-keys` names these bars' calls by, found through
  the menu-bar routine's frame.
- **The frame, not a copy.** While the bar waits, the frame is the
  program's own record of the cursor, the panel's record and the range,
  and it stays put until the call returns, so the scene reads them there.
  The seam keeps what the machine no longer holds where anyone can find
  it: where the frame is (BP has moved on by the time a host asks) and the
  two decisions the frame has overwritten by then.
- **Only while waiting.** Between keys the routine moves the cursor and
  redraws the bar with the frame being rewritten, and the answer is "not
  waiting" for that moment. Driven on the baseline edition, slot J's
  fight (`--wall none`): from target to target the cursor went from the
  acting figure (range 0, no Target) to each other figure in sight with the
  bar's `RANGE` and its Target each time; manual followed the arrows across
  empty ground with the range the bar printed and nobody under the cursor;
  Exit went back to target to target, and a second Exit ended the aim.
- **State**: words 6 to 9 of the seam's own, observation, dropped by
  `enable()` and `reset()`. **Host services**: none. **Keys**: none.
- **Fidelity**: the handlers read the routine's frame and write nothing of
  the machine's, so every session with `modern-controls` on verifies as it
  did. Unit: `SeamAimWatch.*`, `SceneCombat.*`.
