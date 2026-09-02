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
(`docs/seams.md` §7). Six sessions in `tests/sessions/` say the same
thing about each seam on the real program.

**Nothing here is code injected into the game.** A seam is native C++
compiled into the emulator, which stops the program at an address, reads
or writes memory, and lets it continue. The program on the disk is never
modified, and neither is any file the game owns.

---

## The code-wheel bypass

**What it does.** The game asks you to look up a word on a cardboard
wheel before it will start. This answers it for you.

**How you turn it on.** `--seam code-wheel` on the desktop host, or the
toggle on the web page.

**What it will not do without.** The code wheel itself. This enhancement
is **gated on the document**: present your own copy with `--document`,
and until you do the seam is inert and says so —
`seam code-wheel inert document_not_presented`. It is a *possession*
gate, which is the whole of what it claims: it demonstrates you hold the
wheel, and decides nothing else about you. Nothing reads inside the file;
a host hashes what it is handed and presents the digest (`machine/
document.h`).

**Where its facts came from.** One address: the instruction where the
program compares what you typed against its own table. It is answered by
making the program's own comparison report equal — the flags and the
count it would have had, had you got it right — rather than by skipping
the check. The rest of the program cannot tell.

**What makes it native.** It engages the program's own "you got it
right" path. There is no dialogue, no message, and nothing on the screen
that the game did not draw: the challenge appears and is passed, exactly
as it is for somebody with the wheel in front of them.

**What it is not yet.** Nothing outstanding. Its gate was the last of it
(#115).

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

**What it is not yet.** Nothing outstanding.

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
dungeon screens. The overworld map is a separate enhancement and is not
built (#179).

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
fixture engine.

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
than a measured sentence.

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

---

## What is not here

**The explored overlay** (#179) — the areas you have already been to,
marked on the game's own overworld map — is the sixth v1 enhancement and
is **not built**. It is the one item of the five with no proven prior
design, and PLAN.md §5's rule for that case is that the marking is
settled at the point of definition, with the reasoning and a dump beside
it, and judged native by a person before it ships.

**Save and roster management** was withdrawn from v1 by decision rather
than deferral: it was the one item that was not an in-game enhancement,
and nothing else in the plan needed it.

**A toggle panel** — turning these on and off from inside the page rather
than from a flag — is M6's, along with the guided onboarding that puts a
face on presenting a document and ingesting a journal.
