<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# The enhancements

What this emulator adds to the game, as a player meets it. `docs/seams.md`
is the mechanism and the house style for writing another.

**Every enhancement is off by default**, and with all of them off the
machine is a plain machine (`docs/seams.md` §7 is the test). **Nothing is
injected into the game**: a seam is native C++ that stops the program at
an address, reads or writes memory, and lets it continue. The program on
the disk and every file the game owns are never modified.

## The code-wheel bypass

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
caller), which writes `\SAVE\AFMAP.DAT` beside the game's saves, with a
snapshot per save slot — and the journal's read log beside it, on the one
flag. The sidecars are off unless asked, because a file appearing in your
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
