# The journal, tested

*How the journal is tested, from a player's PDF to the entry on the
game's screen. M5-E3 (#174) and M5-E4 (#175); PLAN.md §5 item 2. Tracked
on #239; what the matrix still owes is #270.*

`docs/journal.md` is what the journal is, `docs/seams.md` §10 what the
reader is, `docs/playable.md` Legs 9 and 10 the driven legs.

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

Two halves, five surfaces: the host half ingests a player's PDF into a
text store, the seam half reads it back onto the game's screen.

| Surface | Where it lives |
| --- | --- |
| **Ingestion**: locate, inflate or carry, crop, OCR, keep | `hosts/common/src/journal_*.cpp`; `--journal`; the page's file input |
| **Store**: scanned text, corrections, read log, pictures | `journal_store.cpp`, `page/journal.mjs` |
| **Citation watch**: "section word plus a number" at the message box | `seam_journal.cpp`, `journal_citation_in()` |
| **Reader**: opening a row, paging, close | `seam_journal.cpp`, `machine/journal.h` |
| **Notes and the log**: `Notes` on the party's bar, `N` opens the log | `seam_journal.cpp`, the overlay 14 points |

Three properties cut across all five: the **fidelity invariant**,
**cross-host equality** and **persistence**. Out of scope: Tesseract's
accuracy, fact tables for editions nobody holds, and the automap beyond
the pixels it shares with the reader.

## 2. Where coverage stands today

§4 carries the status per row. Everything either side of a real engine
runs in CI on all four targets; what draws is held by the confinement
legs in `tests/visual/`; a real engine, document and display are a
person's, reported per `docs/journal.md` §8. What CI runs, by surface:

| Surface | In CI |
| --- | --- |
| Ingestion | `journal_*_test.cpp`, `run-journal.cmake`, `smoke.mjs` |
| Store | `journal_store_test.cpp` |
| Citation watch | `JournalCitation.*`, `JournalWindow.*` |
| Reader | `seam_journal_test.cpp`, 157 cases |
| Notes and log | `JournalNotes.*`, `JournalList.*` |
| Sessions | the recordings, and the `contrast` sweep |

## 3. The visual method

Three constraints: no screenshot of the game may be committed; no test in
the repository runs the game, so anything needing the disk runs on a
machine that has one; the game draws the same pixels for the same keys at
the same ticks, so a hash is as good as a picture once a person has
looked once.

**The content rule.** A PPM or PNG of any frame goes in the scratchpad
and nowhere else. Committable: a SHA-256 of a frame or a rect, a pixel
count, or a recording whose checkpoints hash the framebuffer. A store
holding real journal text is external and pinned by digest, like the
game disk.

**Three assertion forms, none needing a golden image:**

1. **On/off confinement.** One key script run with the seam off and on,
   stills dumped at the same frames, diffed: empty outside the rect the
   seam owns at that moment, and empty after the give-back.
2. **Cross-host equality.** `drive.mjs` over the same script and store
   writes a final frame that must `cmp` equal to the desktop's.
3. **Checkpoint hashes.** A session's state hash covers the framebuffer,
   so a recording over a named store (§5) is a pixel-exact regression
   test on all four targets.

A picture that has to be judged is judged once by a person, at recording
time, off a contact sheet (`frames.py sheet`).

