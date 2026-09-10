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
pages are `/DCTDecode` (§4a). It has 58 entries in 75 pieces, and 14
pictures on 12 of those entries (§11). Six of the 18 boundaries between
those pieces are paragraph breaks rather than continuations (§5).

An edition is data in two places:

1. **The gate**, `core/src/machine/document.cpp`: the SHA-256 of the
   whole file, a name a player would recognize, `document_kind::journal`.
2. **The insides**, `hosts/common/src/journal_facts.cpp`: the same
   fingerprint and one `journal_entry_fact` per item: its section
   (`journal_kind`), the number the game uses for it, and its
   `journal_fragment`s, each with the page, the byte offset of the
   stream's first data byte, its `/Length`, the image's `/Width`,
   `/Height`, `/BitsPerComponent`, component count, filter,
   `/DecodeParms /Predictor`, whether it is `/Decode [1 0]`, the
   rectangle of that image which is that piece, and whether that piece
   opens a paragraph (step 7 below). Pictures are a second span, `art`
   (§11.1).

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
the 58, onto the facing page. Sixteen are in more than one piece.
`journal_entry_fact` carries a span of `journal_fragment` in reading
order, and what an engine reads is joined in that order — with one
newline, unless the piece's `begins_paragraph` says the boundary is a
paragraph break and not a continuation (§5).

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
   and scans — **but never across matter set outside the column grid**,
   and three pages of this edition carry some. A walk down the columns
   steps straight over it and hands it to whichever item ends beside it,
   which a reader then shows as that item's own words.
   - *Above the grid*, on the one page where a section begins: a display
     heading and a paragraph the width of the page, with a printed rule
     under them. The first item of the section stops at the foot of its
     own column and the next column begins below the rule, not at the
     head of the scan (#344).
   - *Below the grid*, on two pages: a legend belonging to no numbered
     item, and a drawing the width of a printed page, each under a rule
     of its own. Both columns end above that rule rather than at the foot
     of the scan (#357). This is the harder of the two to see, because
     the columns still end level with each other; find it by looking for
     a rule or a page-wide block low on a page and reading what the
     entries above it come back with.
4. **Check the numbering.** Entries are a counted chain: check it against
   the printed number on every scan; the last must land on 58. Tales and
   proclamations are read: they must ascend in reading order, and every
   rectangle must come back beginning with its own heading. Ascending
   order catches a swash `CIX` read as `CLIX`; settle by eye.
5. **Drop a piece with no ink in it**.
6. **Pictures** are found and measured by §11.1's rule.
7. **Mark the boundaries that are paragraph breaks** (#361). A boundary
   is a continuation by default, and this edition's six exceptions are
   ink rather than judgement: a paragraph opens with an indent, so
   compare the first line of the resuming piece with that piece's own
   left margin. Twelve of the eighteen start within two samples of it;
   six start 18 to 22 right of it. Nothing lands between, which is what
   makes the rule a measurement. Set `begins_paragraph` on the six.

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
`/DCTDecode`, `/DeviceRGB`, 8 bits a component. A page of *text* is
carried to the engine undecoded, and that is still the arrangement:
`extract_scan()` (`journal_extract.h`) answers one of two things:

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

A **picture** has no engine to carry a page to, so it needs the page
decoded, and since #345 every build can (`host/journal_jpeg.h`, §11.4).
That is not a JPEG decoder written here — #212's reason for refusing one
stands, and is the same reason `AmberfolioLibdeflate.cmake` gives for not
writing an inflate — it is a pinned one fetched at build time. Nothing on
the text path goes through it.

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

  **The engine is discovered rather than named** (#382,
  `hosts/sdl/src/ocr_discovery.h`): beside the binary first, because that
  is where a packaged build would put one, then each directory of `PATH`
  in the order the platform gave it. A miss is a report — the filename
  looked for and every place looked in — and not a silence, because "no
  engine" with nothing after it is the failure a player finds out about
  last. `PATHEXT` is deliberately not consulted: a `.bat` in front of an
  engine is a wrapper somebody wrote, and running one this host went
  looking for is a step past discovery into guessing.

  `--journal-ocr PATH` and `--journal-ocr none` win over discovery, and a
  build that carries its own engine (above) answers first and never
  searches. A discovered path is **not** written into the config file
  (`docs/hosts.md` §2a): discovery stays right when a player upgrades
  their engine, and a frozen path does not.

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
| two fragments the table calls two paragraphs | a blank line, the same join |

- A fragment boundary is **usually** a continuation (§3): one newline,
  because an entry is in pieces from running out of column and not from
  the writer stopping. Where it is not, the fact is
  `journal_fragment::begins_paragraph` and the join is a blank line
  (#361). No engine can see this: it is handed one rectangle, and the
  paragraph ended in the rectangle before it. Six of the tabled
  edition's eighteen boundaries are breaks, and each of them read as two
  sentences run together on one line until the flag existed.
- The program-driven engine derives paragraphs from Tesseract's `tsv`
  columns `block_num`, `par_num` and `line_num`. Trap: each count
  restarts inside its parent, so joining on `line_num` alone runs two
  one-line paragraphs together.
- The browser walks `blocks[].paragraphs[].lines[].words[]`. The linked
  engine's `TessBaseAPI::GetUTF8Text` already ends each line with `"\n"`
  and each paragraph with one more (tesseract 5.5.1's source; not run
  here).
- **The engine's paragraphs are not taken at face value** (#345). It
  measured badly on this edition: where the engine fails on the *first
  word* of a line, the line begins where its second word does, which
  looks to it exactly like an indent — so the reading loses a word and
  gains a break in the middle of a sentence at the same place. Two things
  have to be true instead: the line before it **ended** (a stop, or well
  short of the column), and the line itself **starts** something (a new
  block, or an indent past the column's own margin). Over the ninety-nine
  items: breaks after a finished sentence 157 → 183, breaks mid-sentence
  110 → 66.
- The margin, the far edge and the type's height are measured off the
  lines that were *kept*, so the indent is relative to the ink and the
  threshold survives a page drawn at any scale.
- A break is emitted only *between* two kept lines, so a clipped block
  gains none at the crop.
- Both hosts carry the identical rule: `hosts/sdl/src/tsv_words.h` argues
  for it and `hosts/web/page/journal.mjs` mirrors it, because a player
  should not get a different transcription for choosing a different host.

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
program-driven desktop engine hands the CLI a file and Tesseract's CLI
has no crop or scale flag, so a default desktop ingestion reads worse
than a browser one, and the linked engine (#216) is the way to close
that. The desktop's installed engine has never been measured. (§11.4's
decoder is not the missing piece: it would decode the page, but the CLI
takes a path and this host does not write an image out.)

The commonest surviving errors are the opening single quote that starts
each paragraph, read as a curly double quote or `*`, and hyphenated line
breaks, which §9's reflow joins.

## 6. The store

One file of UTF-8 lines, each text length-prefixed:

```
amberfolio-journal 5
edition <64 hex>
engine tesseract 5.5.1
scanned entry 12 431
<431 bytes><newline>
corrected entry 12 438
<438 bytes><newline>
picture entry 4 0 193 160 10404
<10404 bytes of base64><newline>
```

- `scanned <kind> <number> <bytes>` and `corrected <kind> <number>
  <bytes>`, each followed by exactly that many bytes and a newline. The
  kind is the section's lower-case word (§3). A version 1 store had no
  kind word and is read as a store of journal entries, then written back
  as the current version.
- `picture <kind> <number> <nth> <width> <height> <bytes>`, the body
  base64 (§11): which of the entry's pictures, and its size in screen
  pixels.
- Two texts per item and only one is ever overwritten: ingestion replaces
  `scanned` and never touches `corrected`; the reader shows the
  correction where there is one.
- Length-prefixed so a transcription line beginning `scanned entry 3 4`
  cannot be read as a header. A file that is not exactly this is refused
  whole, never half-read; CRLF is normalized first.
- A store of a different edition is cleared, not merged.
- **No read log.** A version 4 store had `seen` lines here; version 5 has
  none, and they went beside the save (§6a). A version 4 store's lines
  are still read and are then that run's working log, and it is written
  back as version 5 without them.
- Only a store's counts and `journal_store::fingerprint()` may be written
  down anywhere (§8).

**Where it lives** is a host's business (PLAN.md §4). The desktop writes
the file above. The browser writes the same text into `localStorage`, one
slot, and reads it back on the next load (M5-E3f). Rejected: IndexedDB,
because this is tens of kilobytes wanted synchronously as the module
comes up. A full or blocked drawer and a store this build cannot read are
each a sentence on the page; an unreadable store is left where it is. The
page's *Forget it* button empties the drawer and the tab's copy.

## 6a. The read log, which is not in the store (#351)

The store above is about the player's **document**: what an engine read
off their copy, what they corrected, what its drawings look like reduced.
Those are true however many parties they run, which is why one file in
the per-user data directory is right for them.

Which entries the game has *cited*, when it said so, and whether they
have been opened since is about a **playthrough**. One list between two
parties tells each that it has already been sent somewhere it has never
been — the same complaint the automap's exploration answered correctly
and this answered wrongly. So the log lives where the exploration does:
a sidecar beside the saves, per slot, off unless the player asked
(`hosts/common/.../slot_store.h`).

| where | what |
| --- | --- |
| `\SAVE\AFSEEN.DAT` | the working log, written whenever it moves |
| `\SAVE\AFSEEN<L>.DAT` | slot `L`'s snapshot, written at that slot's save and read **over** the working log at its load, even when it is not there |
| the browser's `amberfolio.journal.log.v1` drawer | the page's working log, base64 of exactly those bytes (`af_web_journal_log_write`/`_read`), because a browser has no directory to put a sidecar in until M6 gives it a disk |

Binary and fixed-stride where the store's own file is text: `AFS`,
version 1, a count and a stride, then eight bytes a row — section,
number, month, day, hour, minute, and whether it has been opened. Text
was right for a transcription a player edits; nobody hand-edits a list of
what the game said and when, and a fixed stride is what lets a host write
one from a seam's callout without allocating.

`--save-sidecars` is the flag on the desktop, `saveSidecars(true)` on the
page, and it is the same flag the automap's sidecar rides: the permission
being asked for is "may this build write its own files beside your
saves", and that sentence is the same one for each — which is why it is
one question and both hosts ask it once (#385, `docs/hosts.md` §2b). An
empty log writes no new file; it replaces one that is there.

The store's own **changed flag** is the text's; `log_changed()` is the
log's. Without the split a citation would have a host rewrite a player's
whole transcription to record something that is no longer in it.

**The rows still live in `journal_store`** and reach the reader exactly
as they did: `set_seen` puts them in, `restore_journal_log()` hands them
to the machine. Nothing above the store moved.

**The read log is restored by `host::restore_journal_log()`** in
`hosts/common`, called by both hosts (the web export is
`af_web_journal_seen_restore`, #237, which reads the sidecar first). Trap:
the store and `machine::journal_state` both hold the log newest first and
`note_seen` puts each row on the *front*, so rows fed in stored order come
out upside down; `JournalLogRestore` pins the order.

**On the page it is `Machine.journalSeenRestore()`** (#288), beside the
five store methods of #229 and delegating to that same export. Call it
after `journalStoreRead()` and on the machine the reader will run in:
`journalStoreRead()` puts back a store's *text*, and this puts back the
half of it that lives in the machine. Twice is harmless. Before #288 the
only spelling was `journal.mjs`'s module-level `restoreSeen(module,
handle)`, so a consumer built on the facade alone lost every `*` on
reload. `tests/smoke.mjs` reads a version 4 store with a `seen` line
through the facade and then cites everything, because
`cite_all_journal()` copies the machine's log back into the store and
`note_seen` keeps a row's read flag: the restored row comes back read and
a row nobody restored does not.

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
the page's smoke check and the probe (#331); the score harness (§5a). The
probe's carried entry has a second piece that **opens a paragraph**, so
the flag of #361 crosses the extractor, the ABI and the page on every
target, and the join it feeds runs on its own in
`hosts/common/tests/journal_ocr_test.cpp` and the page's smoke check —
which is the only way to run it, no real engine being there to ask.

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
- **A citation records and opens nothing** (#346). The game's own
  narration is what tells a player a note arrived; the citation watch
  puts the entry on the `Notes` log with the moment it was named, still
  carrying its `*`, and stops. No host is asked for the text or the
  pictures until somebody opens the entry.
- **One page size and one way in** (M5-E4d #305, #346): the frame
  drawer's full screen, twenty rows of thirty-eight characters, opened
  off the `Notes` command and off nothing else.
  `journal_state::bar_live()`, set and cleared at the `Notes` splice's
  own points, says whether the party's own command-bar routine is
  running, which is the one precondition under which the program's screen
  composer may be asked to put the screen back — and `Notes` satisfies it
  by construction, being a word on that bar. **So the reader reaches what
  the log holds and nothing else**: there is no way to name an entry the
  game has not cited, and `journal_open` is called for a row of the log
  or not at all. `host::cite_all_journal()` (§10) is how a whole store is
  read. This seam rasterizes nothing and reads no font; the program draws
  every word it shows.
- **Reflow** (#316), in the wrap and not at ingestion, so the store keeps
  the engine's lines for a correction to be written against: a single
  newline is a space; a blank line is a paragraph break and gets one
  blank row; a line ending in a hyphen joins the word after it, hyphen
  dropped, when there is a letter on each side (a guess: `WITH-` against
  `WELL-`). Cost: a list of one-line items runs together; the fix is a
  blank line between items in the correction field.
- **Each section is numbered the way its own booklet numbers it**
  (#358): decimal for entries and tales, a Roman numeral for
  proclamations, on the listing's rows and in a page's title alike.
  §3 keeps a proclamation's number as the numeral's *value* because
  comparing, sorting and keying all want a number; the writing is owed
  back here, and `machine::journal_number_as_printed()` is it — the
  mirror of the recognizer that reads the numerals the program itself
  writes, and checked against it over every number the grammar can say.
- **Transliteration** (M5-E4c, #219): the program draws one of its own
  sixty-four glyphs per byte, so on the way into the delivery buffer
  curly quotes and dashes become their plain forms, an ellipsis three
  stops, any other code point or invalid byte one visible substitute. The
  store is not touched.
- **The bar** (#317, #329, #330, #341, #342). The listing and a
  full-screen page carry `NEXT`, `PREV` and `EXIT` on row `0x18`, flush
  left, one space apart, each chosen by its first letter; Escape closes
  too. `NEXT` and `PREV` are only on the bar when there is a screenful to
  turn to in that direction - an empty log or a one-page entry carries
  `EXIT` alone - and a dropped word is not a gap: the rest close up, the
  same way the program's own bars do. Drawn as the program draws bars,
  in one call for the row in the message green and one more for each
  word's initial over its own cell in the bright - up to four, fewer on
  any bar that has dropped a word. There is no other key: since #346 this
  seam claims nothing at all while the reader is down.
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
  service's own write, so the rows reach the sidecar beside the save
  (§6a) and survive until something replaces them or *Forget it* empties
  the drawer. With no journal ingested it does nothing and says so.
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

- a text fragment is measured **to the column**, and **cut around the
  art**: above the drawing, and again below it where prose continues,
  with a piece that has no ink left in it dropped (#357). An engine reads
  a drawing too, and everything it makes of hand lettering arrives as the
  entry's own words — the label beside a maze's door, an engine's two
  guesses at two runes, the place names of an atlas — while the reader
  draws the picture anyway;
- a picture is measured **to its ink**, and a printed rule around the
  drawing, the drawing's title, and any label printed outside its frame
  are inside the rectangle: they are the drawing's words, not the
  entry's;
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
   result and look at it: the caption creeps into an ink box, and a
   drawing's own words creep out of one. A map's title and the labels
   printed outside its frame are the drawing's and belong inside the
   rectangle; a line of the drawing that falls outside it is the mistake
   to look for, and one map lost its bottom border that way (#357).

The result is in `hosts/common/src/journal_facts.cpp`. Not in the table:
the edition's legend for its map symbols, printed under its own heading
and belonging to no numbered item, so it has no key and is unreachable
from the game.

### 11.2 What it becomes

**The box.** A page's interior is 38 cells by 20 rows: **304 x 160
pixels** at (8, 24) on the 320x200 screen. A picture is reduced to that
box once, at ingestion, and drawn whole.
`machine::journal_art_width`/`_height` are declared in core and
`static_assert`ed against the frame in `seam_journal.cpp`.

**The fit** allows for the display: a pixel of the 320x200 mode is a
fifth taller than wide on a 4:3 screen, so a square drawing comes out
wider in pixels. The fourteen:

| picture | printed | drawn | scale |
| --- | --- | --- | --- |
| Entry 4 | 270x269 | 193x160 | 0.71 |
| Entry 10 | 254x214 | 228x160 | 0.90 |
| Entry 15 | 251x255 | 189x160 | 0.75 |
| Entry 22 | 272x172 | 304x160 | 1.12 |
| Entry 26 | 256x220 | 223x160 | 0.87 |
| Entry 28 | 268x137 | 304x130 | 1.13 |
| Entry 29 | 247x255 | 186x160 | 0.75 |
| Entry 35 | 210x41 | 304x49 | 1.45 |
| Entry 37, first map | 577x359 | 304x158 | 0.53 |
| Entry 37, second map | 579x421 | 264x160 | 0.46 |
| Entry 37, third map | 584x814 | 138x160 | 0.24 |
| Entry 41 | 254x253 | 193x160 | 0.76 |
| Entry 42 | 502x176 | 304x89 | 0.61 |
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

Integer arithmetic throughout, and since #345 one decoder in front of it
on every target (§11.4), so a store's `fingerprint()` is stable across
builds and across hosts: two ingestions of one document report the same
hash wherever they were made.

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

### 11.4 Who decodes the page, and why every build now does (#345)

A `/DCTDecode` page reaches an OCR engine as its own bytes (§4a), but a
picture has no engine to hand the decoding to. `journal_page_decoder` was
built as a door for a host to fill out of something it already linked,
and what that produced was a feature one build of one host had:

- the `AMBERFOLIO_LINK_TESSERACT` desktop build made pictures out of
  Leptonica;
- a default desktop build reported `pictures=0/14` and named the filter;
- the browser had no picture path at all.

So a player who had done nothing wrong saw an entry's caption with the
map missing under it, and no way to tell that from a journal with no
drawings in it. The decoder is now `host/journal_jpeg.h`, compiled into
the library both hosts link, and every build has it;
`cmake/AmberfolioStbImage.cmake` says why it is stb_image and not the
libjpeg-turbo the OCR chain already builds.

The door itself stays, and `set_page_decoder(nullptr)` still means *no
decoder* - which is what an edition in a filter nobody has written code
for really gets, and what the probe's refusal cases are.

**One decoder, so one answer.** While Leptonica did this on one build and
nothing did it on the others, two ingestions of one document could not be
expected to agree; §11.2's note that two decoders differ on 0.37% of the
pixels by one level was about exactly that. They agree now. That same
0.37% is what checked the new decoder: stb_image's fourteen against
Leptonica's fourteen, identical shapes, no pixel more than one level
apart.

CI proves it on every target over the probe, whose `/DCTDecode` page is a
baseline JPEG this project assembles marker by marker - a hand-written
encoder and a third-party decoder agreeing on a page is a fact about both
of them (`hosts/common/tests/journal_jpeg_test.cpp`).

### 11.5 How it is drawn

Text pages are drawn by the program's own frame drawer and
`draw_string_entry` (§9). A picture has no such routine, so it is plane
surgery (`docs/seams.md` §3), on the automap's path, with a rect and a
level-to-index ramp; the packed levels are walked in place.

- **A picture is a page of the entry** after its text pages: `NEXT` walks
  from the caption into the drawing and `PREV` back. `reader_pages` in
  `seam_journal.cpp` is how many pages of text and how many pictures.
- **A refusal is a page too.** An entry can have pictures and no text, so
  `NOTHING WAS READ` is page one of two rather than the whole entry.
- **It is drawn whole**, in the box it was reduced to.
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
an entry with a drawing and no text, an atlas fetched
a page at a time, a page number outliving its entry, and the two-arrival
ordering: `tests/core/machine/seam_journal_test.cpp`'s `JournalArt` and
`JournalArtScreen`, and `hosts/common/tests/host_services_test.cpp`'s
`HostServicesArt`.

**By hand**, a **default** desktop host with `--journal` over the archive
edition and the OCR engine off:

```
journal Pool of Radiance Adventurer's Journal, archive release entries=99
journal pages decoded by stb_image
journal entries=99 extracted=99 recognized=0
journal pictures=14/14
journal store <path> entries=0 corrections=0 pictures=14
  sha256=f7e23b0bc10b0fa7e05808725ff12234b4633176766f6d7fdfb8fea6c2728807
```

Those fourteen were compared, picture by picture, with the fourteen the
linked build made out of Leptonica before #345: identical shapes, and
0.37% of the levels one step apart, which is the difference between two
JPEG decoders and nothing else (§11.4).

**And in a browser**, the built page driven headlessly over the same
document: `99 of 99 entries read by tesseract.js 6.0.1, 14 of 14
pictures`. That is the number that was zero, on the host most players
use. `reduce_entry_pictures` over the same fourteen, from greymaps of the
scans, is byte-identical to the prototype §11.2's candidates were chosen
on. The harness, the greymaps and the store stay out of the repository.

**Not proven**: nobody has looked at a picture on a display; they have
been seen only as dumped stills at 2x. The ramp's four
tones are side by side in `tests/visual/reader-art-store.txt`'s synthetic
picture and have not been judged. #270 tracks the rows.
