# The journal

*How a player's own Adventurer's Journal becomes text and pictures this
emulator can show, and what is deliberately not here. M5-E3, issue #174;
PLAN.md §5 item 2.*

This document is the half underneath the reader: locating each entry's
scan inside the player's own PDF, decoding it, reading it once with an
OCR engine, reducing the entries that are drawings (§11), and keeping all
of it on the player's machine. The reader itself is M5-E4 (#175), a seam
(`docs/seams.md` §10); §9 is where the two halves meet.

**Nothing from a journal is in this repository, ever**: no page, image,
word of text, or fixture that resembles one. Facts *about* a document are
fine: a fingerprint, a page number, a byte offset, an image's width. It
is CONTRIBUTING.md's rule for the game binary.

## 1. The pieces

| Piece | Where | What it does |
|---|---|---|
| The document gate | `core/.../machine/document.h` | Fingerprints a document the player holds and gates a seam on it (#171). A gate is over bytes; it never looks inside. |
| The fact table | `hosts/common/.../journal_facts.h` | Per edition, per entry, per piece of it: the page, the stream's offset and length, the image's shape, the rectangle that is that piece (§3). |
| The extractor | `hosts/common/.../journal_extract.h` | Follows an offset, inflates the stream, undoes the predictor, expands samples to gray, crops the region. |
| The reducer | `hosts/common/.../journal_picture.h` | Reduces a printed drawing, once, to four tones and the box the reader draws in (§11). |
| The engine | `hosts/common/.../journal_ocr.h` | One virtual call. The desktop runs the player's own Tesseract; the browser drives tesseract.js. |
| The store | `hosts/common/.../journal_store.h` | Section and number to text, with what the engine read and what a person corrected kept apart. |
| The reader | `core/.../machine/seam_journal.cpp` | The seam that shows an entry in the game (§9). |

The first six are the ingestion and run once. `journal_ingest.h` is the
order they go in. `journal_probe.h` is the synthetic document all of it
is tested against.

## 2. Running it

On the desktop:

```sh
amberfolio <dir> <program.exe> --journal ~/Documents/journal.pdf
amberfolio <dir> <program.exe> --journal J.pdf --journal-store ./journal.txt
amberfolio <dir> <program.exe> --journal J.pdf --journal-ocr /opt/bin/tesseract
amberfolio <dir> <program.exe> --journal J.pdf --journal-ocr none
```

- `--journal` presents the document the way `--document` does, so a
  journal-gated seam arms, then reads inside it.
- The store goes to `journal.txt` under the per-user data directory
  (SDL's `SDL_GetPrefPath`); `--journal-store` says otherwise.
- An ingestion that goes wrong is one printed sentence; the run continues.
- In a browser: the dev page's **read your own journal** file input. Both
  hosts print `journal_trouble_name()`.
- `--seam journal` (or the page's checkbox) turns the reader on; the store
  is read at the start of every such run, so an ingestion is done once:

```sh
amberfolio <dir> <program.exe> --seam journal
```

## 3. Adding an edition

One edition is in the table (#214): the Adventurer's Journal as the
currently sold archive release ships it, fingerprint
`67cbfc0c833b835494310680ad298bc4de1cdcc0168115cc3608c2f6074c737c`. Its
pages are `/DCTDecode` (§4a). It has 58 entries in 78 pieces, and 14
pictures on 12 of those entries (§11).

An edition is data in two places:

1. **The gate**, `core/src/machine/document.cpp`: the SHA-256 of the
   whole file, a name a player would recognize, `document_kind::journal`.
2. **The insides**, `hosts/common/src/journal_facts.cpp`: the same
   fingerprint and one `journal_entry_fact` per item: its section
   (`journal_kind`), the number the game uses for it, and its
   `journal_fragment`s, each with the page, the byte offset of the
   stream's first data byte, its `/Length`, the image's `/Width`,
   `/Height`, `/BitsPerComponent`, component count, filter,
   `/DecodeParms /Predictor`, whether it is `/Decode [1 0]`, and the
   rectangle of that image which is that piece. Pictures are a second
   span, `art` (§11.1).

CI checks the two against each other, and every row's shape: region
inside its image, no two rows for one *(section, number)*, pieces in
reading order, no entry mixing a decoded filter with a carried one, and
a filter this build can carry at all. `--journal` on a document whose
edition is not in the table prints its fingerprint, which is the first
half of the row to write.

**Sections** (#218). Three numbered sections, each from its own base:
journal entries 1–58, tavern tales 1–23, proclamations in Roman numerals
59–214 with gaps. `machine::journal_kind` is the other half of every key:
the fact table's row, the store's record, the citation, the word the
reader draws. It lives in core because the recognizer decides it;
`journal_facts.h` re-exports it. `journal_open` carries one
`std::uint32_t`; a citation packs as `kind << 16 | number`
(`journal_open_argument`). Proclamations are stored as the numeral's
*value*. Store format version 2 added the kind word; a version 1 store is
read as journal entries and written back as the current version.

**An entry is a list of pieces.** Entries are set two columns to a page
and two pages to a scan, and they flow out of a column and, for four of
the 58, onto the facing page. Eighteen are in more than one piece.
`journal_entry_fact` carries a span of `journal_fragment` in reading
order, and what an engine reads is joined in that order.

**Measuring an edition's rectangles.** The tooling stays out of the
repository: it reads a document this project may never carry. The method:

1. **Scan geometry.** Column bands are the blank vertical bands wide
   enough to be gutters; the body's bottom is above the footer rule.
2. **Headings.** Entries open with one long phrase in a display face at
   the column margin: correlate one instance of its bitmap down each
   column. Rejected: gap and line-width rules, because a paragraph's last
   line is short and starts at the margin like a heading. Tales and
   proclamations are set in the body face: cut each column into lines and
   ask an OCR engine what each line opens with. Read a heading's number
   again on its own with a whitelist of the letters it can contain;
   without that `LXXVIII` comes back `LXXVIIT`.
3. **An entry runs from its heading to the next one**, across columns
   and scans.
4. **Check the numbering.** Entries are a counted chain: check it against
   the printed number on every scan; the last must land on 58. Tales and
   proclamations are read: they must ascend in reading order, and every
   rectangle must come back beginning with its own heading. Ascending
   order catches a swash `CIX` read as `CLIX`; settle by eye.
5. **Drop a piece with no ink in it**.
6. **Pictures** are found and measured by §11.1's rule.

## 4. What the extractor decodes, and what it refuses

Two questions, kept apart: whether an entry can reach an engine at all
(`journal_filter_supported()`) and whether this build turns the stream
into samples on the way (`journal_filter_decoded()`).

- **Decoded here**: `/FlateDecode` and unfiltered streams; 1 and 8 bits
  per component; one component (gray or bilevel) and three (RGB, to gray
  by the standard luma weights); PNG predictors 10–15, all five row
  filters; `/Decode [1 0]` and `/ImageMask`. Output is eight bits of gray
  a pixel, cropped to the entry.
- **Carried, not decoded**: `/DCTDecode` (§4a).
- **Refused by name**: `/CCITTFaxDecode`, `/JBIG2Decode`, TIFF's
  predictor 2, and any bit depth or component count not listed. Each
  gets written when a document that needs it is in front of somebody.

## 4a. The pages this build does not decode (M5-E3a, #212)

The archive edition is a 21-page PDF whose every page scan is
`/DCTDecode`, `/DeviceRGB`, 8 bits a component. Rejected: a JPEG decoder
in this tree, because a decoder tested only against its own encoder is
untested. `extract_scan()` (`journal_extract.h`) answers one of two
things:

| | `gray` | `jpeg` |
|---|---|---|
| what the engine gets | samples this build produced | the stream, byte for byte |
| already cropped | yes | no |
| who applies the region | the extractor, before the engine sees it | the engine, to its own output |

For a carried page the engine reads the whole scan and the entry's
rectangle filters the engine's *output*: Tesseract's `tsv` and
tesseract.js's `bbox` both say where each word was, and a word is inside
when its centre is. The filter is in `journal_ocr.h`, not in each host,
so both transcribe one page the same way.

Still checked about a stream nothing looks inside: the offset and length
name bytes of *this* document, and the region is inside the table's
shape. Without the second, a rectangle off the edge reaches the engine as
a filter that matches nothing, which reads like an engine that could not
read the page.

Not a PDF parser, ever: no objects found, no cross-reference table, no
page tree. A wrong row points at bytes that do not inflate, or inflate to
the wrong size, and the extractor says which.

**Traps in the pinned tesseract.js** (#306):

- Version 6 has no flat `data.words[]`. It answers a `blocks` tree only
  when the call asks for `{ blocks: true }` (the worker's default is
  `{ text: true, blocks: false, ... }`); the shape is
  `data.blocks[] → paragraphs[] → lines[] → words[]`, each word
  `{ text, bbox: { x0, y0, x1, y1 }, confidence, ... }`. An answer with
  no `blocks` array is refused with a sentence, never read as blank.
- `loadImage` takes a URL, a data URL, an `IMG`/`VIDEO`/`CANVAS`,
  an `OffscreenCanvas`, a `File` or a `Blob`; an `ImageBitmap` or
  `ImageData` fails with `Error attempting to read image`, so the gray
  path hands it a canvas.
- The UMD bundle exports no version; `scripts/fetch-ocr-engine.py`
  writes `version.txt` beside the engine and the store's engine line is
  read off that.
- A test that hands the filter word boxes in a guessed shape is a test of
  the test; the smoke check uses the pinned engine's own shape.

Entry 1's rectangle, `left 702, top 270, 290x413` of a 1328x1003
two-page scan, comes back beginning `Journal Entry 1:`; the edition reads
in about 320 seconds.

## 5. The OCR engines, and the decisions about them

`.tesseract-version` and `.tesseract-js-version` pin them. Both are
Apache-2.0.

**Desktop.** Two engines, one interface (`journal_ocr.h`), chosen by a
build option (M5-E3c, #216):

- `AMBERFOLIO_LINK_TESSERACT=ON` builds Tesseract, Leptonica and
  libjpeg-turbo and links them (`cmake/AmberfolioTesseract.cmake`). Off
  by default: a long one-time build. The linked API crops to a fragment's
  rectangle directly (`SetRectangle`).
- The default build runs the player's installed Tesseract as a program
  (`hosts/sdl/src/tesseract_ocr.h`): not linked, vendored or fetched. It
  reads the whole page and filters the words afterwards. The version is
  asked of the engine and written into the store; an engine that is not
  installed is reported in as many words. Whether a packaged build ships
  one is M6's question (#265).

**Browser.** tesseract.js, served from the page's own origin and never a
CDN. `scripts/fetch-ocr-engine.py --into <the served directory>` fetches
the pinned library, its wasm core and one language's data into
`vendor/tesseract/`; nothing is committed. `journal.mjs` names every path
the engine may reach for (worker, core, language data) under that
directory. With the engine absent the page says so, names the script, and
the ingestion still runs and reports zero recognized. The wasm CI job runs
the fetch, so the deployed page carries all fifteen files, 32 MB.
`loadEngine()` runs at ingestion and not at page load, so only a visitor
who picks a PDF fetches anything, about seven megabytes.

Digests are pinned in `scripts/ocr-engine.sha256sums`, over the bytes as
upstream served them and not the bytes on disk: the language data is
gzipped locally and a gzip stream is not reproducible across zlib builds.
`--digests` points a run at the record; a mismatch fails the build; when
`.tesseract-js-version` moves, `--force` writes the new record.

**The whitespace a reading answers is an interface** (#331), written down
in `hosts/common/include/amberfolio/host/journal_ocr.h`, because the
reader reflows on it (§9):

| between | what an engine answers |
| --- | --- |
| two words of a printed line | a space |
| two lines of a paragraph | one newline |
| two paragraphs | a blank line |
| two fragments of one entry | one newline, the hosts' own join |

- A fragment boundary is a continuation (§3): one newline, never a blank
  line.
- The program-driven engine derives paragraphs from Tesseract's `tsv`
  columns `block_num`, `par_num` and `line_num`. Trap: each count
  restarts inside its parent, so joining on `line_num` alone runs two
  one-line paragraphs together.
- The browser walks `blocks[].paragraphs[].lines[].words[]`. The linked
  engine's `TessBaseAPI::GetUTF8Text` already ends each line with `"\n"`
  and each paragraph with one more (tesseract 5.5.1's source; not run
  here).
- A new block is a new paragraph, as in `GetUTF8Text`. A break is emitted
  only *between* two kept things, so a clipped paragraph gains none.
- All three depend on Tesseract finding the paragraphs; whether it finds
  this edition's, which open with a quotation mark rather than an indent,
  has not been measured.

## 5a. How well it reads, and how anybody knows (#315)

**The method.** A corrected entry is ground truth for itself, so the
store already holds both halves of a comparison and no truth goes in this
repository. `hosts/common/.../journal_score.h` scores exactly the
corrected entries: character error rate (Levenshtein distance over the
length of the truth) with a word rate beside it; whitespace normalized,
case and punctuation not. Trap: never score an uncorrected entry against
itself, or the number improves every time a player finds a mistake. The
desktop host prints the rate at the end of an ingestion.

**What CI proves** is that the harness counts: `journal_probe_noisy_ocr`
appends three known characters to every correct answer, so the rate is
arithmetic off the probe's own lengths (an append is exactly three edits;
a substitution has no such guarantee).

**The one number.** On the pinned tesseract.js over the tabled edition,
`--psm 6` (one uniform block) read a two-page spread at 12–21% character
error; automatic segmentation drawn 2x reads it at 1–2%. The rule: the
segmentation mode must match the picture. Whole-page paths (the
`/DCTDecode` carry) ask for automatic; the decoded path and the linked
engine's `SetRectangle` hand over one block and keep single block.

Rejected, measured: `user_defined_dpi=300`, `preserve_interword_spaces=1`
and reading the region as grey changed not one character; a
`--user-words` list would fix two misreadings in 341 words for a table to
keep in step; marking doubtful words in the text catches a quarter of the
errors while putting noise in front of a reader, so confidence is a
per-entry score instead.

**The asymmetry.** Drawing the page 2x needs it decoded. The browser does
that (`createImageBitmap` and a canvas, one recognition per scan); the
program-driven desktop engine has no decoder and Tesseract's CLI has no
crop or scale flag, so a default desktop ingestion reads worse than a
browser one, and the linked engine (#216) is the way to close that. The
desktop's installed engine has never been measured.

The commonest surviving errors are the opening single quote that starts
each paragraph, read as a curly double quote or `*`, and hyphenated line
breaks, which §9's reflow joins.

## 6. The store

One file of UTF-8 lines, each text length-prefixed:

```
amberfolio-journal 4
edition <64 hex>
engine tesseract 5.5.1
scanned entry 12 431
<431 bytes><newline>
corrected entry 12 438
<438 bytes><newline>
picture entry 4 0 193 160 10404
<10404 bytes of base64><newline>
seen entry 12 8 29 22 19 0
```

- `scanned <kind> <number> <bytes>` and `corrected <kind> <number>
  <bytes>`, each followed by exactly that many bytes and a newline. The
  kind is the section's lower-case word (§3). A version 1 store had no
  kind word and is read as a store of journal entries, then written back
  as the current version.
- `picture <kind> <number> <nth> <width> <height> <bytes>`, the body
  base64 (§11): which of the entry's pictures, and its size in screen
  pixels.
- `seen <kind> <number> <month> <day> <hour> <minute> <read>`: the read
  log (#222), newest first, no body.
- Two texts per item and only one is ever overwritten: ingestion replaces
  `scanned` and never touches `corrected`; the reader shows the
  correction where there is one.
- Length-prefixed so a transcription line beginning `scanned entry 3 4`
  cannot be read as a header. A file that is not exactly this is refused
  whole, never half-read; CRLF is normalized first.
- A store of a different edition is cleared, not merged.
- Only a store's counts and `journal_store::fingerprint()` may be written
  down anywhere (§8).

**Where it lives** is a host's business (PLAN.md §4). The desktop writes
the file above. The browser writes the same text into `localStorage`, one
slot, and reads it back on the next load (M5-E3f). Rejected: IndexedDB,
because this is tens of kilobytes wanted synchronously as the module
comes up. A full or blocked drawer and a store this build cannot read are
each a sentence on the page; an unreadable store is left where it is. The
page's *Forget it* button empties the drawer and the tab's copy.

**The read log is restored by `host::restore_journal_log()`** in
`hosts/common`, called by both hosts (the web export is
`af_web_journal_seen_restore`, #237). Trap: the store and
`machine::journal_state` both hold the log newest first and `note_seen`
puts each row on the *front*, so rows fed in stored order come out upside
down; `JournalLogRestore` pins the order.

## 7. What is checked, and what is not

**In CI, on every target**, over `journal_probe.h`: a small
byte-deterministic PDF this project generates, whose fact table is what
the generator measured while generating. Three image XObjects: eight-bit
gray with no predictor; one-bit inverted with a different PNG row filter
on every row; a carried `/DCTDecode` page (§4a) that is a flat baseline
JPEG this project encodes. The engine is a fixture that answers for
exactly one image per entry, compared against a bitmap generated from the
same description, so a store with its words in it is evidence the offset,
filter, predictor and crop were right. Three levels:

| Level | Where | What it settles |
| --- | --- | --- |
| C++ suite | `hosts/common/tests/journal_*_test.cpp` | extractor, store, ingestion, scoring, pictures |
| Desktop end to end | `hosts/sdl/cmake/run-journal.cmake` | real files on a real disk; a hand-written correction survives a second ingestion |
| wasm through the ABI | `hosts/web/tests/smoke.mjs` | the inverted loop a browser needs |

Also in CI: the region filter against word boxes in the pinned engine's
own shape (§4a); paragraph breaks in `hosts/sdl/tests/tsv_words_test.cpp`,
the page's smoke check and the probe (#331); the score harness (§5a).

**Never in CI**: any real engine or real document; the desktop engine is
off by default and no runner has the document. Huffman-coded Flate
streams are not exercised either: the probe's are stored blocks, and
inflation is libdeflate's business (`cmake/AmberfolioLibdeflate.cmake`).

**By hand, once each, reported per §8**: the archive edition through the
linked engine (`entries=58 extracted=58 recognized=58`; 57 of 58 begin
with their own printed heading, the other a `57` read as `37`); through
tesseract.js in a browser (99 of 99, #306); a real citation opening a
real entry (#232; `docs/seams.md` §10); the 14 pictures through the
linked decoder (§11.6). Not done: the desktop's installed engine on a
real page, a paragraph break in a real entry, a citation of an entry or
a tale, a picture on a display. #270 tracks the matrix rows.

## 8. Reporting an ingestion

What may go on #174 about an edition you hold and ingested:

- the edition's name and its SHA-256;
- how many entries the fact table has, how many were extracted, how many
  were recognized, and how many pictures;
- the engine's version string;
- `journal_store::fingerprint()`;
- the numbers of §5a: mean word confidence, words under the threshold,
  and the character and word error rates against your own corrections.

The desktop host prints every one of those on its own line. Not: any
text, any excerpt, any screenshot of an entry, any file. Ever.

## 9. The reader, and the one door between the two halves

The reader is a seam (`docs/seams.md` §10). This section is the join.

- **`journal_open`** takes a citation packed as §3 says; a host's
  `serve()` looks it up in the store. Four answers, each a different
  thing for a player to fix: the text; nobody has read a journal; no such
  entry; the entry is there and the engine read nothing off it.
- **`journal_seen`** (#222) carries nothing: the log in
  `machine::journal()` moved, and the service copies it into the store
  for the host to write. `automap_update` is the same shape.
- **The answer comes back in a buffer.** `serve()` returns `void`; the
  text goes into `machine::journal()`, observation and not machine state,
  dropped by `reset()` and absent from the state hash
  (`machine/journal.h`, `docs/seams.md` §3). The cap is four kilobytes;
  a longer entry is delivered truncated and the reader says so.
- **A correction is what the reader gets**; the reader cannot tell.
- **Two page sizes** (M5-E4d, #305): the frame drawer's full screen, or
  the roster panel. `journal_state::bar_live()`, set and cleared at the
  `Notes` splice's own points, says whether the party's own command-bar
  routine is running, the one precondition under which the program's
  screen composer may be asked to put the screen back. So a `Notes` row
  and F1 on the adventuring screen open a full screen; F1 at camp or with
  a vendor's bar up opens the panel; a citation opens the panel always,
  because it fires inside a script's narration with an NPC possibly in
  the viewport.
- **Reflow** (#316), in the wrap and not at ingestion, so the store keeps
  the engine's lines for a correction to be written against: a single
  newline is a space; a blank line is a paragraph break and gets one
  blank row; a line ending in a hyphen joins the word after it, hyphen
  dropped, when there is a letter on each side (a guess: `WITH-` against
  `WELL-`). Cost: a list of one-line items runs together; the fix is a
  blank line between items in the correction field.
- **Transliteration** (M5-E4c, #219): the panel draws one of the
  program's sixty-four glyphs per byte, so on the way into the delivery
  buffer curly quotes and dashes become their plain forms, an ellipsis
  three stops, any other code point or invalid byte one visible
  substitute. The store is not touched.
- **The bar** (#317, #329, #330). The listing and a full-screen page carry
  `NEXT`, `PREV` and `EXIT` on row `0x18`, flush left, one space apart,
  each chosen by its first letter; Escape closes too. Drawn as the
  program draws bars, in four calls: the row in the message green, then
  the three initials over their own cells in the bright. The panel keeps
  `F1 MORE`, because it sits beside the program's *live* bar, whose
  letters `N`, `P` and `E` already are.
- **`Notes` hands the highlight back** (#330): the bar routine's cursor
  group lives in the shared byte of M5-E1g (#304). `--watch 6B2B` reads
  `01` after a load, `07` from the frame `N` is pressed, `07` after the
  give-back. It is put back to what the routine was entered with, exact
  because `N` matches none of the program's own commands.
- **The listing** (#318, #319) is twenty rows and pages rather than
  scrolls: the screenful is the cursor's own page, replaced whole by
  `NEXT` and `PREV` and derived rather than kept. The cursor moves a row
  at a time inside a screenful and never slides one; `Return` opens the
  row it is on.
- **`Machine` methods** (M5-C1, #229): `journalStoreWrite`,
  `journalStoreRead`, `journalStoreStats`, `journalStoreChanged`,
  `journalStoreClearChanged`, delegating to `page/journal.mjs`. Every
  write raises `host::journal_store`'s changed flag; `journalStoreRead()`
  does not; lowering is the caller's: read the flag, write the store,
  *then* lower it.
- **Both hosts hand over the same store**: the desktop's file, read at
  the start of a run; the browser's tab, from the file input or
  `drive.mjs --journal-store`. Neither host writes to it from inside the
  game.

## 10. The cheat that cites everything (#301)

`--cite-all-journal` on the desktop and *Cite them all (cheat)* on the
dev page's journal panel, both `host::cite_all_journal()` beside
`restore_journal_log` in `hosts/common`. It fills the `Notes` listing so
a person can proof-read a whole store off the game's screen.

- It walks the store backwards (sorted by section then number, and
  `note_seen` puts each row on the front), so the listing reads Entry 1
  first, every row unread, each stamped with the machine's seeded wall
  clock.
- It clears nothing: `note_seen`'s move-up rule keeps an existing read
  flag, so a second call neither doubles nor unreads a row.
- It writes the log into the store through `set_seen`, the `journal_seen`
  service's own write, so the rows survive until the `seen` lines are
  removed or *Forget it* empties the store. With no journal ingested it
  does nothing and says so.
- It is a host action, not a seam, like `--forget-code-wheel`: nothing
  under `core/` moves, no host service is added, and
  `af_web_journal_cite_all` is a page export beside
  `af_web_journal_seen_restore`, not an entry point in `abi.h`.
- `journal_log_rows` went from 64 to 256 so a 99-item edition fits.

Evidence: `JournalCiteAll.*` in `hosts/common/tests/journal_store_test.cpp`
(`AWholeEditionFitsInTheLog` holds a store of the real edition's shape
with none of its words); step 7 of `hosts/sdl/cmake/run-journal.cmake`;
`tests/smoke.mjs` on the wasm module. The button has never been pressed
in a browser.

## 11. The entries that are pictures (#328)

Several entries are drawings rather than prose; an OCR engine reads only
the heading and caption off such a page. The drawing is drawn as a page
of the entry after its caption, and `NEXT` walks onto it. Ingestion is
§11.1 to §11.4; the reader is §11.5; §11.6 is the evidence.

### 11.1 Where the picture is

`journal_entry_fact::art` is a span of `journal_fragment` beside
`fragments`: a page, a stream offset, a length, the image's shape and a
rectangle, exactly like a piece of text. Rejected: a flag on a text
fragment, because four of the edition's fourteen pictures lie inside no
text rectangle of their own entry (three atlas maps crossing their
caption's columns, one drawing the width of a page).

The rule for the next edition:

- a text fragment is measured **to the column**;
- a picture is measured **to its ink**, and a printed rule around the
  drawing is inside the rectangle;
- the pieces of one entry's art are **separate pictures**, not one
  picture in pieces: an atlas of three maps is three pages.

Finding them, in two steps:

1. **The sieve.** Type has a pitch of about fourteen inked rows then a
   blank one; a picture has no blank rows while it lasts, so per column
   band a run of inked rows much longer than a line of type is a
   candidate. It is a sieve and not an answer: descenders bridge lines,
   prose produces runs of 140 rows, three pictures are 130–145 rows, and
   one (three runes, 41 rows) it cannot see at all. Look at every
   candidate and read every spread.
2. **The rectangle** is the bounding box of the ink inside a band whose
   top is below the entry's caption line. A printed rule, a row or column
   of the search area more than half ink, locates the band. Crop each
   result and look at it: the caption creeps into an ink box.

The result is in `hosts/common/src/journal_facts.cpp`. Not in the table:
the edition's legend for its map symbols, printed under its own heading
and belonging to no numbered item, so it has no key and is unreachable
from the game.

### 11.2 What it becomes

**The box.** A full-screen page's interior is 38 cells by 20 rows:
**304 x 160 pixels** at (8, 24) on the 320x200 screen. The panel's copy
is exactly half, 152 x 80, inside the panel's 176 x 96 body, so one stored
picture serves both shapes with a 2:1 average at draw time.
`machine::journal_art_width`/`_height` are declared in core and
`static_assert`ed against the frame in `seam_journal.cpp`.

**The fit** allows for the display: a pixel of the 320x200 mode is a
fifth taller than wide on a 4:3 screen, so a square drawing comes out
wider in pixels. The fourteen:

| picture | printed | drawn | scale |
| --- | --- | --- | --- |
| Entry 4 | 270x269 | 193x160 | 0.71 |
| Entry 10 | 255x208 | 235x160 | 0.92 |
| Entry 15 | 251x255 | 189x160 | 0.75 |
| Entry 22 | 272x172 | 304x160 | 1.12 |
| Entry 26 | 256x220 | 223x160 | 0.87 |
| Entry 28 | 268x137 | 304x130 | 1.13 |
| Entry 29 | 247x255 | 186x160 | 0.75 |
| Entry 35 | 210x41 | 304x49 | 1.45 |
| Entry 37, first map | 577x331 | 304x145 | 0.53 |
| Entry 37, second map | 579x401 | 277x160 | 0.48 |
| Entry 37, third map | 584x803 | 140x160 | 0.24 |
| Entry 41 | 254x253 | 193x160 | 0.76 |
| Entry 42 | 503x195 | 304x98 | 0.60 |
| Entry 58 | 269x268 | 193x160 | 0.72 |

Three are drawn larger than scanned: a small drawing in a black field
reads worse than one filling the box.

**Four tones, quantized nearest, no dither**: two bits a pixel, four
pixels a byte, rows padded to a byte (`journal_art_stride`), 12,160 bytes
for the whole box. Rejected: dither, because the scan's paper is not a
flat tone and a dither speckles the whole page. A two-level threshold is
legible on all fourteen and is the fallback if the store's size ever
matters; four levels keep the hairlines the box filter has turned grey.

**Levels rather than colours.** Rejected: quantizing to the sixteen EGA
colours at ingestion, because the palette is a fact about a running
machine. The store holds tone and the reader chooses the ramp (§11.5), so
the ramp changes without invalidating an ingestion, as
`explored_reveal_radius` does (`docs/explored-overlay.md` §5).

The reduction, in `journal_picture.h`:

1. **A box filter** to the fitted shape. Rejected: nearest, because it
   loses a hairline between two sample points.
2. **Normalization onto the page's own extremes**, both ends percentiles:
   the darkest half-percent and the lightest tenth. Rejected: the raw
   extremes, because one speck of dirt sets the black point; and the
   paper's mode as the white point, because a half-ink page or a gradient
   then has no range and comes back blank. Over the real fourteen the ink
   point is 59 to 151 and the paper point 252 to 255, so a fixed black
   point would lose one end.
3. **Nearest quantization** to `journal_art_levels`.

Integer arithmetic throughout, so a store's `fingerprint()` is stable
across builds of one host. Not across hosts: the JPEG decode in front of
the reducer is each image library's own, and two decoders differ on
0.37% of the pixels by exactly one level, so two hosts' ingestions of one
document are not expected to report the same hash.

The knobs, none needing a re-ingestion: `journal_art_levels`, the ramp,
and the fit.

### 11.3 Where it lives

In the store, as the `picture` record (§6). Rejected: a sidecar file on
the automap's pattern, because a store record rides every path the store
already has on both hosts (the `Machine` methods, the changed flag,
`drive.mjs --journal-store`, the clear on another edition,
`fingerprint()`). Cost: about 170 kilobytes packed, 230 as base64, beside
a text store of about 37.

The format is version 4; a version 3 store has no pictures and
re-ingesting adds them. There is no `corrected` beside a picture: a
better reduction is a re-ingestion.

### 11.4 Who decodes the page, which is the part nothing else needed

A `/DCTDecode` page reaches an OCR engine as its own bytes (§4a), but a
picture has no engine to hand the decoding to. `journal_page_decoder` is
a door and this tree has no decoder behind it:

- **The linked desktop build** (`AMBERFOLIO_LINK_TESSERACT`, §5) has
  Leptonica and libjpeg-turbo already. `hosts/sdl/src/leptonica_decoder.cpp`
  is a short file over `pixReadMem` and `pixConvertTo8`, the first of
  which `tesseract_linked_ocr.cpp` already calls on the same bytes.
- **A default desktop build** has no decoder and therefore no pictures
  out of this edition: `pictures=0/14`, with the filter named.
- **The browser** makes no pictures yet, so its ingestion and a linked
  desktop's produce different stores. It draws them: a store carried
  over from a desktop ingestion shows its pictures in a browser.

An edition this build decodes itself needs none of this, and that is
what CI runs: `journal_probe.h` has a Flate picture reduced on every
target with nothing installed, and a `/DCTDecode` one refused by name
with no decoder and reduced with a fixture one.

### 11.5 How it is drawn

Text pages are drawn by the program's own frame drawer and
`draw_string_entry` (§9). A picture has no such routine, so it is plane
surgery (`docs/seams.md` §3), on the automap's path, with a rect and a
level-to-index ramp; the packed levels are walked in place.

- **A picture is a page of the entry** after its text pages: `NEXT` walks
  from the caption into the drawing and `PREV` back. `reader_pages` in
  `seam_journal.cpp` is how many pages of text and how many pictures.
- **A refusal is a page too.** An entry can have pictures and no text, so
  `NOTHING WAS READ` is page one of two, and a citation opens the reader
  when the entry has a picture.
- **The full screen draws it whole and the panel halved**, a 2x1 average
  rounded toward ink on a tie, so a hairline is not lost.
- **`art_ramp`** in `seam_journal.cpp`, ink first: the program's bright,
  its grey, its dark grey, and black, because the reader draws on a black
  ground and paper is the ground.
- **It crosses on its own host service, `journal_art`**, into a buffer in
  `journal_state` on `automap.h`'s terms: twelve kilobytes of
  observation. The argument packs the citation and which picture, in the
  byte above the pair `journal_open_argument` packs. No ABI entry point:
  `serve()` is C++ inside the module on both targets.
- **Every answer carries the count**, refusals included, because the
  count is half of how many pages the reader draws. One picture crosses
  at a time; the entry's first is fetched with its text so the footer can
  say `1/2`.

**Trap: the full screen is painted in two arrivals.** A handler's own
pixels land the instant it runs; a call into the program lands when the
batch does, after the handler returns. So the arrival that turns onto a
picture page asks the program for the clear, the frame and the bar and
paints nothing; the arrival after it paints into the box. Drawn in one
pass the picture goes under the frame. `docs/seams.md` §8.4 carries it.

### 11.6 What is proven, and what is not

**In CI, on every target**, over the probe: the fit, the reduction's
three steps, the packing, both routes to the reducer, a decoder answering
the wrong page being caught, the store's round trip and refusals, and an
ingestion producing pictures with no OCR engine present
(`hosts/common/tests/journal_picture_test.cpp`, the `JournalStorePictures`
cases, `hosts/sdl/cmake/run-journal.cmake`). The reader's half, including
the panel's average, an entry with a drawing and no text, an atlas fetched
a page at a time, a page number outliving its entry, and the two-arrival
ordering: `tests/core/machine/seam_journal_test.cpp`'s `JournalArt` and
`JournalArtScreen`, and `hosts/common/tests/host_services_test.cpp`'s
`HostServicesArt`.

**By hand**, the linked desktop host with `--journal` over the archive
edition and the OCR engine off:

```
journal Pool of Radiance Adventurer's Journal, archive release entries=99
journal pages decoded by leptonica (linked)
journal entries=99 extracted=99 recognized=0
journal pictures=14/14
journal store <path> entries=0 corrections=0 pictures=14
  sha256=a0b81e0d8950beb4017f226f3f9af222218f4f80ade89c6e4ec743bfc226f4c4
```

`reduce_entry_pictures` over the same fourteen, from greymaps of the
scans, is byte-identical to the prototype §11.2's candidates were chosen
on. The harness, the greymaps and the store stay out of the repository.

**Not proven**: nobody has looked at a picture on a display, in either
size; they have been seen only as dumped stills at 2x. The ramp's four
tones are side by side in `tests/visual/reader-art-store.txt`'s synthetic
picture and have not been judged. #270 tracks the rows.
