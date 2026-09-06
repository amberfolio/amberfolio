# The journal

*How a player's own Adventurer's Journal becomes text this emulator can
show, and what is deliberately not here. M5-E3, issue #174; PLAN.md §5
item 2.*

This document is the half underneath the reader: locating each entry's
scan inside the player's own PDF, decoding it, reading it once with an OCR
engine, and keeping the result on the player's machine. **The reader
itself is M5-E4 (#175)** — a seam, `docs/seams.md` §10 — and §9 below is
where the two meet.

**Nothing from a journal is in this repository, and nothing ever will
be.** Not a page, not an image, not a word of text, not a fixture that
resembles one. What is here is *facts about* a document — a fingerprint,
a page number, a byte offset, an image's width — which is the same rule
CONTRIBUTING.md applies to the game binary, and it is reviewed the same
way. The text a player ends up with is read off their own copy, on their
own machine, and stays there.

## 1. The pieces

| Piece | Where | What it does |
|---|---|---|
| The document gate | `core/.../machine/document.h` | Fingerprints a document the player holds, and gates a seam on it (#171). A gate is over bytes; it never looks inside. |
| The fact table | `hosts/common/.../journal_facts.h` | Per edition, per entry, **per piece of it**: the page, the stream's offset and length, the image's shape, the rectangle that is that piece (§3). |
| The extractor | `hosts/common/.../journal_extract.h` | Follows an offset, inflates the stream, undoes the predictor, expands samples to gray, crops the region. |
| The engine | `hosts/common/.../journal_ocr.h` | One virtual call. The desktop runs the player's own Tesseract; the browser drives tesseract.js. |
| The store | `hosts/common/.../journal_store.h` | Section and number to text, with what the engine read and what a person corrected kept apart. |
| The reader | `core/.../machine/seam_journal.cpp` | The seam that shows an entry in the game (§9). The only thing any of the above is *for*. |

The first five are the ingestion and run once; the sixth is the reader
and runs for ever after. `journal_ingest.h` is the order the first five
go in, and its header says why that
order and no other. `journal_probe.h` is a synthetic document this
project generates, which is what any of this is tested against.

## 2. Running it

On the desktop:

```sh
amberfolio <dir> <program.exe> --journal ~/Documents/journal.pdf
amberfolio <dir> <program.exe> --journal J.pdf --journal-store ./journal.txt
amberfolio <dir> <program.exe> --journal J.pdf --journal-ocr /opt/bin/tesseract
amberfolio <dir> <program.exe> --journal J.pdf --journal-ocr none
```

`--journal` presents the document the way `--document` does — so a
journal-gated seam arms — and then reads inside it. The store goes to
`journal.txt` under this platform's per-user data directory (SDL's
`SDL_GetPrefPath`, which is where M6's configuration will live too);
`--journal-store` says otherwise. The run then continues normally: an
ingestion that goes wrong is a sentence and nothing else, because a
player who could not read their journal still asked to play.

In a browser: the dev page's **read your own journal** file input. It
says what happened in the same words the desktop host prints, because
both hosts print `journal_trouble_name()`.

And then, on either host, `--seam journal` (or the page's checkbox) turns
the reader on and the text is reachable from inside the game (§9). The
store is read at the start of every run that asks for the reader, so an
ingestion is something a player does once:

```sh
amberfolio <dir> <program.exe> --seam journal
```

## 3. Adding an edition

**There is one**, since M5-E3b (#214): the Adventurer's Journal as the
currently sold archive release ships it — the same release every other
fact in this tree was gathered against. Its fingerprint is
`67cbfc0c833b835494310680ad298bc4de1cdcc0168115cc3608c2f6074c737c`, which
is a fact about a file and is all that may be written down about it. Its
pages are `/DCTDecode` (§4a) and it has **fifty-eight entries in
seventy-eight pieces**.

An edition is data in two places:

1. **The gate**, in `core/src/machine/document.cpp`: the SHA-256 of the
   whole file, a name a player would recognize, and
   `document_kind::journal`.
2. **The insides**, in `hosts/common/src/journal_facts.cpp`: the same
   fingerprint, and one `journal_entry_fact` per item — which of the
   journal's numbered sections it is in, the number the game itself uses
   for it, and its `journal_fragment`s, each with the page
   it is on, the byte offset of the stream's first data byte, its
   `/Length`, the image dictionary's `/Width`, `/Height`,
   `/BitsPerComponent`, component count, filter and `/DecodeParms
   /Predictor`, whether it is `/Decode [1 0]`, and the rectangle of that
   image which is that piece of the entry.

The suite checks the two against each other, so an edition in one and not
the other fails in CI rather than on a player's machine. It also checks
every row's shape: a region inside its image, no two rows for one
*(section, number)*, pieces in reading order, no entry that mixes a
decoded filter with a carried one, and a filter this build can carry at
all.

### Three numbered sections, and why a row says which (M5-E3d, #218)

The journal prints three things the game sends a player to by number, and
**each numbers from its own base**: journal entries 1–58, tavern tales
1–23, and proclamations, in Roman numerals, 59–214 with gaps. Tale 4 and
Journal Entry 4 are both `4` and are not the same text.

So a number identifies nothing on its own. `machine::journal_kind` is the
other half of the key, and it is the *whole* mechanism this added — it
rides on the fact table's row, on the store's key, on the citation the
recognizer answers with, and on the word the reader draws. Everything
else about the two new sections is data.

It lives in core rather than beside the fact table because the recognizer
is what decides it, and the recognizer is core. The host's
`journal_facts.h` re-exports it under its own namespace rather than
spelling it a second way.

**The seam's callout did not get wider for it.** `journal_open` carries
one `std::uint32_t` and always did; a citation packs into it as
`kind << 16 | number` (`journal_open_argument`). The ABI is a cost every
embedder pays, and a kind is three values.

**The store's format went to version 2** for the same reason, gaining one
lower-case word per record. A version 1 store is still read — it had no
kind because there was one section, so every record in one is a journal
entry and saying so loses nothing — and is written back as version 2.
Refusing it would have thrown away a player's corrections to make a
point.

The proclamations are stored as the *value* of their numeral, not its
spelling: a numeral is a way of writing a number, and the reader does the
writing.

`--journal` on a document whose edition is not in the table prints its
fingerprint, which is the first half of the row somebody has to write.

### An entry is a list of pieces, and this is why

The entries of this edition are set two columns to a printed page and two
printed pages to a scan, and they **flow**: an entry runs out of its
column and resumes at the top of the next, and four of the fifty-eight
resume on the facing page — a different scan, a different stream
altogether. Eighteen are in more than one piece.

A row of one rectangle could describe none of those: the bounding box of
two columns swallows the entries between them, and the first piece alone
is half a sentence. So `journal_entry_fact` carries a span of
`journal_fragment`, each with its own stream and rectangle, **in reading
order** — and what an engine reads out of them is joined in that order.
Most fragments of most editions will repeat their neighbour's offset,
which is the price of being able to say the thing that is true.

### How this edition's rectangles were found

Written down because the next person needs to know whether to trust them,
and because the method is the method for any edition:

1. **The scan geometry, measured.** The four column bands of a spread are
   the blank vertical bands wide enough to be gutters; the body's bottom
   is above whichever footer rule the printed page carries.
2. **The headings.** Two methods, and which one a section needs is a
   fact about how it is set.

   The **entries** open with the same long phrase in a display face at
   the column margin, so one instance of its bitmap correlated down each
   column finds the rest. Gap and line-width rules were tried first and
   are not good enough: a paragraph's last line is short and starts at
   the margin exactly as a heading does, and the gap above a heading is
   not separable from the gap above a paragraph.

   The **tales** and **proclamations** cannot be found that way at all.
   "Proclamation" is set in the body face at the body size, so a template
   for it correlates as well with any line of prose; "Tale" is four
   characters, indented into its own paragraph. So each column is cut
   into lines and an *engine* is asked what each line opens with — which
   is only possible since #216 put one in the build. A line that opens
   with the section's word is a heading and its number is read again on
   its own, with a whitelist of the only letters it can contain: without
   that, `LXXVIII` comes back as `LXXVIIT` and nothing afterwards can
   know whether that last letter was an `I` or a `T`.
3. **An entry runs from its heading to the next one**, wherever that
   falls — down its column, on into the next, on into the next scan.
4. **The numbering is a chain**, so it was checked against the printed
   numbers on every one of the nine entry scans. Two of them are where a
   chain would drift silently and neither did: the maps scan, whose
   single entry covers it end to end, and the last scan, which has to
   land on fifty-eight.

   For the other two sections the number is *read* rather than counted,
   so the check is different: both ascend in reading order, and 23 of 23
   tales and 18 of 18 proclamations come back out of their own rectangles
   beginning with their own printed heading. Ascending order is what
   caught the one misreading — an italic `CIX` whose `I` carries a swash,
   called `CLIX` by a plain run and a whitelisted one alike, and settled
   by eye against the scan.
5. **A piece with no ink in it is dropped.** An entry that ends exactly at
   the foot of its column would otherwise carry an empty rectangle.

The tooling that did this is not in the repository and should not be: it
reads a document this project does not have and must never carry. What is
here is its output, which is facts.

## 4. What the extractor decodes, and what it refuses

**Two questions, not one**, since M5-E3a (#212): whether this build can
get an entry as far as an engine, and whether it turns the stream into
samples of its own on the way. `journal_filter_supported()` and
`journal_filter_decoded()` are those two, and keeping them apart is what
lets a JPEG-paged edition work with no JPEG decoder in this tree.

**Decoded here**: `/FlateDecode` and unfiltered streams; 1 and 8 bits per
component; one component (gray or bilevel) and three (RGB, converted to
gray by the standard luma weights); PNG predictors 10–15, all five row
filters; `/Decode [1 0]` and `/ImageMask`. What comes out is eight bits
of gray a pixel, cropped to the entry.

**Carried, not decoded**: `/DCTDecode`. Its stream goes to the engine as
its own bytes with the entry's rectangle beside it (§4a).

Refused **by name**, which is "log, don't fake" one level up from a
service: `/CCITTFaxDecode`, `/JBIG2Decode`, TIFF's predictor 2, and any
bit depth or component count not listed above. Neither fax filter was
built on spec and neither is now. The day somebody has a document that
needs one is the day that code gets written, with the document in front
of them — which is exactly how `/DCTDecode` got here, and the shape of
what got written is §4a.

## 4a. The pages this build does not decode (M5-E3a, #212)

The first real edition anybody put in front of this — the Adventurer's
Journal as the currently sold archive release ships it — is a 21-page PDF
whose every page scan is `/DCTDecode`, `/DeviceRGB`, 8 bits a component.
So the whole pipeline answered `filter_unsupported` for every entry of
it, and would have with a perfect fact table in front of it.

**What was written is not a decoder.** §4 had already settled that, before
the document arrived, and the reason holds: this project's argument for
using libdeflate rather than writing an inflater is that a decoder tested
only against its own encoder is untested, and a JPEG decoder here would
have been exactly that, for a format this project has no way to generate
at scale.

So `extract_scan()` answers one of two things and says which
(`journal_extract.h`):

| | `gray` | `jpeg` |
|---|---|---|
| what the engine gets | samples this build produced | the stream, byte for byte |
| already cropped | yes | **no** |
| who applies the region | the extractor, before the engine sees it | the engine, to its own output |

**The crop moves, and that is the whole cost.** This build can crop what
it decoded and cannot crop what it did not, so an encoded scan reaches
the engine as a whole page plus a rectangle, and what gets filtered is
the engine's *output*. Both engines already report where each word was —
Tesseract through its `tsv` output, tesseract.js through the `bbox` on
every word of `data.blocks[].paragraphs[].lines[].words[]` — so the
filter reads a number they were producing anyway. A word counts as inside
when its centre is, which gives the same answer a crop would for every
word a crop would not have cut in half.

**What the first real browser sitting found** (#306). The page read
`data.words[].bbox`. That is the shape of tesseract.js 4; the pinned
tesseract.js 6 has no flat `words` list at all, and answers a `blocks`
tree only when the call asks for `{ blocks: true }` — its worker's own
default output is `{ text: true, blocks: false, ... }`. So the archive
release went through the page as

    Pool of Radiance Adventurer's Journal, archive release: 0 of 99
    entries read by tesseract.js (unversioned) (entry 1: the OCR engine
    did not read it)

with the engine having read every page: the filter walked an `undefined`
and answered nothing, and an empty answer is what `ingestJournal()`
reports as "did not read it". The smoke check that covered the filter had
handed it a `words` array the test itself wrote, in the old shape — a
test of the test.

Now the shape it reads is the one measured off the pinned engine in a
browser: `data.blocks[] → paragraphs[] → lines[] → words[]`, each word
`{ text, bbox: { x0, y0, x1, y1 }, confidence, ... }`, and a line is the
engine's own grouping rather than a guess from word identity. An answer
with no `blocks` array is **refused with a sentence** rather than read as
a blank page, an engine's throw reaches the report on the entry it
happened on instead of taking the ingestion down, and when the rectangle
keeps nothing the report says whether the page had words on it.

The same sitting found two more things. The gray path handed the engine
an `ImageBitmap`, which the pinned version does not read — nor an
`ImageData`; both come back as `Error attempting to read image`, because
its `loadImage` takes a URL, a data URL, an `IMG`/`VIDEO`/`CANVAS`
element, an `OffscreenCanvas`, a `File` or a `Blob`, and hands anything
else to `new Uint8Array(...)`, which for those two is empty. So the gray
path hands it a canvas. And the UMD bundle exports no version at all
(`languages, OEM, PSM, createScheduler, createWorker, setLogging,
recognize, detect`), which is where `(unversioned)` came from — so
`scripts/fetch-ocr-engine.py` writes a `version.txt` beside the engine
and the name is read off that.

Driven again after the fix, in headless Edge over the same document,
served from a local build with the engine staged by that script:

    Pool of Radiance Adventurer's Journal, archive release: 99 of 99
    entries read by tesseract.js 6.0.1 - kept in this browser for next
    time

in 321 seconds, with nothing in the browser's console. Entry 1's
rectangle — `left 702, top 270, 290x413` of a 1328x1003 two-page scan —
comes back beginning `Journal Entry 1:`, which is the whole of what the
region filter is for. **What that is, is a count.** Whether ninety-nine
transcriptions are ninety-nine *good* transcriptions is a person reading
them, and #236 still owns that, along with the desktop's installed engine
having never read a page at all.

It is written into `journal_ocr.h` rather than left to each host on
purpose: two hosts that filtered differently would give a player two
different transcriptions of one page and neither could be called wrong.

**What is still checked about a stream nothing looks inside**: that the
offset and length name bytes of *this* document, and that the region is
inside the shape the table gives. The second is the check the crop used to
make for free, and losing it silently would have been the one real cost of
not decoding — a rectangle off the edge would have reached the engine as a
filter that quietly matched nothing, which reads exactly like an engine
that could not read the page.

It is not a PDF parser and will not become one. The objects are not
found, the cross-reference table is not resolved, the page tree is not
walked. The editions are known, an unrecognized one was already refused,
and the offsets are facts. What that buys is the good failure mode: a
table row that is wrong points at bytes that do not inflate, or inflate
to the wrong size for an image of that shape, and the extractor says
which — it cannot quietly produce a picture of something else.

## 5. The OCR engines, and the decisions about them

`.tesseract-version` and `.tesseract-js-version` pin them the way
`.emscripten-version` pins emsdk. Both are Apache-2.0, which
CONTRIBUTING.md's inbound rule allows.

**Desktop: the player's own Tesseract, run as a program — or one this
build carries.** Two engines, one interface, chosen by a build option
(M5-E3c, #216).

`AMBERFOLIO_LINK_TESSERACT=ON` builds Tesseract, Leptonica and
libjpeg-turbo once and links them in, so a player who has installed
nothing still gets a reader. It is **off by default**, because it is a
long one-time build and a contributor who did not ask for it should not
pay for it; `cmake/AmberfolioTesseract.cmake` is the whole argument. What
that build gets is also slightly better: the linked API can crop to a
fragment's rectangle directly (`SetRectangle`), where the program-driven
one must read the whole page and filter the words afterwards.

Everything below is about the program-driven engine, which is what a
default build uses and what the argument was originally about. **Not
linked, not vendored, not fetched by the build.** Tesseract is a large C++
dependency with a large C++ dependency of its own, and every contributor
would pay for it at every configure — for a feature that runs once,
during onboarding. Nothing is combined with anything, so there is no
licence question to have an opinion about. The version is asked of the
engine at ingestion and written into the store, so a store always says
what read it. `hosts/sdl/src/tesseract_ocr.h` is the whole argument.

What it costs is stated rather than hidden: an engine that is not
installed is an engine that is not there, and the host says so in as many
words. Whether a packaged desktop build should ship one is M6's question,
and `journal_ocr.h`'s interface is what makes it answerable without
touching anything above it.

**Browser: tesseract.js, served from the page's own origin.**
`scripts/fetch-ocr-engine.py --into <the served directory>` fetches the
pinned library, its wasm core and one language's data into
`vendor/tesseract/` beside the module. Nothing is committed — the same
arrangement the conformance vectors have, for the same reasons.

The deployed page **does not reach a CDN at runtime**. #174 permits it
only if the page says so, and a page that quietly pulled several
megabytes of somebody else's JavaScript the moment a player picked a file
would be doing something the player did not ask for and could not see; it
would also make the engine a moving target, and an OCR result nobody can
reproduce is not much of a result. `journal.mjs` therefore names every
path tesseract.js might reach for — worker, core, language data — and
they are all under the directory the library itself came from. When the
engine is not there, the page says exactly that and names the script that
fetches it, and the ingestion still runs: every entry is located and
decoded, nothing is recognized, and both numbers are reported. The
script also leaves a `version.txt` there, because the bundle does not
export its own version and the store's engine line should say what read
it (#306).

**The deployed page ships with the engine** (M5-E3e). It did not until
now: nothing in CI ran the fetch, so https://amberfolio.vercel.app
located and decoded a recognized journal's entries, recognized none of
them, and said so. That was defensible while no edition was in the table
— there was nothing to read — and indefensible the moment one was
(#214), because the page then did all the work and threw the answer
away. The wasm job runs the fetch and the artifact carries
`vendor/tesseract/`.

**32 MB is what that costs, and almost nobody pays it.** All fifteen
files have to be deployed, because tesseract.js chooses its core variant
at run time from what the browser supports. But `loadEngine()` is called
when a journal is ingested, not when the page loads, so a visitor who
never picks a PDF downloads none of it, and one who does fetches about
seven megabytes — the loader, the worker, one core and the language data.

The fetch script pins versions **and now pins digests**. It could not at
first, because this repository may not carry a fingerprint nobody has
computed; it trusted on first use and pinned afterwards. Somebody has
computed them since: `scripts/ocr-engine.sha256sums` records them,
fetched twice on different days and identical both times. It records the
bytes **as upstream served them**, not the bytes that reach the disk: the
language data is gzipped by the script because the page asks for it that
way, and a gzip stream is not reproducible across machines — a Python
built against zlib-ng and one built against stock zlib compress the same
input differently, which is how a record written here first failed on a
runner. What a digest pins is that upstream served what was expected.
`--digests` points a run at a record rather than at whatever a previous
fetch left lying about. A CI runner has no previous fetch, so without a
committed record every deploy would have been trust on first use — which
is not a thing to do with somebody else's JavaScript on a page other
people load. A mismatch fails the build; when `.tesseract-js-version`
moves, the record is stale by design and `--force` writes the new one.

## 5a. How well it reads, and how anybody knows (#315)

Everything in §5 was decided without a number. #315 was filed off a real
entry on the game's own screen that was readable and wrong in four
different ways, and its first instruction was the right one: **make it
measurable before touching a single engine setting.**

### The ground truth is the player's own corrections

A comparison needs something to compare against, and the obvious source —
two hand-typed entries in the test tree — is exactly what this repository
may never hold (§6, CONTRIBUTING.md). What goes in the repository is the
harness; both halves of the comparison stay on the player's machine.

And the store already holds both halves. Each item carries what the
engine read and, wherever a person has been in there, what they wrote —
and **a player who corrected an entry has produced ground truth for it.**
So a score is free for exactly the entries somebody cared enough to fix,
needs no new file and no new format, and gets better the more of their
journal a player has been through. `hosts/common/.../journal_score.h` is
the whole argument, including the one way this could have been made
misleading (scoring uncorrected entries against themselves, which would
make the number *improve* every time a player found a mistake).

The number is the standard character error rate — Levenshtein distance
over the length of the truth — with a word rate beside it, because the
two answer different questions. Whitespace is normalized away and case
and punctuation are not; all three of those were measured before they
were decided.

The desktop host prints it at the end of an ingestion, and says so when
there is nothing to measure against.

### What CI can prove about it, and what it cannot

The `journal_probe.h` arrangement exactly: `journal_probe_noisy_ocr`
reads the synthetic document correctly and then puts three known
characters on the end of every answer, so the rate the harness must
report is *arithmetic* off the probe's own string lengths rather than a
number somebody ran it once to find. The noise is an **append** for that
reason — a string and the same string with three characters on the end
are exactly three edits apart, no more and no less — where a substitution
has no such guarantee.

That proves the harness counts. It proves nothing about Tesseract, and
does not pretend to.

### The measurement, and what it found

Taken on **the pinned tesseract.js under node**, over the one edition in
the table, against two entries transcribed by hand from the scans. The
desktop's installed engine has still never read a real page (§7), so none
of these numbers are its.

| setting | entry A | entry B |
| --- | --- | --- |
| whole page, single block (what both hosts did) | 12.1% | 21.1% |
| whole page, automatic page segmentation | 2.9% | 4.0% |
| whole page, automatic, drawn 2x | **2.1%** | **1.2%** |
| entry cropped, single block | 2.9% | 2.3% |
| entry cropped 3x, single block | 1.8% | 1.8% |

And over all ninety-nine items, where there is no truth but the engine's
own confidence is a proxy that moved with the rate on every setting
tried:

| setting | characters read | mean word confidence | words under 60 |
| --- | --- | --- | --- |
| single block | 40,040 | 70.4 | 2,190 of 7,696 |
| automatic | 45,197 | 86.1 | 532 of 8,175 |
| automatic, 2x | 45,327 | **90.9** | 254 of 8,174 |

**The whole of it was one page-segmentation mode.** `--psm 6` — one
uniform block of text — is true of a journal *entry* and emphatically not
of the **two-page spread** the `/DCTDecode` path hands over. Told the
page is one block, Tesseract does not look for the four columns on it: it
reads straight across them, so the entry's lines come back interleaved
with the facing page's and the region filter keeps a plausible-looking
wreck. Both whole-page engines now ask for automatic segmentation, and
the decoded path keeps single block because there the image really is one
block. The rule is not that one mode is better; it is that the mode has
to match what is in the picture.

The linked desktop engine (§5) was already right by accident of a
different decision: `SetRectangle` means it hands Tesseract one column of
one entry, so single block is true of what it sees. Its measured
equivalent is the cropped row above.

### What did not help, measured and therefore not shipped

- `user_defined_dpi=300` — **not one character**, on either entry, on
  either path. The engine's own resolution estimate was already fine and
  declaring one changed nothing.
- `preserve_interword_spaces=1` — not one character.
- Reading the region as grey rather than colour — not one character.
- A `--user-words` list of the setting's proper nouns — measured and left
  out. Of 341 words in the two scored entries, exactly two were
  proper-noun misreadings, so a list that fixed both would move the rate
  by six parts in a thousand. It is a fact table this project would then
  own and keep in step with an edition, for that.
- **Marking doubtful words in the text.** Flagging every word under
  sixty picks out 2.6% of the words and 78% of what it picks is genuinely
  wrong — but it catches only 27% of the errors, because three quarters
  of what is left is something the engine is confident about (an
  apostrophe read as a double quote, a lower-case `k` read as a capital).
  A mark that finds a quarter of the mistakes while putting noise in
  front of a reader is a bad trade. The confidence is kept as a
  **per-entry score** instead, which is what tells a player which of their
  ninety-nine entries to look at.

### The one asymmetry, stated rather than hidden

Drawing the page bigger is worth another halving of the rate, and it
needs the page **decoded**. That is not the same lever on the two hosts:

- **The browser can pull it and does.** `createImageBitmap` and a canvas
  are the browser drawing a JPEG it already knows how to draw — the same
  thing it does when the plain Blob path hands tesseract.js the stream.
  #212 refused to put a decoder in *this project*, and that stands.
- **The desktop's program-driven engine cannot.** It has no decoder
  within reach at all: Tesseract's CLI has no crop or scale flag and this
  host is not growing an image library. Its decoded path *could* upscale
  the PGM it writes and does not, because the only edition in the table is
  `/DCTDecode` — it would be code no shipped edition executes.

So a browser ingestion of this edition now reads measurably better than a
desktop one, and the honest way to close that is #216's linked engine
rather than a decoder here.

While it was in there: the browser was recognizing **every scan once per
entry on it** — 120 recognitions of 11 pages. It keeps one page now, which
is what pays for the upscale (11 readings at 4.1 seconds against 120 at
2.9).

### What is still owed

- **Nobody has run this against the desktop's installed Tesseract**, and
  its error profile will not be tesseract.js's. §7's standing gap.
- The commonest surviving error is not a misreading of a word at all.
  At the best measured setting the two scored entries have twenty-seven
  word errors left between them, and **nine of them are the opening
  single quote** that starts each of the book's paragraphs, read as a
  curly double quote or a `*` — characters the in-game reader's font
  cannot draw anyway. Worth a look, and not looked at here: it is a
  transcription decision about a player's document rather than an engine
  setting, and it should be measured like everything else in this section
  before it is made.
- **Eight more of those twenty-seven** are the scan's own hyphenated line
  breaks, which is #316 and is filed separately. De-hyphenating at
  ingestion would fix all eight and would also give the engine's
  dictionary a whole word to work with on the next line.
- #315's own diagnosis had one thing wrong, and it is worth recording
  because it is what measuring bought: the garbage line at the top of the
  entry it quotes is not "an illuminated heading no engine will ever
  read". It is the entry's own opening line, in the same script as the
  rest, read badly because the whole page was being read badly. Dropping
  it by position, as the issue proposed, would have deleted real text
  from every entry in the book.

## 6. The store

One file of UTF-8 lines, each text length-prefixed:

```
amberfolio-journal 2
edition <64 hex>
engine tesseract 5.5.1
scanned entry 12 431
<431 bytes><newline>
corrected entry 12 438
<438 bytes><newline>
```

The word after the keyword is the section (§3): a number alone names
three different texts, and `scanned tale 4` says what `scanned 1 4` does
not — which matters here, because this file is meant to be opened and
edited by a person. A **version 1** store had no such word, and is read
as a store of journal entries and written back as version 2.

Two texts per item, and only one of them is ever overwritten. Ingestion
replaces `scanned` and never touches `corrected`; a reader shows the
correction where there is one. That is #174's "a player can fix an OCR
error and the fix survives re-ingestion", and it is the reason there are
two fields rather than one — a single text cannot tell "the player fixed
this" from "the engine happened to get it right", so re-ingesting with a
better engine would either destroy every correction or keep every
mistake.

Length-prefixed so that a transcription containing a line beginning
`scanned entry 3 4` cannot be read as a header. Strict on the way in: a file
that is not exactly this is refused whole, never half-read — a player's
transcription with a hole in it is the one outcome nothing downstream
could detect. Line endings are the one thing it is not strict about: CRLF
is normalized first, so a store that has been through an editor on
Windows still reads.

A store of a *different* edition is cleared rather than merged. Entry 12
of one printing is not entry 12 of another.

Where it lives is a host's business, because files are (PLAN.md §4). The
desktop writes the file above. **The browser writes the same text into
its own `localStorage` and reads it back when the page next loads**
(M5-E3f) — the module still opens nothing, because serializing is all a
store does and the page is the host that decides where the bytes rest.
An ingestion of a real edition is fifty-eight entries through a wasm OCR
engine, which is minutes; asking for that on every visit was the one part
of this pipeline a player would have felt every day.

`localStorage` rather than IndexedDB, and that is a decision rather than
a placeholder for M6's. What M6 owes a browser is the player's *disk* —
megabytes of binary, kept between visits, which genuinely needs a
database. This is one string of a few tens of kilobytes, wanted
synchronously at the instant the module comes up and before anything can
look at the store. A key-value drawer is the right size for it.

One slot, not one per edition: the module holds one store and clears it
when a document of another printing is ingested, so a second slot could
only ever hold a store the first would refuse to mix with. A player who
alternates between two editions re-reads the second one; a player with
one journal — everyone this is for — never OCRs twice.

Nothing about it is quiet. A drawer that is full, a browser that refuses
one (a private window, blocked site data), or a store from a format this
build cannot read: each is a sentence on the page rather than a silence,
and a store this build could not read back is **left where it is** — a
build that cannot read somebody's corrections has no business being the
thing that deletes them. The page carries a *Forget it* button, which
empties both the drawer and the tab's own copy, because a reset that took
effect only after a reload would be the page claiming to have forgotten
something it was still showing the reader.

The read log — the `seen` lines — is kept too, and was not until #237.
A store holds two things and only one of them used to arrive: the text
reaches the reader through the host-service pointer, and the log lives in
`machine::journal_state`, where it is observation. The desktop put it
there and the browser had no ABI to, so a terminal restored a player's
`*` marks across runs and a browser silently did not. That was a gap
rather than a decision, and it was one call:
`af_web_journal_seen_restore`.

The ordering it has to get right is easy to get wrong in a way nothing
notices — the store holds the log newest first and so does the machine,
and `note_seen` puts each row on the *front*, so feeding them in stored
order hands the reader its own list upside down. It was written twice and
then only one of the two was written at all, so it is
`host::restore_journal_log()` in `hosts/common` now, with both hosts
calling it and `JournalLogRestore` holding it down.

The only thing that may be written down about a store, anywhere, is how
many entries it has and its SHA-256 — `journal_store::fingerprint()`
exists so a maintainer can report an ingestion of their own document on
#174 without reporting a word of it.

## 7. What is checked, and what is not

**In CI, on every target.** The extractor, the store and the whole
ingestion, over `journal_probe.h`'s synthetic document: a real, small,
byte-deterministic PDF this project generates, with three image XObjects
in it chosen for what they exercise — eight-bit gray with no predictor,
one-bit inverted with a different PNG row filter on every row, and a
`/DCTDecode` page that goes through undecoded (§4a). The fact table for it
is what the generator *measured while generating*, which is what gathering
a real edition's facts looks like minus the generator.

The third one is a **real baseline JPEG this project encodes** — a flat
field of one gray, whole 8x8 blocks, a flat quantization table and two
Huffman tables of two codes and one. Flat on purpose: nothing in this
build decodes it, so what it has to be is a well-formed image of the right
shape arriving at the engine byte for byte, and a JPEG with words painted
into it would not move the boundary below, because the fixture answers by
fiat either way.

The engine in those checks is a fixture that answers for exactly one
image per entry, compared against a bitmap generated from the same
description the document was generated from. It is not a stub that says
yes: a store with its words in it is evidence that the offset, the
filter, the predictor and the crop were all right.

Three levels of it: the C++ suite (`hosts/common/tests/journal_*_test.cpp`),
the desktop host end to end over real files on a real disk
(`hosts/sdl/cmake/run-journal.cmake`, which also checks that a correction
written into the store by hand survives a second ingestion), and the wasm
module through the ABI with the loop held by JavaScript
(`hosts/web/tests/smoke.mjs`), which is the only thing that can settle
whether the inverted loop a browser needs actually works.

**Not checked anywhere, and named rather than implied:**

- **A real edition has now been ingested with a real engine**, which is
  the line that used to say the opposite. `--journal` over the archive
  release's own journal, against a build with `AMBERFOLIO_LINK_TESSERACT`
  on: `entries=58 extracted=58 recognized=58`, fifty seconds, 36,865
  characters. Fifty-seven of the fifty-eight come back beginning with
  their own printed heading, which is a self-check on every rectangle in
  the table; the one that does not is the engine reading a printed `57`
  as `37`, and the rectangle is right.

  What is still not covered *in CI* is any of that: the engine is off by
  default, no runner has the document, and neither will change. This is a
  thing a maintainer does on their own machine and reports, the way
  §8 says.
- **No real OCR engine has been run by CI.** Neither host's engine is
  exercised by any test: the desktop's needs Tesseract installed, the
  browser's needs 32 MiB of fetched wasm and a browser to run it in.
  What the tests cover is everything up to the engine and everything
  after it. Tesseract's own correctness is Tesseract's business; what is
  untested here is the *plumbing* to it — the PGM this host writes and
  the command it runs, and the three same-origin paths the page hands
  tesseract.js.
- **A real citation has now been seen by the reader** (#232), and it
  cost the seam two facts to learn: the watch was on a routine that draws
  no narration, and the shape it wanted was not the shape the game
  writes. Both are `docs/seams.md` §10. What is still only checked
  against strings the suite writes is every *other* citation this game
  has: the one driven is the city hall's four proclamations, and the
  entry and tale forms are the pattern's word rather than a measured
  sentence.
- **Somebody has now opened a browser on the journal panel of the dev
  page**, with a real journal, and it read nothing (#306): the page read
  the engine's answer in the shape of a tesseract.js two majors older
  than the one pinned, and the smoke test had checked the filter against
  that same invented shape. §4a carries what was found and what the
  fixed page reads. What is still only a person's to do is #236's rest —
  the desktop's installed engine on a real document, and looking at what
  the browser read rather than counting it.
- **Huffman-coded streams are not exercised by our own fixtures.** The
  probe's Flate streams are stored deflate blocks, because nothing in this
  tree compresses anything. That is libdeflate's business and it is tested
  against the world's compressors, which is why it is used
  (`cmake/AmberfolioLibdeflate.cmake`).
- **The browser's engine has read real JPEG pages** (§4a, #306) and the
  desktop's installed one has not. What CI proves about the passthrough is
  that the right stream reaches the engine unaltered with the right
  rectangle; what only a person with a document and an installed engine
  can prove is that Tesseract reads words off it and that the rectangle
  picks out the entry. The region filter itself is checked on both hosts
  against word boxes the tests write — in the pinned engine's own shape on
  the page, since #306, because a shape the test invents is a test of the
  test.
- **How *well* it reads is measured now, and CI still cannot measure it**
  (§5a, #315). What runs everywhere is the harness — over a synthetic
  document, with a fixture engine that misreads it by an amount this
  project chose, so the rate the harness must report is arithmetic. What
  a real engine does to a real page was measured by hand, once, on
  tesseract.js under node, against two entries transcribed off the scans
  by a person; the numbers are in §5a and the transcriptions are not here
  and never will be. Nobody has taken the same measurement against the
  desktop's installed engine.

## 8. Reporting an ingestion

If you hold an edition and have ingested it, what may go on #174 is: the
edition's name, its SHA-256, how many entries the fact table has, how
many were extracted, how many were recognized, the engine's version
string, and `journal_store::fingerprint()`. The desktop host prints every
one of those on its own lines.

Since #315, also: the **numbers** — the mean word confidence, how many
words were under the threshold, and the character and word error rates
against your own corrections. Those are measurements of an artifact and
not the artifact, in exactly the sense a fingerprint is, and they are the
only way a change to the engine can be argued about at all.

Not: any text, any excerpt, any screenshot of an entry, any file. Ever.
An error rate is a number; the two strings it was taken over are your
document and stay on your machine.

## 9. The reader, and the one door between the two halves

The in-game reader is M5-E4 (#175) and is a **seam**, so what it is and
what it refuses is `docs/seams.md` §10's business rather than this
document's. What belongs here is the join.

**One host service.** The seam calls `journal_open` with a *citation* —
a section and a number, packed into the one word the callout has always
carried (§3) — and a host's `serve()` looks it up in the store above and
answers. A word that does not decode to a citation this build knows is
refused exactly like a number that names nothing, because that is what it
is. There are
four answers and each is a different thing for a player to do about it:
the text, "nobody has read a journal", "this journal has no such entry",
and "that entry is there and the engine read nothing off it". The last
two are the same distinction `journal_trouble` makes one layer down, kept
rather than collapsed, because they are fixed by different things.

**And one that carries nothing.** `journal_seen` (M5-E4b, #222) says the
journal's log has moved — the game cited something, or the player opened
something it had cited. What changed is in `machine::journal()`'s own log,
which is observation and not machine state, so the service copies it into
the store and a host writes that out. `automap_update` is the same shape
for the same reason.

**A correction is what the reader gets**, which is the whole reason the
store keeps two texts per entry (§6). Nothing about the reader knows that
a text was corrected, and nothing should.

**A page is drawn in two sizes, and where it was opened decides which**
(M5-E4d, #305). The `Notes` listing is a full screen, and picking a row
off it used to drop back to a page in the roster-sized panel — 264
characters where the screen it had just filled holds 760. It is the same
screen now: the same box, drawn by the same two of the program's own
routines. The rule is not "which key opened it" but a fact about the
machine — is the party's own command-bar routine the thing running? —
because that is the precondition under which the program's screen
composer may be asked to put the screen back (`docs/seams.md` §10, and
M5-E2d for what asking it elsewhere cost). So a row of the listing and
the F1 prompt on the adventuring screen open a full screen; F1 at camp
or with a vendor's bar up opens the panel; and a **citation** opens the
panel always, because it fires inside a script's own narration where an
NPC can be in the viewport. The word wrap did not change; it only got
wider.

**What arrives is what the panel can draw** (M5-E4c, #219). The panel maps
a *byte* to a glyph, out of the program's table of sixty-four; a store is
UTF-8 and an OCR engine produces plenty of it. A real ingestion of the one
tabled edition carries 229 non-ASCII characters and 222 of them are
quotation marks, so an entry beginning with a curly quote used to begin
with three pieces of furniture.

So a page is made drawable on the way into the delivery buffer, one glyph
per code point: the quotation marks and dashes become their plain
equivalents, an ellipsis becomes three stops, and everything else — a
character with no glyph, or a byte that is not valid UTF-8 — becomes one
visible substitute rather than vanishing. Doing it there rather than at the
drawing step is what also fixes the wrapping, which counts bytes, and stops
a page longer than the buffer being cut in half through a character.

**The store is not touched.** A player's transcription is theirs, it is
UTF-8, and somebody editing that file by hand should be able to type a
curly quote into it. What changes is only what the panel is handed.

**The answer comes back in a buffer, not a return value.** `serve()`
answers `void`, so what a host found goes into `machine::journal()` —
core's own observation buffer, not machine state, dropped by `reset()`
and absent from the state hash (`machine/journal.h`, `docs/seams.md` §3).
What crosses is capped at four kilobytes, which is about sixteen screens
of the roster panel and five of the full screen; a longer entry is
delivered truncated
and the reader says so, because a transcription with a silent hole in it
is the failure a player finds out about last.

**A page reaches the store through `Machine` now** (M5-C1, #229). Five
methods — `journalStoreWrite`, `journalStoreRead`, `journalStoreStats`,
`journalStoreChanged`, `journalStoreClearChanged` — delegate to
`page/journal.mjs`, which stays the implementation. What moved is the
door and not the code: everything else a save layer needs was already a
`Machine` method and the store was the exception, so a page had to import
a second file and reach past the façade for it. The ingestion itself
stays where it is, because it is asynchronous and page-shaped and is not
a thing to wrap.

The pair worth reading the rule for is `journalStoreChanged()` /
`journalStoreClearChanged()`, over `host::journal_store`'s own flag.
**Every write raises it** — a scan recorded, a correction, the edition or
the engine, a clear, a citation logged — and `journalStoreRead()` is the
one that does not, because a store read in came from the caller and the
caller already holds those bytes. **The lowering is the caller's**: a
store cannot know whether a drawer or a disk accepted the bytes, and a
flag that cleared itself on read would lose a correction made between the
read and the write. So: read the flag, write the store, *then* lower it.

**Both hosts hand over the same store.** The desktop's lives for the run
and is read at its start, so a player who ingested last week starts today
with `--seam journal` able to answer; the browser's is the tab's, filled
by the page's own file input — or, for a script, by `drive.mjs`'s
`--journal-store`, which reads a store file and hands the module its
bytes. Neither host writes to it from inside the game: a correction is a
page's or an editor's, and the reader only reads.

**What it has and has not been driven against** is in `docs/seams.md`
§10, and the short version belongs here too: the recognizer, the reader
and the service are checked in CI on all four targets, and **a real
journal has now been opened at a real citation** (#232) — a player's own
ninety-nine entries, ingested against an installed Tesseract, opened by
the game's own words at the city hall with nobody having pressed a key.
What has still not been driven is a citation of an *entry* or a *tale*:
the one the game gave up was four proclamations, and the other two
sections' shapes are the pattern's word rather than a measured
sentence.

## 10. The cheat that cites everything (#301)

**What was wrong.** A store is text an OCR engine produced off a scan,
and the only way to look at what the engine actually read was to wait
for the game to cite an entry, or to type its number at the F1 prompt —
ninety-nine times, section by section, for the one edition anybody has
ingested. The `Notes` listing (M5-E4b, #222) is the cheapest possible
proof-reading surface, newest first with a `*` on what has not been
opened, and it lacked only a way to fill it.

**What changed.** A debug cheat fills it: `--cite-all-journal` on the
desktop host and the *Cite them all (cheat)* button on the dev page's
journal panel, both `host::cite_all_journal()` beside
`restore_journal_log` in `hosts/common`. It walks the store **backwards**
— the store is sorted by section and then number, and `note_seen` puts
each row on the front — so the listing reads Entry 1 first, then the
tales and the proclamations in their own order, every row unread and
every row stamped with the machine's own seeded wall clock at that
instant, the way the seam stamps a real citation. It **clears nothing**:
`note_seen`'s move-up rule keeps a read flag on a row already there, so
a second call neither doubles a row nor unreads one, and anything the
game cited that is not in the store stays underneath. Then it writes the
machine's log into the store through `set_seen` — the `journal_seen`
service's own write — so the rows go to the file, or the drawer, the way
a real citation's do, and **survive every later run until the `seen`
lines are removed from the store or the page's *Forget it* empties it**.
Both hosts say so where they offer it. With no journal ingested it cites
nothing, touches nothing and says so; the reader's own "you have not
ingested a journal" is that player's answer.

**Where it lives, and why it is not a seam.** The log is observation
(`machine/journal.h`'s three terms — dropped by `reset()`, absent from
the state hash, host-writable), and the store it reads is the host's:
core never enumerates a store, it asks for one entry at a time. So this
is a host action like `--forget-code-wheel` and *Ask me again* — nothing
under `core/` moves, no host service is added, the ABI is at 1.2 where
`v0.4.0` left it, and `af_web_journal_cite_all` is a page export beside
`af_web_journal_seen_restore` and not an entry point in `abi.h`. It
contradicts `journal.h`'s "a log, not an index" on purpose, the way
`cheat-wound-party` contradicts the game's damage rules on purpose
(PLAN.md §5 item 6), and it is off unless a person asks; a flag is an act.
The cost of that shape is that it is not in the seam list, so a person
looking there for a cheat will not find it — which is why the flag, the
button and this section all use the word.

**What it cost.** `journal_log_rows` went from 64 to **256**. The cap was
set for play, where sixty-four is more than a game cites in an evening;
citing a ninety-nine-section edition into it dropped the last thirty-five
off the end, which defeats the purpose. The listing is untouched by the
change — it already scrolls a ten-row window over whatever the log holds
(`seam_journal.cpp`), so a longer log is more to scroll and not a
different screen — and what it costs is about two kilobytes of
observation per machine and the same in a store's `seen` lines, none of
it in the state hash. `JournalCiteAll.AWholeEditionFitsInTheLog` holds a
store of that edition's shape, with none of its words, and asserts all
ninety-nine rows arrive.

**What evidence it rests on.** `JournalCiteAll.*` in
`hosts/common/tests/journal_store_test.cpp`, over the probe edition
(`journal_probe.h`: three entries, the third in two pieces, and the tale
numbered one): four rows, Entry 1 first, all unread, one stamp, the
store's log equal to the machine's and round-tripping as `seen` lines, a
second call harmless, an empty store untouched. Step 7 of
`hosts/sdl/cmake/run-journal.cmake` drives the flag on the desktop host
against a real store file and reads the four `seen` lines back in order,
twice; `tests/smoke.mjs` drives the page export the same way on the wasm
module. Nothing here runs the game, and the fidelity invariant is
unaffected: a run with the flag and every seam off is the run without it.

**What a person still owes.** The look itself — every entry opened on
the game's own screen, off `Notes`, and read against the scan — which is
what this exists for and what no runner can do. It belongs on the list
§7 keeps: a real engine, a real page, a real display. And on the dev page
specifically: the button has been driven under node and never pressed in
a browser, which is #236's standing state for the whole panel.