**Rules a leg has to obey** (#233, #234):

- Confinement is masking, not a bounding box: what a seam draws and the
  `Notes` cells can be far apart, so the difference's bounding box fits
  neither.
  `scripts/frames.py diff --allow` blanks every allowed rect and asserts
  what survives is empty.
- A screen can appear in the two runs a dump apart, because a claimed key
  shifts the program's next repaint by less than the dump interval.
  `slack 1` passes a frame confined at its own number or one dump either
  side.
- A leg cannot both swallow keys and diff against an off run: the off run
  has no journal, so every swallowed key reaches the program and the
  states diverge. A `kind pair` may press only keys the seam lets
  through; a `kind single` asserts its own frames against each other.
  The log's give-back is a single run too, because the key that opens the
  log steps the off run's bar highlight; it is stated as `equal`.
- Text pages take about 230 frames to paint after the key; a picture page
  about 75 (#305, #328).

**The rects a diff is allowed to touch:**

| Rect | Pixels | Cells | Note |
| --- | --- | --- | --- |
| `Notes` splice, 3D bar | x 273..311, y 192..199 | six cells at the bar's end | measured by diff, §9; the area-mode bar's word sits further right and is unmeasured |
| Log screen | whole frame | | |
| Give-back | ∅ | | after the way out the diff is empty; since #330 the bar row included |

Colours: the caption row is EGA index 14 (highlight yellow), the body
rows index 2 (message green). Structural checks a rect can carry without
a golden: caption-row non-black pixels all 14 and body-row all 2; two
stores differing only in curly versus straight quotes give byte-identical
screens (RDR-11). Since #346 everything this seam draws takes the whole
frame, so a rect narrower than one confines nothing.

**A test-only readback** of the screen's cells as text (an SDL flag and
an `af_web_` call, on `--journal-probe`'s precedent) is unbuilt; worth it
only if the hash checks are too blunt.

## 4. The test matrix

Tiers: **A** runs in CI with no game. **B** runs on a machine with the
disk, scripted. **C** is a person with a display.

### Ingestion

| ID | Case | Tier | Status |
| --- | --- | --- | --- |
| ING-1 | Probe PDF end to end, both hosts | A | exists |
| ING-2 | Archive edition through the linked engine, `build/ocr-linked` with `--journal <pdf> --journal-store SCR/real.txt`: `entries=99 extracted=99 recognized=99`, fingerprint recorded, 94+ entries beginning with their printed heading | C | exists |
| ING-3 | The same through an installed Tesseract; diff against ING-2 | C | new |
| ING-4 | `--journal-ocr none`, and a missing engine: said in words, 99 empty scans | B | new |
| ING-5 | A correction survives re-ingestion: `corrections=1` and the text | C | new |
| ING-6 | Default store path is the per-user data directory | B | new |
| ING-7 | Archive edition through tesseract.js in a browser (§7) | C | done once (#306) |

### Store and persistence

| ID | Case | Tier | Status |
| --- | --- | --- | --- |
| STO-1 | Round trips, versions, limits, the log | A | exists |
| STO-2 | Desktop store read by the web host: `drive.mjs --journal-store`, Leg 9's script, `journal-open calls=1`, final frame `cmp` equal | B | exists |
| STO-3 | Read log written by the desktop, shown next run: row there, star off | B | new |
| STO-4 | Read log on the web across a reload | A | `restore_journal_log()` (#237) in `smoke.mjs` |
| STO-5 | Drawer full, blocked, or a later format: the fake drawer throws on `setItem` and on access | A | new |

### Citation watch

| ID | Case | Tier | Status |
| --- | --- | --- | --- |
| CIT-1 | Recognizer and window over test strings | A | exists |
| CIT-2 | A real citation logs a real entry: square `3,4` facing east at the city hall, Up and Return; `--seam journal`, ING-2's store, `--dump-every 25`. Since #346 it draws nothing and asks no host: `journal-open calls=0`, four rows on the `Notes` log, each unread | B | driven for the old behaviour (#232, PR #241); owed a re-drive for #346 |
| CIT-3 | A citation split over two message-box calls; the first logs nothing | B | driven (#232) |
| CIT-4 | No false positives: `calls=0` on sessions that do not cite | B | blocked: the host refuses `--seam` beside `--replay` (#235) |
| CIT-5 | A citation leaves every key the program's, because it draws nothing (#346) | B | subsumed: nothing is claimed at a citation |
| CIT-6 | A citation with an empty store: nothing drawn and no host asked; refusals are the reader's own path only (#175) | B | driven (#232) |

### Reader

All scripts start from the Leg 9 prefix (§10): the party is on the street
from about frame 10,000. Leave 100 frames between presses.

| ID | Case | Tier | Status |
| --- | --- | --- | --- |
| RDR-1 | Unit suite, pixel buffer against a test font | A | exists |
| RDR-2 | Return on a listing row opens it; `ENTRY 3` in 14, body in 2, no row over 38 cells | B | `tests/visual/rdr-page.leg`; owed a drive (#293) |
| RDR-5 | Paging a long entry; the last key gives the screen back | B | `tests/visual/rdr-page.leg`, single since #305; owed a drive (#293) |
| RDR-6 | Escape from a page and from the listing; Backspace back a page | B | `rdr-page.leg` (#234, #305) |
| RDR-7 | The four refusals: no store, entry 999, empty scan, over 4 KiB | B | new |
| RDR-8 | Give-back exact: the frame after the listing closes equals the frame before `Notes` | B | `rdr-page.leg` as an `equal` (#234, #305) |
| RDR-9 | Modal over the automap, map back after: `tests/visual/rdr-map-back.leg`; `--seam automap`, Tab, `Notes`, `E`, Tab twice | B | a leg (#332); owed a drive (#293) |
| RDR-10 | With the reader down no key is this seam's at all (#346): every keystroke reaches the program, on every screen | A | `JournalKeys.WithTheReaderDownEveryKeyIsTheProgramsOwn` |
| RDR-11 | Transliteration: curly and plain stores, identical screens | B | new |
| RDR-12 | Every reader script on the wasm module, `cmp` of final frames | B | done for three sessions (#177) |
| RDR-13 | A real entry read by a person, windowed; never screenshot it | C | new |
| RDR-14 | An entry that is a picture: `tests/visual/rdr-art.leg`; prompt, caption, `NEXT`, `EXIT`, over `reader-art-store.txt` | B | a leg (#328); owed a drive (#293) |
| RDR-15 | A real picture on a display, whole | C | dumped stills at 2x only (#328; `docs/journal.md` §11.6) |

### Notes and the log

| ID | Case | Tier | Status |
| --- | --- | --- | --- |
| NOT-1 | Unit suite | A | exists |
| NOT-2 | `Notes` on both bars: six cells on the bar row | B | `tests/visual/not-bars.leg` |
| NOT-3 | Not on a vendor's bar: diff ∅ in a shop | B | new |
| NOT-4 | The empty log: frame, title, one sentence, `EXIT`, in one frame (`--dump-every 1` around the press shows no partial screen) | B | `tests/visual/not-log-giveback.leg` (#234) |
| NOT-5 | A filled log after CIT-2: four in order, `*` on every one of them because a citation reads nothing (#346), cursor row in 14 | B | a leg (#234) |
| NOT-6 | Cursor at the ends; a log longer than a screenful pages | B | new |
| NOT-7 | Return opens the line on the whole screen (#305); the star comes off | B | seen; #233's defect removed by #305 |
| NOT-8 | Nothing reaches the program under the log (#230): `tests/visual/not-log-modal.leg`, one digest across 1,475 frames | B | a leg (#234) |
| NOT-9 | Give-back in every mode: `not-log-giveback.leg` and `tests/visual/rdr-bar.leg`, bar row included, whichever key point the way out lands on (#325); area mode and the alternate screen uncovered | B | 3D mode only (#234, #330, #325) |
| NOT-11 | The log on the wasm module, `cmp` equal | B | done for `notes.rec` (#177) |
| NOT-12 | Nothing reaches the program under a full-screen page: `tests/visual/not-page-modal.leg` | B | derived (#305); owed a drive (#293) |
| NOT-13 | A page from the log goes back to the log: `tests/visual/not-page-back.leg` | B | derived (#305); owed a drive (#293) |

### Fidelity and sessions

| ID | Case | Tier | Status |
| --- | --- | --- | --- |
| FID-1 | Seam on, nothing cited, no key: identical run | B | not reachable, not a bug (#235): `Notes` is spliced as the party's bar is drawn, so `cpu` and `ram` differ too. `docs/seams.md` §7's seam-off invariant holds instead; `quiet-journal` is a `contrast`, not an `identical` |
| FID-2 | `reader.rec` over `tests/visual/reader-store.txt`, 156 checkpoints | B | recorded (#235); owed a re-recording for the full screen (#293) |
| FID-3 | `notes.rec`, 146 checkpoints | B | recorded (#235) |
| FID-4 | `cite.rec` over an external store pinned by digest; skips loudly without it. A citation draws nothing since #346, so the run diverges from the seam-off run only where the `Notes` splice does | B | recorded (#235); stale, and re-recorded with the rest of the library under #293 |

## 5. Harness work the matrix needs

All four items are settled; none added mechanism to the seam or the core.

1. **`scripts/frames.py`** (#233): `png`, `crop`, `hash`, `diff` (with
   `--allow`, the confinement check), `changed`, `sheet`.
   `scripts/test-frames.sh` is its self-test, in CI's guards job. Rects
   are inclusive on both sides, which is how §3's table writes them and
   is not PIL's convention.
2. **A store line in a session descriptor** (#235): `journal-store
   tests/visual/reader-store.txt` or `journal-store external <sha256>`,
   mirroring `disk external`. `sweep.py` copies the store before passing
   it to the host, because a run writes its log back into it, and skips
   loudly when an external one is absent. Trap: `--replay` used to accept
   `--journal-store` and never read it, because a store was loaded only
   when the seam was named on the command line and a replay takes its
   seams from the recording; it loads whenever a store is named now. The
   pin is necessary: `reader.rec` against another store, or none,
   diverges at the first checkpoint after the entry opens, in `devices`.
3. **`scripts/visual-legs.py`** (#234): runs a leg's key script with the
   seam off and on under the dummy drivers and asserts nothing differs
   outside the allowed rects. `tests/visual/*.leg` are the legs;
   `tests/visual/reader-store.txt` is their store, and
   `reader-art-store.txt` a second whose one entry has a picture (#328),
   kept separate because the legs that fill the log with
   `--cite-all-journal` open whatever row the cursor lands on. A leg says
   `kind pair` or `kind single`; a pair uses `allow <range> <rects>`, a
   single uses `same <range>` and `equal <before> <after> [rects to leave
   out]`. It has the sweep's three outcomes, and
   `scripts/test-visual-legs.sh` asserts in CI that a skip is loud. Trap:
   **a leg run rewrites the store it was pointed at**; check the fixture
   out again rather than committing it.
4. **A shared wall seed for on/off pairs**: not needed. Neither host
   seeds the wall clock (nothing calls `setWallClock`), so both count
   from `wall_clock`'s 1980-01-01 default and two runs' log screens are
   byte-equal. If a host ever seeds from the calendar, mask the timestamp
   column rather than add a test-only seed surface.

## 6. Order of work, and the issues

The seven phases of #239 — #232, #233, #234, #235 (verified on the wasm
module at #177), #236, #237 and #238 — are all closed. What they left of
the matrix is #270.

## 7. The browser, by hand

One sitting per browser, two browsers. Serve with `scripts/serve-web.py`
after `fetch-ocr-engine.py` has put the engine beside the page. Record
what was seen in `docs/hosts.md` §3's words. A Claude Code session has no
display and cannot run it.

- [ ] No console error and no request leaving the origin: tesseract.js,
      its worker and its language data from this host, never a CDN.
- [ ] Drop the archive edition's PDF on the input; the status line names
      the edition and counts entries. Note the wall time.
- [ ] The counts match ING-2. Reload: the store is restored, same counts,
      no second ingestion.
- [ ] Press *Forget it*; reload; the store is gone.
- [ ] Fill `localStorage` near its quota in DevTools and ingest again:
      the page says the browser would not keep it and still works.
- [ ] A private window, and a browser blocking site data: the page loads,
      ingests, and says it kept nothing.
- [ ] Drop a game directory, tick the `journal` seam, load slot A. `N`
      opens the log; Tab with the automap on does not move focus;
      Backspace on the log does not navigate back; Escape does not leave
      full screen.
- [ ] Open entry 1 and compare by eye with the desktop windowed at the
      same point: same wrapping, same colours.
- [ ] `N` opens the log; `E` gives the screen back with nothing left
      behind.
- [ ] Everything above in the second browser; note both and their
      versions.

## 8. Documents to correct

The "table is empty" claims this plan found contradicting the tree were
corrected by #241 and #238, which also added `docs/playable.md`'s Leg 10.
Trap: a probe that reaches a routine says nothing about whether that
routine sees what you are watching for.

## 9. Running it from a session on this machine

Tier B runs from a Claude Code session on the maintainer's Windows
machine with no display.

**The disk.** `games/por` carries two things the session manifests do not
(`SAVE_old`, a PDF): copy it and drop them, never touching the original.

```sh
SCR=<the scratchpad directory>
cp -r games/por "$SCR/por"
rm -rf "$SCR/por/SAVE_old" "$SCR/por/__ CODE WHEEL __.PDF"
```

**A store to drive with.** One entry, this project's own words; lengths
are UTF-8 byte counts. The host rewrites the header to the current format
version on exit.

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
      --press N@10600 --press Return@10800"
$HOST "$SCR/por" START.EXE --seam code-wheel --seam journal \
  --journal-store "$SCR/store.txt" --fast max --until 240000000 $KEYS \
  --dump "$SCR/on" --dump-every 100
$HOST "$SCR/por" START.EXE --seam code-wheel \
  --fast max --until 240000000 $KEYS --dump "$SCR/off" --dump-every 100
```

About twenty seconds each, 121 stills each; the on-run reports:

```
amberfolio: journal store .../store.txt entries=2 corrections=0 seen=0
amberfolio: seam journal armed fired=602575
amberfolio: host-service journal-open calls=1 last=3 at=214790468
amberfolio: stop reason=tick_budget steps=60000000 ticks=240000000 frames=12069
```

Exit code 1 is the tick budget, not a failure; a `stop` line with any
other reason is. Since #291 the seam does not answer the code wheel; legs
wanting the shorter boot ask the runner for `code-wheel-answered` (#293).

**The diff.** `scripts/frames.py`; Pillow is its one dependency, pinned
by `.pillow-version`.

```sh
python3 scripts/frames.py diff "$SCR/off/f-011000.ppm" "$SCR/on/f-011000.ppm" \
  --allow 136,8,311,119 --allow 273,192,311,199
python3 scripts/frames.py sheet "$SCR/on" --against "$SCR/off" --out "$SCR/s.png"
python3 scripts/frames.py changed "$SCR/on"        # the stills that differ
```

`diff` prints the pixel count and the inclusive bounding box; with
`--allow` it exits 0 when nothing differs outside the allowed rects and 2
when something does. `sheet` collapses consecutive identical stills into
one tile. Over Leg 9's script the on-run differs from the off-run in the
whole box and the `Notes` cells at every still after the log opens, and
nowhere before it.

**The wasm side.** `build/wasm/hosts/web/Debug/drive.mjs` takes the same
flags; the wasm preset builds Debug only unless told otherwise, and the
import needs a `file:///C:/...` URL.

## 10. Facts the scripts rely on

| Fact | Value | Source |
| --- | --- | --- |
| Frame rate for `--press` | 60 per virtual second; `--until` in PIT ticks at 1,193,182 per second | `docs/playable.md` |
| Boot to the street, slot A | `--seam code-wheel --fast max --until 240000000 --press A@7601 --press Return@7651 --press L@8951 --press A@9201`; ends at frame 12,069 | Leg 9 |
| Leg 9 reader keys | `N@10600 Return@10800 N@11600`; `E` twice leaves the page and then the log | Leg 9 |
| Dummy drivers | `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`, and no `--headless`, or `--press` is refused | learnt twice |
| The document | the archive edition's PDF, in `games/por-journal`, never committed | this machine |
| The box everything is drawn in | cells 1..0x26 by rows 1..0x16; 38 columns by 20 rows of body, from row 3; the bar on row 0x18 | `seam_journal.cpp` |
| `Notes` rect, 3D bar | x 273..311, y 192..199 | §9, measured |
| Strings the seam draws | `ENTRY`, `TALE`, `PROCLAMATION`, `NO JOURNAL / HAS BEEN READ`, `NO SUCH ENTRY / IN THIS JOURNAL`, `NOTHING WAS READ / FROM THAT ENTRY`, `THE PICTURE / IS NOT HERE`, `ADVENTURER'S JOURNAL`, `THE GAME HAS NOT SENT YOU HERE YET.`, `EXIT`, `NEXT`, `PREV` | `seam_journal.cpp` |
| Delivery cap | 4 KiB per entry; longer is truncated and the reader says so | `docs/journal.md` §9 |
| Web key handling | Recognised keys are `preventDefault`ed; unrecognised ones are left to the browser | `app.mjs` |
| Session hashes | A checkpoint's state hash includes the framebuffer | `tests/sessions/README.md` |
| Report rule | An ingestion is reported as counts, fingerprints and the engine version; never text, an excerpt or a screenshot | `docs/journal.md` §8 |
