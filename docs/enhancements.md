<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# The enhancements

What this emulator adds to the game, one entry each: what it does, how a
player turns it on, what it will not do without, where its facts came
from, what makes it feel like something the game shipped with — and what
it is not yet.

`docs/seams.md` is the mechanism these are built on and the house style
for writing another. This is the other side of that: the enhancements as
a player meets them, in the voice `docs/playable.md` uses.

Two sentences hold for every one of them and are worth having up front,
because everything below assumes them.

**Every enhancement is off by default.** Not "defaults to off" — with all
of them off, the machine is a plain machine, and that is a test rather
than a claim: a run's state hash with the engine present and idle equals
the hash of the same run on a build with no engine at all
(`docs/seams.md` §7). Seven sessions in `tests/sessions/` say the same
thing about each seam on the real program.

**Nothing here is code injected into the game.** A seam is native C++
compiled into the emulator, which stops the program at an address, reads
or writes memory, and lets it continue. The program on the disk is never
modified, and neither is any file the game owns.

---

## The code-wheel bypass

**What it does.** The game asks you to look up a word on a cardboard
wheel before it will start. **It asks you once.**

**How you turn it on.** `--seam code-wheel` on the desktop host, or the
toggle on the web page.

**How it goes, the first time.** Exactly as it always did. The challenge
comes up and the seam does nothing at all but watch: it does not answer
it, it does not hint, and it cannot — with this seam on and the question
unanswered the machine is byte for byte the machine with no seam. Look
the word up on whatever form of the wheel you own — the cardboard one,
the manual, the code generator application the current releases ship
instead of a wheel — and type it. Get it right and the seam sees the
program's own comparison come out equal, and says so once.

**How it goes after that.** The challenge is not drawn. The seam moves
the program past its own call into the copy-protection routine, so the
game goes from its titles to its menu and nothing asks you anything.

**What it will not do.** Answer the challenge for you. There is no path
through this build that gets past the wheel without a person having
answered it once — which is the same possession claim the old gate made
(*you hold the wheel*, and nothing else about you), by the one route
that still works. It was gated on a **PDF** of the wheel until #290:
present the file, arm the seam. The releases sold today do not ship that
PDF, so the file demonstrated nothing about the player who most needed
it.

