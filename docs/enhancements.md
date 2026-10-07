<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# The enhancements

What this emulator adds to the game, as a player meets it. `docs/seams.md`
is the mechanism and the house style for writing another.

**Every enhancement is off by default**, and with all of them off the
machine is a plain machine (`docs/seams.md` §7 is the test). **Nothing is
injected into the game**: a seam is native C++ that stops the program at
an address, reads or writes memory, and lets it continue. The program on
the disk and every file the game owns are never modified.

## Answer the code wheel once

**What it does.** The game asks you to look up a word on the code wheel
before it starts. **It asks you once.** The first time, the challenge
appears exactly as it always did and the seam only watches. Answer it
correctly, off whatever form of the wheel you own (the cardboard wheel,
the manual, the code generator application the current releases ship),
and from then on the challenge is never drawn: the seam steps the program
past its own call into the copy-protection routine.

**How you turn it on.** `--seam code-wheel` on the desktop, the toggle on
the web page.

**Where it is remembered.** A one-line-per-copy text file,
`code-wheel.txt`, in the per-user data directory (`%APPDATA%\amberfolio\`,
`~/Library/Application Support/amberfolio/`, `$XDG_DATA_HOME/amberfolio/`)
or wherever `--code-wheel-store` says; in a browser, this browser's own
storage. It holds the SHA-256 of the copy you answered for and nothing
else. `--forget-code-wheel`, the *Ask me again* button, or deleting the
file asks again.

**What it will not do.** Answer the challenge for you. Nothing in this
build gets past the wheel without a person having answered it once.

## The Encamp Fix

**What it does.** Puts a **`FIX`** command on the camp screen's own bar.
Press its letter and the party rests as long as it needs: the cures it
already carries are cast through the game's own cast driver (one queued
back for each spent), then the game's own rest is dialled to the days the
wounded still need. A framed report, drawn by the game in its own font,
says what happened; a rest the game interrupts reports `Fix:
Interrupted!` and does not retry.

**How you turn it on.** `--seam encamp-fix`.

**With nobody holding a cure ready** — the ordinary state of a party
after a hard fight — the rest is the whole of the healing, and the
report says so: hit points, the days it took, and no spell. The rest is
then as long as the worst wound, so in an area the game rolls wandering
monsters for it is likely to be interrupted before it finishes.

**What it will not do.** Write hit points, mend the wound statuses a
fight leaves that resting cannot mend, or memorize a cure into a slot
that was empty to begin with.

## The automap

**What it does.** A map of the squares your party has walked, drawn over
the party roster on the game's own screen. **Tab** shows and hides it.
Walls are the colour of the tiles the 3D view draws for them, a door leaf
is drawn where a wall face's kind has been seen shut, and the zone's name
is set in the program's own glyphs. It comes down on its own whenever the
bar on the screen is not the adventuring screen's own, and comes back
after the journal has used the same cells.

**How you turn it on.** `--seam automap`. To keep the map between runs,
`--save-sidecars` on the desktop or `saveSidecars(true)` on the page
(`af_web_save_sidecars`; `hosts/web/tools/drive.mjs` is the reference
caller), which writes `\SAVE\AFMAP<L>.DAT` beside the game's saves each
time the game saves slot `L` and reads it back when you load that slot —
and the journal's read log beside it, on the one flag. A new game starts
with an empty map, and what you explore without saving goes with the
session. The sidecars are off unless asked, because a file appearing in your
game directory changes it: **the desktop asks you before your first run
and the page asks in a panel**, once each, and remembers what you said
(`docs/hosts.md` §2b). Neither file appears until there is something to
put in it, so answering yes and then saving straight away leaves your
directory as it was.

**If you are writing a host**: turn the store on **once, at install,
after the files are in and before the program is loaded**, whatever the
seam's state. Every call re-attaches and reading the sidecar replaces
every record, so turning it on mid-session discards what the player
walked. A save with the store on and the seam off writes **nothing**: a
sidecar with no records in it is its header alone, and one of those goes
over a file that already exists and never into a directory that has
none.

**What it will not do.** Map the overworld; that is the explored overlay.

## The explored overlay

**What it does.** Fog of war on the game's own overworld map. The squares
your party has stood on are the game's own map; every other square is
hazed with a fine black checkerboard, so the shape of the country shows
through. The reveal radius is zero: the square you are standing on.

**How you turn it on.** `--seam explored`. There is no key. It shares the
automap's store and its table, so squares walked with only the automap on
are already clear.

**What it will not do.** Draw a grid, a border or lettering. The design
and every rejected candidate are in `docs/explored-overlay.md` §5.

## The journal

Two halves that meet at a text file on your own machine.

### Ingestion

**What it does.** Locates each entry inside your own Adventurer's Journal
PDF off a fact table for that edition, crops it, reads it once with an
OCR engine, and keeps the text: a file beside the config on the desktop,
this browser's own storage on the web (the page's *Forget it* button
empties it). Entries that are drawings are reduced to four tones and kept
beside the text. Corrections are a second field per entry and survive
re-ingestion.

**How you turn it on.** `--journal <your PDF>` on the desktop
(`--journal-ocr PATH` names your installed Tesseract; `--journal-store`
moves the file); the file input on the page.

**What it needs.** An OCR engine: Tesseract, run as a program on the
desktop, loaded as the pinned `tesseract.js` from the page's own origin on
the web, never from a CDN. A recognised edition: the table has one row
(`docs/journal.md` §3 is how to add one); any other PDF is refused with
its fingerprint. Pictures need a page decoder, which only a desktop build
with the engine linked in has; a default build reports `pictures=0/14`.

### The reader

**What it does.** When the game cites an entry, tale or proclamation, it
goes on your list — the game's own narration is what tells you, and the
entry is there to read when you want it. A **`Notes`** command on the
party's own bar opens that log, newest first with a `*` on the unread,
as a full screen. Picking a row opens the entry on the game's screen in
the game's font, a full screen with `NEXT`, `PREV` and `EXIT`; a picture
is the page after its caption. Closing gives the screen back through the
program's own composer. **`Notes` is the only way in**, and that is why:
it is a command on the party's own bar, which is the one place a whole
screen can be handed back.

**How you turn it on.** `--seam journal`, and `--journal-store` if the
text is not where the host would look.

**What it matches.** The citation's *shape* only: the section's word
(entry, tale, proclamation, and plurals) followed by a number in that
section's notation (decimal, or Roman for proclamations), never a word
of the program's prose.

**A cheat for proof-reading** (#301): `--cite-all-journal`, or *Cite them
all (cheat)* on the page, puts every entry on the log so the OCR text can
be read off the game's screen. It is a host action, not a seam, and the
log stays filled until the store's `seen` lines are removed.

**What it will not do.** Open an entry the game has not sent you to. The
log is what the story has told you to read, and the reader shows the log;
to read the rest of a journal, `--cite-all-journal`.

## Modern controls

**What it does.** One switch for the keys and the colours a later game would
have. The game was made for a keyboard of its day: it takes a command by its
first letter, steps a list with Home and End, lights a selection white from
end to end, and types an arrow's letter into a name. With `modern-controls`
on:

- the **arrows** step the pick-lists and the selected party member, as Home
  and End do (the list arrows);
- **Left and Right step a command bar's highlight, Enter takes the lit
  command and Esc answers No** (the bar keys);
- the **main menu has a cursor** (the menu cursor);
- at the main menu, the number row's **1 to 8 select a party member**, and
  the party list shows each number (the hero keys);
- **walking is a mode** on the party's own bar: `Move` starts it and `Exit`
  ends it, and until then Left and Right step the bar; `Area` is gone, the
  automap being the overhead view (the move mode);
- the arrows and the function keys **type nothing at a text prompt** (the
  edit keys);
- **every selection is a yellow block**, its letters cut out of it, and a
  command's key letter a white one (the selection block).

**How you turn it on.** `--seam modern-controls`, or the toggle in the panel.
It is one setting: the game's own keys and colours, or all of this. Each
part is described below, with where it works and what it leaves alone.

**When it shows.** The menu's cursor from the first frame the menu is drawn,
the main menu's numbers, the party's bar with `Move` and the blocks at the
next thing the game draws, and
each key where the part that takes it says. The panel reads `on armed` from
the start; a part whose screen the game has not loaded yet (the pick-lists'
and command bars', the main menu's and Modify's) waits for it, and the rest
work meanwhile.

### The list arrows

**What it does.** The up and down arrows step the highlight in the game's
pick-lists, as Home and End already do. A list that ignored the arrows (at
character creation, in the spell and item lists, and in every other list
the game builds with its one list routine) now follows them, one row a
press, wrapping and skipping its headings as it does for Home and End.
The party-member picker (who a spell is cast on, who an item is traded
to) steps the same way. Keypad 8 and 2 come along, as 7 and 1 always did.

**At camp and the other bars.** Home and End step the selected party
member at the camp bar, its Magic and Alter bars, the post-combat
treasure bars, the shops, the temples and the game's yes/no style script
prompts; any other key put the selection back on the first member. With
the seam on, **Up and Down step the member too**, as Home and End do. On
the party-order screen, with a member picked up, they move it up and down
the order. **The keypad's 8 and 2 do the same** with Num Lock on, at the
same bars: 8 steps back and 2 steps forward. The number row's 8 and 2 are
not touched, and are what the game makes of them.

**At a door.** The bar at a locked or stuck door, camp's Portraits and
Monsters bar and the save slot bar throw the arrows away, because the game
reads them with its bar in a mode that has no Home or End either. With the
seam on, Up and Down step the selected member there too, by the same rule
(Up from the first goes to the last, Down from the last stays), and the
party list is drawn again by the game's own routine.

**Where it works.** Inside a pick-list or the party-member picker, and at
the bars named above. Everywhere else the arrows are the game's own: in
the 3D view, the wilderness and combat they move the party, in the rest
time menu they raise and lower the time, in the stat editor they pick a
score, and at the main menu they are the menu cursor's. The seam names the bars it
steps; it does not guess.

**What it will not do.** Change the Yes/No prompt or the ability-score
screen, step the member from the keypad's 8 and 2 at the adventuring bars
(they walk and turn the party there, as the arrows do), or make a held key
repeat (the hosts drop OS key repeats, `docs/hosts.md`).

### The bar keys

**What it does.** Makes the game's command bars answer the keys a player
reaches for first. **Left and Right step the highlight** along a bar, as
`,` and `.` always did, wherever the game would otherwise throw them away.
**Enter takes the highlighted command**, as if you had typed its letter,
at nearly every bar that would have ignored it, and at the rest-time menu.
**Esc answers
No** at a Yes/No question. The bar, its highlight and its commands stay the
game's own; nothing is drawn.

**Where Enter works.** The game hands Enter back to whoever called the
bar, and each caller does as it likes with it; this seam takes it at the
callers that ask again, and leaves the ones that use it. At the Yes/No
prompt the highlight starts on `No`, so Enter answers No; Left steps it to
`Yes` first. The same goes for a question an event script asks, such as the
arena master's *do you duel?*: whatever is highlighted is what Enter answers.
A script's *press Enter to continue* is the game's own: Enter already
continues it. These take the command under the highlight: the camp bar
and its Magic, Alter and game-speed bars and Alter's portraits and monsters
bar; the adventuring bar (in menu mode, `Move` among its commands, and while
walking, its one `Exit`); the portrait bar at character creation (`Head Body
Keep`); the shops' and temples' bars and the temple's appraisal; combat's
command, Done and game-speed bars; the View bar; the post-combat treasure
and Take bars; the load-game slot bar, and a locked or stuck door's bar (where the game
would take Return as no choice). A command the other seams add,
the Encamp Fix's `Fix` and the journal's `Notes`, is taken the same way,
and so are the bars of the Notes screen itself (below).
**The rest-time menu** is the one place the game used Enter for itself (it
meant Rest) and the seam takes it instead: it follows the highlight, which
opens on whichever word the camp bar's `Rest` left it on, `Mins`, so Left
and Right to `Rest` first.

**Where Left and Right work.** On every bar but the few that use the
arrows themselves. Those keep them: the adventuring bar, where they turn
and move the party; combat's move and aim cursors; the stat editor, where
they lower and raise a score; and two press-Enter prompts after a
fight. At the camp bar and its Magic and Alter bars the
game used an arrow only to put the selected party member back on the
first, and nobody presses an arrow for that; with the seam on they step
the highlight instead, and Home and End still step the member.

**Where Esc answers No.** At the Yes/No prompt (quit to DOS, keep this
character, and the others that use it) and at an event script's two-answer
question, *Yes* and *No*. Anywhere else Esc does what the game does with it.

**What it changes.** Two bars the game lets an arrow act on by accident,
because its scan code is also a letter. At the post-combat Take bar Right
used to take Money (`M`), and at the temple's keep-or-sell prompt Left used
to keep the gem (`K`). With the seam on both step the highlight like any
bar, and the letters still work. And one by decision: the game's rest-time
menu picks its days, hours or minutes field on Left and Right, and with
the seam on they step the highlight instead, as at any other bar. `Y`, `H`
and `M` still pick the field.

**What it will not do.** Make Enter confirm a row in the pick-lists (it
already does), or take a command where the game gives Enter a meaning of its
own: the pick-lists and the party picker, the party-order screen, combat's
move and aim, the
temple's keep-or-sell (where it sells), the press-Enter notices, the icon
editor. Two bars drop Enter and are left alone on purpose, because a stray
Return there would do harm: the save-game slot bar (it would write the lit
slot) and the stat editor (its `Exit` discards the edit). Make a held
key repeat (`docs/hosts.md`) or step the pick-lists and the selected member
with the up and down arrows (the list arrows, above).

### The menu cursor

**What it does.** Gives the main menu a cursor. The main menu is the
party-setup screen at the start of the game (Create, Add, Load, Exit and,
once there is a party, Drop, Modify, View, Remove, Save, Begin), and the
same menu again at a training hall, where Train is on. The game has no
highlight there: a command is taken only by its first letter. With the seam
on, a cursor is on the first command as soon as the menu is drawn, **Up and
Down** move it over the commands the menu shows, skipping none that is shown
and wrapping at both ends, and **Return** takes the command under it, as if
you had typed its letter. The cursor is the game's own string routine's, as a
pick-list lights a row: the command's first letter a white block and the
rest of the row a yellow one.

**Visible from the start.** The cursor is on the first command whenever the
menu is drawn, so the first press moves it: Down, Down is the third command
shown. Taking a command, by its letter or by Return, ends in the game
redrawing its menu, and the cursor is on the first command again. A player
who only types letters still sees the cursor, which is the one way this
seam changes the menu without a key.

**What it will not do.** Move the party member: Home and End still do that
at this menu. Remember where the cursor was after a command, or follow the
bar's own highlight, which is not where the menu left it. Work at any other
screen: the pick-lists are the list arrows' and the horizontal bars are the
bar keys'. A press made while the cursor is still being drawn is kept and
used (the game draws a glyph at a time, so a move takes about a tenth of a
second of the game's time).

### The hero keys

**What it does.** Pick a party member with one key at the main menu (the
title screen's, and a training hall's). **The number row's `1` to `8`
select that member**, and the party list there shows each member's number
in white in front of the name: `1 FIGHTER1`. The game's own cursor does
the selecting, so the list redraws as it does for Home and End.

**Everywhere else** Up and Down select the member (the list arrows at
camp, the shops, the temple, a script's menus, the treasure bars and the
doors; the move mode at the party's own bar), so the number row does what
the game has it do: what the keypad does. `9` and `0` are always the
game's. A digit with no member behind it does nothing.

**Where it does not work.** Every screen but the main menu, as above. With
the journal reader up, `1` to `8` are its keys, as every key is.

### The move mode

**What it does.** Makes walking a mode, as the later games in the series
did, so the exploring bar can be stepped with the arrows that walk. You
arrive at the party's bar with **`Move` lit**, in the place `Area` had (in
the wilderness, in front of `Cast`). There, Left and Right step the
highlight, Up and Down select the member before or after, Enter takes the
lit command, and the bar's letters are the game's; a letter that is not on
the bar does nothing. With the map open over the party list, Up and Down
do nothing. **`M`, or Enter on `Move`,
starts walking**: the bar reads `Exit` alone, and the arrows, the keypad
and the number row walk and turn as the game has them do. **Enter, Esc or
`E` stop walking** and light `Move` again. A fight, camp, a shop, a
script's question, View or any other screen with a bar of its own also
brings you back to the bar with `Move` lit; the steps you take, what
happens on a square and a locked door's question do not.

**What it changes.** `Area`, the game's overhead view, is gone: the
automap (Tab) is the overhead view, and with the automap off there is
none. While walking, the letters do nothing, `Notes` among them; the
walking bar is `EXIT` alone, lit as any selection is: `E` a white block and
the rest a yellow one.

**Where it does not work.** Only the party's own bar has modes. Combat's
movement and aim, and Modify, keep their arrows.

### The selection block

**What it does.** Draws every selection you can move as a block of yellow
with its letters cut out of it in black, and a command's key letter as a
white block. Today the game lights a selection white from end to end, so the
capital letter that is the key disappears into it; and a selection told
apart by colour alone is lost to a player who cannot tell the colours apart.
A block is a shape, and reads whatever colours you see. With the seam on:

- **A command bar's highlighted word** (the adventuring bar, the camp bar,
  Magic, Alter, the shops, the temple, a script's menus, the save slots and
  every Yes/No question) is a yellow block with its key letter a white one.
  Only the word is lit: on a bar whose keys are not the first letters of its
  words, like the icon editor's top bar, the game's own highlight runs on
  into the next word, and the seam lights the word that holds the key.
  A bar of one command is what Enter takes, and is lit the same way: a
  prompt with only one choice, such as `PRESS <ENTER>/<RETURN> TO
  CONTINUE` (its `P` a white block and the rest a yellow one), and the
  walking bar's `EXIT`. The `Exit` under a pick-list is not lit: Enter
  there takes the list's row.
- **A pick-list's highlighted row** (race, class, spells, shops, coins, the
  list of characters to add) is a yellow block.
- **The selected party member's name** in the party list is a yellow block,
  in the 3D view, in camp, in the party-member picker and on the main menu.
  A number the hero keys draw in front of it stays white on black.
- **Modify's selected score**, which was light magenta, is a yellow block,
  hit points included.
- **The menu cursor's row** on the main menu is a yellow block with its
  first letter a white one, and, with `journal` on, the Notes list's cursor
  row is a yellow block across the row. The lit command on the Notes
  screen's bar is a yellow block with its letter a white one, a lone
  `EXIT` included.
- **A bar the game hands its colours the wrong way round** (the portrait
  screen's HEAD, BODY and KEEP bar, the temple's "pay for cure" and the
  detect-magic confirmations) is drawn like every other bar: key letters
  white, the rest of each word green. Without the seam they are white
  where the others are green, and green where they are white.

A block has a margin of one pixel all round, so its letters do not touch
its edges: the game's and the faces' letters leave a column on their left
and a row beneath, and the seam paints the top and the right in the cells
beside the block, only over black, and takes them back when the selection
moves. The lettering cut out of a block is the game's, or the face's when
`font-sans` or `font-chisel` is on. It is a look and has no key.

**When it shows.** At the next selection the game draws. A selection
already on the screen when you switch it keeps its look until the game
draws it again, which on most screens is the next key you press.

**What it will not do.** Mark anything that is not a selection among
text: the icon editor's cell cursor, the combat grid's cursor and the
hit-point colour of a hurt character are the game's own.

### The edit keys

**What it does.** The arrows, Home, End, the page keys, Insert, Delete and
the function keys stop typing letters where the game asks for text. Without
it, pressing Right while naming a character types an `M`, Up an `H`, Down a
`P` and Left a `K`; Home types `G` and End `O`. With it they do nothing.
Typing, Backspace, Return and Esc are as they were, and so is every other
screen: the arrows still walk, turn and step the lists.

**Where it works.** Every place the game reads a line of text: a new
character's name, a script's free-text question, a script's number prompt
(where a stray letter made the game ask again) and the code word at the
copy-protection challenge. They share one routine in the game, so they are
one setting.

**What it changes.** An Alt chord and the function keys type nothing at those
prompts either. The View > Drop money amount is the game's own and takes
digits only, so an arrow was never wrong there.

## The text faces

**What they do.** Redraw the game's lettering in a face of your choice,
in place of its own, everywhere it puts text on the screen: menus,
the character sheet, the message panel, the journal reader. Two faces,
both drawn for this project on the game's own eight-by-eight grid:
**`font-sans`**, a plain bold sans with two-pixel strokes and a slashed
zero, and **`font-chisel`**, the same face with every stroke cut at an
angle as if by a broad pen. Each letter sits in its cell where the game's
own do, a blank column on its left and a blank row beneath, so a
selection's block keeps its margin. Only the letters, the digits and the
punctuation change. The frame pieces, the blocks and the runes the game
keeps in the same table stay its own, and the game still draws only
capitals, because that is all it asks for.

**How you turn one on.** `--seam font-sans` or `--seam font-chisel`; a
toggle each in the panel. They are alternatives: turning one on turns the
other off.

**When it shows.** At the next character the game draws. Text already on
the screen keeps the face it was drawn in until the game draws it again,
which on most screens is the next key you press.

**What else follows it.** Screen text (`docs/hosts.md` §10) reads the
screen in whichever face is on, and the automap's zone label is lettered
in it.

**What it will not do.** Touch the game's own font: the seam changes each
row of a glyph as the game reads it, so off is the game's own lettering
again.

## The debug cheats

**What they do.** `cheat-invulnerable` (the party takes no damage),
`cheat-kill-all` (every enemy takes 120 damage, when pulled),
`cheat-wound-party` (the whole party drops to one hit point, when pulled
at camp). Each writes through the path the program's own routines use.

**How you turn them on.** `--seam cheat-…`; the last two are pulled
(`--pull`, or the button beside the toggle) rather than left on.

**What they are.** Test tooling, not a player feature.
`cheat-wound-party` exists so the Encamp Fix could be driven on a hurt
party. The numbers 120 and one hit point per member per day are chosen,
not measured.

## What is not here

A toggle panel and guided onboarding are M6 (#265). Save and roster
management was withdrawn from v1 (#176). Open gaps in the enhancements
above: #270, #312.
