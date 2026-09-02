# The journal, tested

*A plan for testing the journal end to end — from a player's PDF to the
entry on the game's own screen — so that every claim about it is a check
somebody can run and every picture it draws is a picture somebody has
asserted. M5-E3 (#174) and M5-E4 (#175) and their sections; PLAN.md §5
item 2. Written 2026-09-01 against `9975f30`.*

`docs/journal.md` is what the journal is. `docs/seams.md` §10 is what
the reader is. `docs/playable.md` Leg 9 is the one driven leg. This
document is the gap between those three and a feature somebody can call
tested, and the work it lists is tracked on #239 and the seven issues
named in §6.

1. [What is under test](#1-what-is-under-test)
2. [Where coverage stands today](#2-where-coverage-stands-today)
3. [The visual method](#3-the-visual-method)
4. [The test matrix](#4-the-test-matrix)
5. [Harness work the matrix needs](#5-harness-work-the-matrix-needs)
6. [Order of work, and the issues](#6-order-of-work-and-the-issues)
7. [The browser, by hand](#7-the-browser-by-hand)
8. [Documents to correct](#8-documents-to-correct)
9. [Running it from a session on this machine](#9-running-it-from-a-session-on-this-machine)
10. [Facts the scripts rely on](#10-facts-the-scripts-rely-on)

## 1. What is under test

The journal is two halves and five surfaces. The host half ingests a
player's own Adventurer's Journal PDF into a text store. The seam half
reads that store back onto the game's screen. Every test below belongs
to one of these surfaces.

| Surface | What it is | Where it lives |
| --- | --- | --- |
| **Ingestion** | Locate each entry inside the PDF off the fact table, inflate or carry the page, crop, hand it to an OCR engine, keep the text. | `hosts/common/src/journal_*.cpp`; `--journal` on the desktop; the page's file input on the web |
| **Store** | One text file per player: scanned text, a correction per entry, and since #222 the read log. Version 3. Desktop: a file beside the config. Web: `localStorage`, with a Forget button. | `journal_store.cpp`, `page/journal.mjs` |
| **Citation watch** | A point at the program's string drawer that matches the shape "section word plus a number" over a rolling window and opens the entry with nobody pressing anything. | `seam_journal.cpp`, `journal_citation_in()` |
| **Reader panel** | F1 opens a number prompt, cycles the section, turns pages and closes. Draws in the automap's rect in the program's own glyphs. Modal over the map. | `seam_journal.cpp`, `machine/journal.h` |
| **Notes and the log** | `Notes` spliced onto the party's command bar; `N` opens a full-screen log of what the game has cited, drawn by the game's own frame and string routines and given back by its screen composer. | `seam_journal.cpp`, the overlay 14 points |

Three properties cut across all five and get their own rows: the
**fidelity invariant** (seam on and nothing cited equals seam off, byte
for byte), **cross-host equality** (the desktop and the wasm module draw
the same frame from the same script and store), and **persistence**
(what a run learned is there on the next run).

Out of scope: Tesseract's own accuracy, the fact table's measurements for
editions nobody holds, and anything about the automap beyond the pixels
it shares with the reader.

## 2. Where coverage stands today

Read from the tree at `9975f30`. *In CI* means on every push; *by hand*
means once, on the maintainer's machine; *never* means never.

| Surface | Check | Status | Where |
| --- | --- | --- | --- |
| Ingestion | Synthetic probe PDF through extract, OCR fixture and store, desktop and wasm | in CI | `journal_*_test.cpp`, `run-journal.cmake`, `smoke.mjs` |
| | The archive edition through a linked Tesseract: 99 of 99 recognized | by hand | `build/ocr-linked`, `docs/journal.md` §7 |
| | The archive edition through an *installed* Tesseract run as a program, which is the shipping path | **never** | `tesseract_ocr.cpp`; no Tesseract on this machine |
| | The archive edition through tesseract.js in a browser | **never** | #147's open state |
| Store | Round trips, versions 1 to 3, CRLF, limits, the log's order and cap | in CI | `journal_store_test.cpp` |
| | `localStorage` keep, restore and forget, over a fake drawer under node | in CI | `smoke.mjs` (#231) |
| | The same in a real browser, across a reload, with the drawer full or blocked | **never** | |
| Citation watch | The recognizer over strings a test writes; the window; the two-draw split | in CI | `JournalCitation.*`, `JournalWindow.*` |
| | The frame read at the real point, by rebuilding with the word set to `S` | by hand | `docs/seams.md` §10 |
| | **A real citation, in the program's own words, opening a real entry** | **never** | the one thing #175 left open |
| Reader panel | Prompt, section cycle, paging, refusals, keys, transliteration, fidelity, on a font a test hands it | in CI | `seam_journal_test.cpp`, 96 cases |
| | Leg 9 on both hosts off a hand-written store; final frames `cmp` equal | by hand | `docs/playable.md` Leg 9 |
| | Any committed assertion about what it draws | **never** | no session can pin a store |
| Notes and log | Splice on and off, both bars, cursor, Return, Escape, `E`, nothing reaches the program | in CI | `JournalNotes.*`, `JournalList.*` |
| | Driven at slot A; the #230 regressions found by a player and fixed | by hand | `docs/seams.md` §10; not yet a leg in `playable.md` |
| Fidelity | Seam on, nothing cited, no key: identical run. Unit, and 40M steps of the real program | unit in CI, the run by hand | |
| Persistence | The read log survives a reload on the web | **never** | known not to; `docs/journal.md` §6 says so |

Two things stand out. The reader has never been shown a citation the
game actually wrote, which is the whole enhancement. And nothing that
draws has a committed check on what it drew: the pictures have been
looked at, on the day, by the person who built them, and then nothing
pins them.

## 3. The visual method

A test of what the journal draws has to work under three constraints
this project already carries. No screenshot of the game may be
committed, because the frame is the game's own art. No test in the
repository runs the game, so anything that needs the disk runs in the
sweep on a machine that has one. And the game draws exactly the same
pixels for the same keys at the same ticks, so a hash is as good as a
picture once a person has looked at the picture once.

**The content rule, applied to pictures.** A PPM or PNG of any frame of
the game goes in the scratchpad and nowhere else. What gets committed is
a SHA-256 of a frame or of a rect, a pixel count, or a recording whose
checkpoints already hash the framebuffer. A store holding real journal
text is external and pinned by digest, exactly like the game disk.

### Three assertion forms, none of which needs a golden image

1. **On/off confinement.** Run the same key script twice, once with the
   seam off and once on, dump a still at the same frames, and diff. The
   diff must be empty everywhere except the rect the seam owns at that
   moment: the panel while a page is up, the six bar cells while `Notes`
   is spliced, the whole screen while the log is up. After the seam
   gives the screen back the diff must be empty everywhere. This is
   M5-E2d's property as a number, and it catches the exact class of bug
   #230 fixed: the program painting its bar over the journal.

   Measured on 2026-09-01 (§9): over Leg 9's script the on-run differs
   from the off-run in exactly two rects at every frame after the entry
   opens — the panel, and `NOTES` on the bar — and in none before F1.

   **Confinement is a masking question, not a bounding-box one** (#233).
   Those two rects are far apart — the panel high on the screen, `NOTES`
   on the bottom bar — so the difference's bounding box spans almost the
   whole frame and lies inside neither. A check that asked whether the
   box fitted one allowed rect would call the correct behaviour a
   failure, and nothing would ever pass. `scripts/frames.py diff --allow`
   therefore blanks every allowed rect in the difference and asserts what
   survives is empty.

   **A leg cannot both swallow keys and diff against an off run** (#233).
   The seam-off run has no journal to take a keystroke, so every key the
   seam would have swallowed reaches the program instead and the two runs
   end in different game states — a `SEARCH` on the status line of one
   and not the other, and a whole-frame diff that means nothing. So there
   are two kinds of leg: an **on/off pair**, whose script may press only
   keys the seam lets through, and a **single run**, which asserts its
   own frames against each other. NOT-8 is the second kind and always
   was: "nothing reaches the program" is one digest holding still, not a
   difference from anything.

2. **Cross-host equality.** The wasm module driven by `drive.mjs` over
   the same script and store writes a final frame that must `cmp` equal
   to the desktop's. Leg 9 already does this by hand; the plan makes it
   a line in a script.

3. **Checkpoint hashes.** A session's state hash covers the framebuffer
   at every checkpoint. Once a session can name its store (§5), a
   recording of the reader is a pixel-exact regression test on all four
   targets, for free.

Where a real picture has to be judged, a person does it once, at
recording time, off a contact sheet of the changed frames. The tool for
that is §5's first item.

### The rects a diff is allowed to touch

| Rect | Pixels | Cells | Note |
| --- | --- | --- | --- |
| Reader panel | x 136..311, y 8..119 (176 × 112) | cols 0x11..0x26, rows 1..14 | `automap_panel_*` in `automap.h`; the party roster's own cells |
| `Notes` splice, 3D bar | x 273..311, y 192..199 | six cells at the bar's end | measured by diff, §9; the area-mode bar's word sits further right and needs its own measurement |
| Log screen | whole frame | | mask the timestamp column when the wall seed differs between runs |
| Give-back | ∅ | | after F1 on the last page, Escape, or `E`, the diff is empty |

Colours to expect: the caption row is EGA index 14 (the program's
highlight yellow) and the body rows index 2 (its message green).

### Structural checks a rect can carry

Beyond "the diff is confined", a rect's pixels answer questions without
a golden: the caption row's non-black pixels are all index 14 and the
body rows' all index 2; the panel is non-blank when a page should be up;
two stores that differ only in curly versus straight quotes produce
byte-identical panels (RDR-11). These are cheap, committable and
independent of the font.

### What a test-only readback would add

Optional. The seam knows which byte it placed at which cell, so a
test-apparatus export of the panel's cell contents as text — on the SDL
host under a flag and in the module as an `af_web_` call — would let a
script assert *words* without an image. The precedent is
`--journal-probe`. Worth building only if the hash-based checks turn out
too blunt to say what broke.

## 4. The test matrix

Tiers: **A** runs in CI with no game. **B** runs in the sweep on a
machine with the disk, scripted and repeatable. **C** is a person with a
display. Status is *exists* or *new*.

### Ingestion

| ID | Case | How | Visual check | Tier | Status |
| --- | --- | --- | --- | --- | --- |
| ING-1 | Probe PDF end to end, both hosts | As today | None; the store's words are the check | A | exists |
| ING-2 | The archive edition through the linked engine | `build/ocr-linked` host with `--journal <pdf> --journal-store SCR/real.txt`; assert `entries=99 extracted=99 recognized=99` and record the fingerprint | Open the store in an editor and confirm 94 or more entries begin with their own printed heading. Never commit it | C | exists |
| ING-3 | The same through an installed Tesseract as a program | Install Tesseract, run the plain host with `--journal`; compare counts and the store's fingerprint against ING-2 | The two stores should agree entry for entry apart from engine version drift; diff them | C | new |
| ING-4 | `--journal-ocr none` and a missing engine | Assert the host says so in words and the store has 99 entries with empty scans | None | B | new |
| ING-5 | A correction survives re-ingesting the real edition | Edit one entry's correction in ING-2's store, re-run, assert `corrections=1` and the text | None | C | new |
| ING-6 | Default store path | Run with no `--journal-store`; assert the printed path is under the per-user data directory | None | B | new |
| ING-7 | The archive edition through tesseract.js in a browser | See §7. Time it; assert the same counts as ING-2 | The progress line advances per entry; the finished drawer shows the counts | C | new |

### Store and persistence

| ID | Case | How | Visual check | Tier | Status |
| --- | --- | --- | --- | --- | --- |
| STO-1 | Round trips, versions, limits, the log | As today | None | A | exists |
| STO-2 | A desktop store read by the web host | `drive.mjs --journal-store` over ING-2's store; Leg 9 script; assert `journal-open calls=1` | Final frame `cmp` equal to the desktop's | B | exists |
| STO-3 | The read log is written by the desktop and shown next run | Run 1: cite or open entry 4. Run 2: `N` at once. Assert the log has the row and the star is off | Log screen still, second run, hash it | B | new |
| STO-4 | The read log on the web does not survive a reload | Decide: pin the current behaviour in `smoke.mjs` with a comment naming it, or file the fix. Either way a test says which | None | A | new |
| STO-5 | Drawer full, drawer blocked, drawer holds a later format | Extend `smoke.mjs`'s fake drawer to throw on `setItem` and on access; assert the three sentences | None | A | new |

### Citation watch

| ID | Case | How | Visual check | Tier | Status |
| --- | --- | --- | --- | --- | --- |
| CIT-1 | Recognizer and window over test strings | As today | None | A | exists |
| CIT-2 | **A real citation opens a real entry** | Done (#232, PR #241). The square is `3,4` facing east outside the city hall, reached by Leg 1's tour and then east; one press of Up and a Return runs the entrance event, whose last page cites four proclamations. Drive with `--seam journal`, ING-2's store and `--dump-every 25`; assert `journal-open calls=1 last=131136` — proclamation 64, the first of the four | Contact sheet around the callout: the message panel with the citation, then the entry over the roster with nobody having pressed anything. Hash the panel rect | B | **driven** |
| CIT-3 | A citation in more than one piece | Done (#232). It arrives as **two message-box calls** — a sentence, then the numerals appended with the box's clear flag down — and not as a line wrap: the box word-wraps a whole operand itself, so what splits a citation is the script printing its number as the next operand. A leg asserts the pair, and that the first alone opens nothing | Same as CIT-2 | B | **driven** |
| CIT-4 | No false positives across the library | **Not runnable as written**: the host refuses `--seam` beside `--replay`, because a recording owns its seams (#232). It needs #235 first — a `journal-store` line in the descriptor and sessions recorded with the seam on — and then the assertion is `calls=0` on the ten that do not cite. What stands in the meantime is #232's own drive: one open across boot, party creation, credits, menus and twelve story messages | None | B | blocked on #235 |
| CIT-5 | A citation while a story page is up keeps Space and Return the program's | **Not drivable at CIT-2's square** (#232): the citation is on the event's *last* page, so there is no story page left to turn. What that square can assert is the weaker half — Space reaches the program and the panel stays up. The stronger claim wants a square where the game cites mid-event, and none has been found yet | Two stills: the page turned, the entry still over the roster | B | needs a square |
| CIT-6 | A citation with an empty store | Done (#232), and the expectation here was wrong: the service is still called once and the seam draws **nothing at all**, because #175's rule is that a citation never takes the screen for a refusal. The refusal panel is the F1 path only. The leg asserts `calls=1` and a roster the seam did not touch | Panel rect: unchanged — the roster, with nothing drawn over it | B | **driven** |

### Reader panel

All scripts start from the Leg 9 prefix: code wheel `A@7601`
`Return@7651`, load `L@8951` `A@9201`, party on the street from about
frame 10,000. Frames are 60 per virtual second; leave 100 frames between
presses.

| ID | Case | How | Visual check | Tier | Status |
| --- | --- | --- | --- | --- | --- |
| RDR-1 | Unit suite | As today | Pixel buffer against a test font | A | exists |
| RDR-2 | F1 opens the prompt | Still at F1+50 | On/off diff confined to the panel; `JOURNAL` caption row is index 14; footer row present; the cursor rule drawn in the seam's own pixels, no stray glyph | B | **seen** (#233) |
| RDR-3 | Digits, Backspace, Return open an entry | `4`, `3`, Backspace, Return; assert `last=4` | Panel: `ENTRY 4` in 14, body in 2, no row wider than 22 cells, no half glyph at the right edge | B | **seen** (#233) |
| RDR-4 | F1 cycles the section at the prompt | F1 four times; stills after each | Caption reads ENTRY, TALE, PROCLAMATION, ENTRY; the fourth still equals the first | B | **seen** (#233) |
| RDR-5 | Paging a long entry | Open the longest entry in ING-2's store; F1 per page; assert the page count matches the footer's `n/m` | Each page still differs from the last; the last F1 gives the roster back (diff ∅) | B | **seen** (#233) |
| RDR-6 | Escape from the prompt and from a page; Backspace back a page | Three short scripts | Diff ∅ after each Escape; the Backspace still equals the earlier page's | B | **seen** (#233): after Escape the whole difference is the six `NOTES` cells |
| RDR-7 | The four refusals | No store; entry 999; a store with an entry whose scan is empty; an entry over 4 KiB | Each refusal's two lines in the panel; the truncated entry's last page says so | B | new |
| RDR-8 | Give-back is exact | Close by every route | Whole-frame diff against the off run is ∅ apart from `Notes`; the 3D viewport never differed at any frame | B | **seen** (#233) for Escape and for the last F1 |
| RDR-9 | Modal over the automap | `--seam automap` too: Tab, F1, 3, Return, F1 | Map, entry over it, map back with the label and the party's square; the third still equals the first | B | exists by hand |
| RDR-10 | Off a roster screen the key is nobody's | Walk into a shop (Leg 4's route), press F1, assert `calls=0`; leave the shop with an entry cited on the way and assert it comes up when the roster returns | Shop still unchanged by F1; the entry over the roster on the street | B | new |
| RDR-11 | Transliteration on the glass | Two hand-written stores: one with curly quotes, an em dash, an ellipsis and a CJK character, one with their plain forms and `?`. Open each | The two panel rects are byte-identical | B | new |
| RDR-12 | Every reader script on the wasm module | `drive.mjs` with `--journal-store`, same script | `cmp` of final frames per script | B | new |
| RDR-13 | A real entry, read by a person | ING-2's store, entry 1, windowed | The text is legible in the game's font; wrapping breaks at words; the quote marks are plain. Never screenshot it into the tree | C | new |

### Notes and the log

| ID | Case | How | Visual check | Tier | Status |
| --- | --- | --- | --- | --- | --- |
| NOT-1 | Unit suite | As today | None | A | exists |
| NOT-2 | `Notes` on both bars | Slot A (3D mode), then `A` for area mode; stills of each | On/off diff confined to six cells on the bar row in both modes; the word is the program's lettering, big initial and small tail | B | new |
| NOT-3 | Not on a vendor's bar | In a shop, still of the bar | Diff ∅ against the off run | B | new |
| NOT-4 | The empty log | Fresh store, `N` | Frame, title, the one sentence, `EXIT`; arrives as one frame (`--dump-every 1` around the press shows no partial screen) | B | **seen** (#233) |
| NOT-5 | A filled log | After CIT-2: `N`. **Not** after opening entries by F1 — an entry the player asked for was never cited, so nothing goes on the log (#233) | The four proclamations in the order the game said them, `*` on the three unread, the cursor row in 14 and the rest in 2, timestamps from the seeded clock | B | **seen** (#233) |
| NOT-6 | Cursor and scrolling | Down past the end, Up past the start, a log longer than the window | Stills: cursor stops at the ends; the window scrolls to keep it | B | new |
| NOT-7 | Return opens the line; the star comes off | `N`, Return, then back to the log | The entry over the roster; the log's row without its `*` | B | **seen, and it was broken** (#233): the entry never appeared, because the give-back's batch had not run and the panel was painted over. Fixed with it |
| NOT-8 | **Nothing reaches the program while the log is up** (#230) | `N`, then `S`, `C`, `L`, Left, Right, Up. A **single-run** leg, not an on/off pair: the off run has no log and every one of those keys reaches the program (§3) | Every still after `N` and before `E` is identical. Measured (#233): one digest across 1,475 frames | B | **seen** (#233) |
| NOT-9 | Give-back in every mode | `E` and Escape, from 3D mode, area mode, and the alternate adventuring screen | Whole-frame diff ∅ against the off run apart from `Notes`, including the outer frame and the viewport's ornaments | B | new |
| NOT-10 | F1 from the log goes to the prompt | `N`, F1 | The log gone, the prompt in the panel | B | new |
| NOT-11 | The log on the wasm module | NOT-4, NOT-5 and NOT-9 via `drive.mjs` | `cmp` equal, timestamp column included when the wall seed is shared | B | new |

### Fidelity and sessions

| ID | Case | How | Visual check | Tier | Status |
| --- | --- | --- | --- | --- | --- |
| FID-1 | On, nothing cited, no key: identical | Record `walk.rec`'s script with `--seam journal` as `walk-journal.rec`; every checkpoint hash equals `walk.rec`'s | The framebuffer is inside the hash | B | new |
| FID-2 | The reader as a session | `reader.rec` over a committed hand-written store: Leg 9's keys plus RDR-4's cycle | Checkpoints pin the panel on all four targets | B | new |
| FID-3 | The log as a session | `notes.rec`: NOT-4, NOT-8's key barrage, NOT-9 | Same | B | new |
| FID-4 | The citation as a session | `cite.rec` over an external store pinned by digest | Same; skips loudly without the store | B | new |

## 5. Harness work the matrix needs

Four pieces, in the order they unblock the most rows. None adds
mechanism to the seam or the core.

1. **`scripts/frames.py`** — **done** (#233). Six subcommands: `png`,
   `crop`, `hash`, `diff` (with `--allow`, the confinement check),
   `changed`, and `sheet`. `scripts/test-frames.sh` is its self-test over
   stills it draws itself, and runs in CI's guards job beside the other
   three. Rects are inclusive on both sides, which is how §3's table
   writes them and is *not* PIL's convention; the tool's own docstring
   says so and the self-test pins it.
2. **A store line in a session descriptor.** `journal-store
   tests/sessions/reader-store.txt` for a committed hand-written store,
   or `journal-store external` with a `sha256`, mirroring how `disk
   external` pins a disk. `sweep.py` passes it as `--journal-store` to
   the host and to `drive.mjs`, and skips loudly when an external one is
   absent. This is the loose end #175 named, and it turns FID-2 to FID-4
   from hand runs into goldens. Check first that `--replay` honours
   `--journal-store`; the store is read at the start of every run, so it
   should.
3. **`scripts/visual-legs.py`.** The on/off confinement runner: takes a
   leg's key script, runs it off and on, dumps at the named frames, and
   asserts each diff's bounding box lies inside the rects the leg allows
   at that frame. Reads a small table of legs from `tests/visual/*.leg`.
   Runs under the sweep's disk rules.
4. **A shared wall seed for on/off pairs.** The log's timestamp column
   comes from the seeded wall clock, so two hand runs disagree there.
   Either the confinement runner masks the column, or the host grows a
   test-only way to fix the seed. A replay already pins it, so FID-3 is
   unaffected; only NOT-9's whole-frame diff needs one of the two.

## 6. Order of work, and the issues

Each phase is an issue, and #239 tracks them in this order: #232,
#233, #234, #235, #236, #237 and #238.

1. **Find the citation** (#232) — **done**, and it earned its place at the
   front twice over. The count stayed zero on a square where the game
   visibly cited four proclamations, and the recognizer's shape was only
   the *second* thing wrong with it. The first was the address: the watch
   was on the per-cell string drawer, which on this program draws the
   credits, the menus and the position line and no narration whatever.
   The narration goes to the word-wrapping message box, where the
   script's every PRINT ends. PR #241 moved the watch there and rewrote
   the shape — the section's own word rather than the book's, and the
   notation the booklet numbers that section in. Had any of the sessions
   in step 4 been recorded first they would have pinned a watch that
   could never fire.
2. **The frames tool and the first contact sheets** (#233) — §5 item 1. Produce
   sheets for RDR-2 to RDR-8 and NOT-2 to NOT-10 and look at each once.
   This is the visual pass a person does; everything after it is a hash.
3. **Confinement legs** (#234) — §5 item 3 and the `.leg` files for the reader
   and log rows. NOT-8 and NOT-9 are the regression net for #230 and the
   first legs to write.
4. **Sessions** (#235) — §5 item 2, then record `walk-journal`, `reader`,
   `notes` and `cite`. Verify all four on the wasm module. Add the four
   rows to `tests/sessions/README.md`.
5. **The browser** (#236) — §7 in full, once, written up in `docs/hosts.md` §3
   the way the rest of that section is. Install Tesseract for ING-3 in
   the same sitting.
6. **Unit-level gaps and documents** (#237, #238) — STO-4, STO-5, then §8. Add a Leg
   10 to `docs/playable.md` for Notes and the log, with the confinement
   numbers rather than adjectives.

## 7. The browser, by hand

Nobody has opened the dev page on the journal drawer. One sitting, one
browser first, then a second. Serve with `scripts/serve-web.py` after
`fetch-ocr-engine.py` has put the engine beside the page. Record what
was seen in the same words as `docs/hosts.md` §3. This is the one part
of the plan a Claude Code session cannot run: it has no display.

- [ ] The page loads with no console error and no request leaves the
      origin: the network panel shows tesseract.js, its worker and its
      language data from this host, and nothing from a CDN.
- [ ] Drop the archive edition's PDF on the input. The status line names
      the edition, then counts entries as they are read. Note the wall
      time.
- [ ] At the end the counts match ING-2. Reload the page: the status
      says the store was restored from this browser and the counts are
      the same, without a second ingestion.
- [ ] Press *Forget it*. The status says so; reload; the store is gone.
- [ ] Open DevTools, fill `localStorage` near its quota, ingest again:
      the page says the browser would not keep it, and the run still
      works for this tab.
- [ ] A private window, and a browser set to block site data: the page
      loads, ingests, and says it kept nothing.
- [ ] Drop a game directory, tick the `journal` seam, load slot A. F1
      opens the prompt in the canvas and does not open the browser's
      help; Tab with the automap on does not move focus; Backspace at
      the prompt does not navigate back; Escape does not leave full
      screen if the canvas is in it.
- [ ] Open entry 1. Compare by eye with the desktop windowed at the
      same point: same wrapping, same colours.
- [ ] `N` opens the log; `E` gives the screen back with nothing left
      behind.
- [ ] Everything above again in the second browser. Note which two were
      used and their versions.

## 8. Documents to correct

Read on 2026-09-01, these sentences contradict what the tree does. They
matter for a test plan because a reader who trusts them will not run
ING-2 or CIT-2.

- `docs/journal.md` §9, twice: "the table is empty (§3)". §7 of the same
  file says the archive edition was ingested at 99 of 99.
- `docs/seams.md` §10, the reader's "What it has not done": "the edition
  table is empty". Its own "Ungated" paragraph three sections up says
  there is a row now.
- `docs/playable.md` Leg 9: "Nobody has an ingested edition". The Notes
  paragraph in `seams.md` describes entry four opening "out of a real
  ingested journal".
- `hosts/sdl/src/main.cpp`'s comment on `--journal`: "`known_journals()`
  is empty today".
- `journal_ingest_test.cpp`: the case named
  `TheShippedTableRecognizesNothingYet`. Check the body; rename if it
  now asserts the opposite.
- `docs/playable.md` has no leg for `Notes` and the log, though #221,
  #222 and #230 were all driven. Leg 10 in phase 6.

**Three of these were corrected by #232's own PR (#241)**, because that
change made them wrong in a second way and leaving them would have been
worse than the first: `docs/journal.md` §7 and §9, `docs/seams.md` §10's
"What it has not done", and `docs/playable.md` Leg 9 — which had claimed
the citation path was proven by building the pattern wrong on purpose so
that the position line would match it. Phase 6 still owns the rest, and
the reason that leg was wrong is worth carrying into it: a probe that
reaches a routine says nothing about whether that routine sees the thing
you are watching for.

## 9. Running it from a session on this machine

Everything in tier B was checked to run from a Claude Code session on
the maintainer's Windows machine on 2026-09-01, with no display. The
traps are the ones `docs/playable.md` and the session notes already
name; this is the shortest path through them.

**The disk.** `games/por` carries two things the session manifests do
not (`SAVE_old`, a PDF), so copy it and drop them; never touch the
original.

```sh
SCR=<the scratchpad directory>
cp -r games/por "$SCR/por"
rm -rf "$SCR/por/SAVE_old" "$SCR/por/__ CODE WHEEL __.PDF"
```

**A store to drive with.** Version 3, one entry, this project's own
words; the lengths are byte counts of UTF-8.

```
amberfolio-journal 3
edition 0000000000000000000000000000000000000000000000000000000000000000
engine hand
scanned entry 3 <bytes>
<the text>
corrected entry 3 0

```

**The run.** The dummy drivers are what let `--press` work with no
window; `--headless` refuses it. The Release host is
`build/windows-msvc/hosts/sdl/Release/amberfolio.exe`; for a real
ingestion use `build/ocr-linked/hosts/sdl/Debug/amberfolio.exe`.

```sh
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
KEYS="--press A@7601 --press Return@7651 --press L@8951 --press A@9201
      --press F1@10600 --press 3@10700 --press Return@10800"
$HOST "$SCR/por" START.EXE --seam code-wheel --seam journal \
  --journal-store "$SCR/store.txt" --fast max --until 240000000 $KEYS \
  --dump "$SCR/on" --dump-every 100
$HOST "$SCR/por" START.EXE --seam code-wheel \
  --fast max --until 240000000 $KEYS --dump "$SCR/off" --dump-every 100
```

About twenty seconds of wall time each, 121 stills each, and the on-run
reports:

```
amberfolio: journal store .../store.txt entries=2 corrections=0 seen=0
amberfolio: seam journal armed fired=602575
amberfolio: host-service journal-open calls=1 last=3 at=214790468
amberfolio: stop reason=tick_budget steps=60000000 ticks=240000000 frames=12069
```

The exit code is 1, which is the tick budget and not a failure; a
`stop` line with any other reason is.

**The diff.** `scripts/frames.py` is the tool now (#233); Pillow is its
one dependency and `.pillow-version` pins it.

```sh
python3 scripts/frames.py diff "$SCR/off/f-011000.ppm" "$SCR/on/f-011000.ppm" \
  --allow 136,8,311,119 --allow 273,192,311,199
python3 scripts/frames.py sheet "$SCR/on" --against "$SCR/off" --out "$SCR/s.png"
python3 scripts/frames.py changed "$SCR/on"        # the stills that differ
```

`diff` prints the pixel count and the bounding box, and with `--allow`
answers the confinement question by masking: exit 0 when nothing differs
outside the allowed rects, 2 when something does. `sheet` collapses the
consecutive stills that show the same thing into one tile, so a run of a
thousand becomes a dozen pictures a person can actually read.

What that measured on 2026-09-01, for every still from 10,700 to 12,000:
a bounding box of `136,8,311,198` — the panel's x range exactly, and a y
range that reaches the bar because the bar row carries `NOTES`. Split by
rect, the difference outside the panel is x 273..311 by y 192..199 and
nothing else; before F1 there is no difference at all. (That box is
written half-open — `(136, 8, 312, 199)` — by PIL and by the twenty-line
seed this section used to carry; `frames.py` prints it inclusive, which
is the form §3's table of rects uses.)

**The wasm side.** `build/wasm/hosts/web/Debug/drive.mjs` takes the same
flags; note the wasm preset builds Debug only unless told otherwise, and
the import needs a `file:///C:/...` URL.

## 10. Facts the scripts rely on

| Fact | Value | Source |
| --- | --- | --- |
| Frame rate for `--press` | 60 per virtual second; `--until` in PIT ticks at 1,193,182 per second | `docs/playable.md` |
| Boot to the street, slot A | `--seam code-wheel --fast max --until 240000000 --press A@7601 --press Return@7651 --press L@8951 --press A@9201`; ends at frame 12,069 | Leg 9 |
| Leg 9 reader keys | `F1@10600 3@10700 Return@10800 F1@11200` | Leg 9 |
| Notes keys as driven | `N`, `4`, `Return` at the same spacing; `E` leaves the log | `docs/seams.md` §10 |
| Dummy drivers | `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`, and no `--headless`, or `--press` is refused | learnt twice |
| The document | the archive edition's PDF, in `games/por-journal`, never committed | this machine |
| Panel rect | x 136..311, y 8..119; cells 0x11..0x26 by rows 1..14; 22 columns by 14 rows, 12 of body | `automap.h` |
| `Notes` rect, 3D bar | x 273..311, y 192..199 | §9, measured |
| Strings the seam draws | `ENTRY`, `TALE`, `PROCLAMATION`, `JOURNAL`, `RETURN OPENS IT`, `ESC CLOSES`, `F1 CLOSES`, `NO JOURNAL / HAS BEEN READ`, `NO SUCH ENTRY / IN THIS JOURNAL`, `NOTHING WAS READ / FROM THAT ENTRY`, `ADVENTURER'S JOURNAL`, `THE GAME HAS NOT SENT YOU HERE YET.`, `EXIT` | `seam_journal.cpp` |
| Delivery cap | 4 KiB per entry; longer is truncated and the reader says so | `docs/journal.md` §9 |
| Web key handling | Recognised keys are `preventDefault`ed, F1 included; unrecognised ones are left to the browser | `app.mjs` |
| Session hashes | A checkpoint's state hash includes the framebuffer | `tests/sessions/README.md` |
| Report rule | An ingestion is reported as counts, fingerprints and the engine version; never text, an excerpt or a screenshot | `docs/journal.md` §8 |