**Where it is remembered.** On the desktop, one small text file in the
place this program keeps your per-user data — `%APPDATA%\amberfolio\` on
Windows, `~/Library/Application Support/amberfolio/` on macOS,
`$XDG_DATA_HOME/amberfolio/` on Linux — called `code-wheel.txt`, or
wherever `--code-wheel-store` says. In a browser, this browser's own
storage. Either way it holds the **SHA-256 of the copy you answered for**
and nothing else: not the question, not the answer, not when. Keyed by
the copy, so answering for one edition does not answer for another.

**How to be asked again.** `--forget-code-wheel` on the desktop host, the
*Ask me again* button on the web page, or delete the file — it is a text
file with one line per copy, and you are meant to be able to read it.

**Where its facts came from.** Two addresses. The instruction where the
program compares what you typed against its own table, which is where
the seam watches; and the far call in the boot that puts the challenge
on the screen, which is where it steps over. The routine behind that
call sets nothing and returns on success, so a program that never called
it is a program that called it and got it right.

**What makes it native.** Nothing is drawn that the game does not draw,
and nothing is skipped that the game does not skip: the call the seam
steps over is the same one the program's own documented boot word steps
over. There is no dialogue, no message, and no acknowledgement on the
game's screen — the answer is remembered by the host, not announced by
the machine.

**What it is not yet.** Nothing outstanding in the seam. Where the
answer is kept is #292, and the committed sessions still boot past a
challenge this seam no longer answers, which is #293.

---

## The Encamp Fix

**What it does.** Puts a **`FIX`** command on the camp screen's own
command bar. Press its letter and the party rests as long as it needs to:
the cures it already carries are cast, and then the game's own rest is
dialled to however many days the wounded still need.

**How you turn it on.** `--seam encamp-fix`.

**Where its facts came from.** The camp loop's four points, the game's
own cast driver, its own rest wrapper, and the out-parameter that says a
rest was interrupted. All of them addresses and offsets, gathered by
driving the real program.

**What makes it native.** It spends the party's *own* cures through the
game's *own* cast driver, and queues one back for every one spent, so
nothing appears from nowhere. It dials the game's own rest clock rather
than writing hit points. The command is spliced into the string the
program draws its bar from, so the word is in the game's lettering, on
the game's bar, chosen the way every other command on it is chosen. The
report afterwards is drawn by the *game*, in the game's font, through the
routine the game writes its own messages with — and it is held on the
screen by the program's own message delay.

And when the game interrupts a rest — a wandering monster — the fix says
so, in the game's own words' place: `Fix: Interrupted!`. It does not
retry, and it does not pretend the rest finished.

**And the report leaves when camp does** (#298). For a milestone its
title stayed: the game's own frame puts a title on the box's top row,
and the camp screen's own clean-up blanks every row of that panel but
that one, so `FIX: PARTY HEALED` was still on the adventuring screen
after EXIT — a player found it there. The title is one row lower now, on
the first row the game clears, and the box gave up a row of its
exception list to make room; the list already said `...and N more.` when
it ran out, so it says it one member sooner.

**What it is not yet.** Nothing outstanding in the mechanism. Two
residuals are filed as **#269**: a party hurt by *combat* rather than by
a debug seam has never camped, so the wound statuses a fight leaves
behind — the ones resting cannot mend — reach the report's reason column
only through rosters the unit suite writes; and a rest of a day or more
cannot print its elapsed time, because the game's clock has no day
counter and the summary drops the clause rather than print the remainder
of a wrap as though it were an answer. And the box has not been looked
at on a display since its title moved: the rows are measured on stills
(`tests/visual/camp-fix-exit.leg`), and the look is **#274**'s.

---

## The automap

**What it does.** A map of the squares your party has walked, drawn over
the party roster on the game's own screen. **Tab** shows it and takes it
away.

**How you turn it on.** `--seam automap`. Its map survives the machine if
you ask a host to keep one: `--automap-store` on the desktop,
`af_web_automap_store` on the page, which writes `\SAVE\AFMAP.DAT` beside
the game's saves and never inside one, with a snapshot per save slot so
two playthroughs do not share a map.

That is **off unless you ask**, deliberately: a file appearing in your
game directory changes your game directory, and every recorded session
pins its disk.

**If you are writing a host, the store has one trap**, and it is the
kind that costs a player their map rather than an error. Ask for it
**unconditionally at install** — after the files are in the machine and
before the program is loaded out of them — whatever the automap seam's
own state. The store and the seam are two independent flags, not a
feature and its switch; `hosts/web/tools/drive.mjs` is the reference and
treats them as two. Turning it on *later* is worse than late: every call
re-attaches, and reading the sidecar **replaces** every record in the
table, so a store switched on mid-session discards everything the player
has walked up to that point. The consequence of doing it right is worth
accepting out loud: with the store on and the seam off, a save still
writes a header-only `\SAVE\AFMAP.DAT`, so that file appears in a copy
the host persists even for a player who never opened the map.

**Where its facts came from.** Six addresses in the resident image, the
overlay tracker, and the shipped data's own table of shut wall faces. The
zone names are a table of (disk, area) to a short label, because the
program holds no such string to read.

**What makes it native.** It is drawn into the EGA planes a plane at a
time, in the game's own sixteen colours, at the game's own resolution —
so it is not a modern layer over the picture, it is in the picture. A
wall is drawn in the *modal non-black colour of the very tiles the 3D
view blits for it*, so the buildings are the colour of the buildings. A
door leaf is drawn where a wall face's kind has been seen shut. The
zone's name is set in the program's own glyphs, read out of the
program's own font.

Tab is claimed by removing it from the BIOS keystroke buffer before the
program's own key routine looks, so the program observes exactly what it
would have observed had the key never been typed.

And it knows when to get out of the way: it comes down, and its key goes
quiet, whenever the bar on the screen is not the adventuring screen's own
— because a panel that stayed up over a vendor's question would be
covering the game with something the game did not ask for.

**What it is not yet.** It marks where you have been on the *city* and
dungeon screens. The overworld map is the explored overlay below, which
is a separate seam and shares this one's store.

The rest is **#268**: the door rule has only ever been driven through its
*fallback*, because New Phlan has no shut wall face anywhere on it, so
what a driven run proves is the table of shut faces and not the seeing;
nobody has walked through the gate at `0,4` with the panel up and watched
it change maps; and two playthroughs in two slots not sharing one map is
a test over file events rather than a run.

---

## The explored overlay

**What it does.** On the game's own overworld map — the five-by-five
window of a wilderness area you travel across — the country your party
has been near is the game's own map, and **everything else is under
fog**: a fine dark-grey haze, laid on every other pixel, so you can still
make out the shape of what is out there without being able to read it.
The fog lifts as you go, a square at a time, so what is plainly on the
screen is where you have been.

**How much you see.** The square you are standing on and the eight around
it. That is a reveal radius of one, and one is not an arbitrary choice:
the game shows you five squares across with your party in the middle, so
at a radius of two every square on the screen would already be uncovered
and the fog would never appear at all. Measured — 523 frames of a real
walk at radius 2, and not one of them different from the same walk with
this off. It is one named constant if a later run says otherwise.

**How you turn it on.** `--seam explored`, and that is the whole of it.
There is no key: it is a setting, not a command. On, it is there whenever
the game is showing that screen; off, it is not.

Its map survives the machine on the same terms the automap's does —
`--automap-store` on the desktop, `af_web_automap_store` on the page,
writing `\SAVE\AFMAP.DAT` beside the game's saves and never inside one,
with a snapshot per save slot. It is the same table: the automap records
the wilderness too, so if you have been playing with that on and turn
this on later, the squares you walked are already clear.

**Where its facts came from.** Three addresses in the resident image, a
handful of data-segment offsets, and two words inside the game's own area
record. `docs/explored-overlay.md` is the table, with the second route
that agreed for every line of it, and the pixel geometry measured off a
real frame rather than derived: the window is 120 by 120 pixels at
(8, 8), and a square is 24 by 24.

**What makes it native.** The fog's colour is one of the game's own
sixteen — index 8, the dark grey it draws with elsewhere — and it is laid
one pixel on, one pixel off, so half of what is under it is the game's
own picture, untouched. Nothing is added to the screen that the game does
not already have on it, and no shape is drawn that the game does not
draw: no grid, no border, no lettering on the map.

**Why a haze and not a solid cover.** The fog was solid black first, on
a good argument — black is the colour the rest of that screen already is,
so a covered window reads as the game's own border framing a smaller
opening. Then five coverings were put side by side over a real frame with
grass, coast water and the shore between them on it — solid black, a
black checker, a dark-grey checker, a light-grey checker and a coarser
dark-grey one — and looked at. The haze won, and the reason is the one an
argument had missed: a solid cover throws away the *shape* of the country
you are standing at the edge of, and you cannot see there is a coast to
walk to if you cannot see the coast. A black checker turned out to
collapse into a flat dark mesh against the grass's own two-green dither,
and a light-grey one just read as paler ground.
`docs/explored-overlay.md` §5 has all of them, black included, with what
each of them cost.

**This is the second design, and the first one is still written down.**
Until #263 the overlay did the opposite: it left the whole map on the
screen and redrew the squares you had walked one shade brighter. It was
measured to be visible on 2,800 squares of a real run, and when somebody
finally sat in front of it, it did not read — a shade is a difference you
have to be told about before you can see it. `docs/explored-overlay.md`
§5.1 keeps it with the six candidates it beat, because a design rejected
by looking at it is worth more than a design nobody tried.

**The square you are standing on is never covered**, and that is not a
detail: your party's icon is drawn there.

**What it is not yet** (**#267**). **Nobody has played with the fog.** Somebody has
now looked at it — that is how the colour was chosen — but on stills, and
the question this whole document is organised around, does it read as
something the game drew, is one only a person with the game running can
finish answering. That is exactly how the first design was replaced. It
has been driven on all three wilderness areas, and over mountain rock —
the terrain whose own art is largely the covering's own grey, which is
the case the composites never showed. It reads there: the tile's bright
half goes to a one-pixel mesh and the edge between covered and clear is a
straight cut on a cell boundary. What it costs is that a third of the
covering writes grey onto grey and does nothing, so the haze is thinner
over rock than over grass. Forest and roads are still not under it.

---

## The journal

Two halves, and they meet at a text file on your own machine.

### Ingestion: your own Adventurer's Journal, read once

**What it does.** Locates each entry inside your own PDF, crops it, reads
it with an OCR engine, and keeps the text on your machine — a file beside
the config on the desktop, this browser's own storage on the web. Read
once means once, not once per visit.

**How you turn it on.** `--journal <your PDF>` on the desktop; the file
input on the page.

**Where its facts came from.** A fact table of page rectangles and stream
offsets, measured off the scans of one edition — the archive release's
journal, fifty-eight entries in seventy-eight pieces across nine
two-page scans, every rectangle measured and every number checked against
the printed headings. `docs/journal.md` §3 is the method, so the next
edition is a procedure rather than an archaeology. An edition this build
does not know is **refused with its fingerprint**, because the offsets
are true of one file and following them into another produces twenty
failures rather than one sentence.

**What it will not do without.** An OCR engine. Tesseract, on both hosts,
and **linked on neither**: the desktop runs your own installed one as a
program, the page loads a pinned `tesseract.js` from its own origin and
never from a CDN.

**What it is not yet.** The edition table has one row. And no test has
ever run a real engine over a real page — CI proves the pipeline on all
four targets against a synthetic PDF this project generates, with a
fixture engine. Both live on **#236**, with the browser half of it: the
page's own ingestion has never been driven by a person in a browser, only
by a node harness.

### The reader: the entry, on the game's own screen

**What it does.** When the game cites an entry, tale or proclamation,
**the entry opens** — on the game's screen, in the game's font, with
nobody having pressed anything. A **`Notes`** command on the party's own
bar opens a log of everything the game has cited, newest first with a `*`
on what you have not read. **F1** opens a prompt for the ninety-odd
entries nothing ever cites.

**How you turn it on.** `--seam journal`, and `--journal-store` if your
text is not where the host would look.

**Where its facts came from.** Six addresses. Five are the automap's, and
the sixth is the program's word-wrapping **message box** — the routine
every one of the script's PRINTs ends at.

That sixth one is worth the paragraph, because it was wrong for two
milestones. The watch began on the routine that draws a string at a
cell — an address already in the tree, which felt like the careful
choice. On the real program that routine draws the credits, the menus and
the position line above the viewport, and **not one word of the story**.
Driving a new party to the city hall, where the game names four
proclamations in one sentence, produced no citation at all (#232). A
probe that reaches a routine says nothing about whether that routine sees
the thing you are watching for.

**What it matches.** The citation's *shape* and never a word of the
program's prose: the word a numbered section of the document is called
by — entry, tale, proclamation, each with its plural — and a number after
it in the notation that section is numbered in. Decimal for entries and
tales; a Roman numeral for proclamations, because that is how the booklet
prints them. After a plural, a list joined by commas and "and".

**What makes it native.** The panel is drawn into the game's planes in
the game's own font, in its highlight yellow and its message green, and
it is given back by asking the program to repaint its own roster. The
`Notes` command is spliced onto the game's own bar and chosen the way
every other command on it is. The page turns on the same key that opened
it.

**What it is not yet.** With the reader on, the machine is **not** the
machine it would have been with it off, even before anything is cited —
`Notes` goes on the bar the moment that bar is drawn. That is the
enhancement being visible rather than a leak, and
`tests/sessions/quiet-journal.session` measures exactly how much.

And only one real citation has been driven: the city hall's four
proclamations. The entry and tale forms are the pattern's word rather
than a measured sentence. **Nobody has read an entry off a display**
(#236) — the reader has been driven, dumped and confined by rect, all of
it file against file — and the rows the journal's own test plan still
owes are **#270**.

---

## The debug cheats

**What they do.** Three switches for somebody testing this emulator:
`cheat-invulnerable` (the party takes no damage), `cheat-kill-all` (every
enemy takes 120 damage at once, when pulled), and `cheat-wound-party`
(the whole party drops to one hit point, when pulled at camp).

**How you turn them on.** `--seam cheat-…`, and the last two are
**pulled** rather than left on — they act when a person asks, once.

**What makes them native.** Each writes through the same path the
program's own routines write: `cheat-wound-party` makes the write the
program's own damage routine would make for that damage, on a record the
program would accept.

**What they are not.** They are not a player feature and are not
presented as one. `cheat-wound-party` exists because the Encamp Fix's
days arithmetic and its report's exception list had no other way to be
driven.

**What they are not yet.** Three numbers underneath them are chosen
rather than measured, and **#271** is the list. One of the three was
measured at this milestone's closeout and was right — `cheat-invulnerable`
still fires nine times over `fight-cheat.rec` — which leaves
`cheat-kill-all`'s 120 points and the Encamp Fix's one hit point per
member per day.

---

## What is not here

**Save and roster management** was withdrawn from v1 by decision rather
than deferral: it was the one item that was not an in-game enhancement,
and nothing else in the plan needed it.

**A toggle panel** — turning these on and off from inside the page rather
than from a flag — is M6's, along with the guided onboarding that puts a
face on presenting a document and ingesting a journal.
