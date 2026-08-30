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
Tesseract through its `tsv` output, tesseract.js through
`data.words[].bbox` — so the filter reads a number they were producing
anyway. A word counts as inside when its centre is, which gives the same
answer a crop would for every word a crop would not have cut in half.

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
decoded, nothing is recognized, and both numbers are reported.

**The deployed page ships without an engine.** Nothing in CI runs the
fetch script, so https://amberfolio.vercel.app locates and decodes a
recognized journal's entries and recognizes none of them, and says so —
32 MiB of wasm on every deploy is a packaging decision and packaging is
M6's. A local build that has run the script has the whole thing.

The fetch script pins versions and **does not pin digests**, because this
repository may not carry a fingerprint nobody has computed. It trusts on
first use and pins afterwards: the first run writes `sha256sums.txt`
beside what it fetched and every later run verifies against it, refusing
on a mismatch until somebody has looked.

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
desktop writes the file above. **The browser keeps it in memory for the
life of the tab, and the page says so**: IndexedDB is M6's, and
pretending otherwise would be the one kind of lie a player finds out
about by losing work. The module can serialize its store, so M6's
persistence is two lines rather than a format decision.

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
- **No real citation has been seen by the reader.** The recognizer is
  checked against strings the suite writes and the reader against a
  synthetic store, on all four targets; what nobody has checked is that
  this program's own citations have the shape the recognizer expects,
  because that needs an edition and the table is empty. It is the first
  thing to look at when somebody has one (`docs/seams.md` §10).
- **Nobody has opened a browser on the journal panel of the dev page.**
  It is the same open state #147 records for the rest of the page.
- **Huffman-coded streams are not exercised by our own fixtures.** The
  probe's Flate streams are stored deflate blocks, because nothing in this
  tree compresses anything. That is libdeflate's business and it is tested
  against the world's compressors, which is why it is used
  (`cmake/AmberfolioLibdeflate.cmake`).
- **No engine has read a real JPEG page** (§4a). What CI proves about the
  passthrough is that the right stream reaches the engine unaltered with
  the right rectangle; what only a person with a document and an installed
  engine can prove is that Tesseract reads words off it and that the
  rectangle picks out the entry. The region filter itself is checked on
  both hosts against word boxes the tests write.

## 8. Reporting an ingestion

If you hold an edition and have ingested it, what may go on #174 is: the
edition's name, its SHA-256, how many entries the fact table has, how
many were extracted, how many were recognized, the engine's version
string, and `journal_store::fingerprint()`. The desktop host prints every
one of those on its own lines.

Not: any text, any excerpt, any screenshot of an entry, any file. Ever.

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

**A correction is what the reader gets**, which is the whole reason the
store keeps two texts per entry (§6). Nothing about the reader knows that
a text was corrected, and nothing should.

**The answer comes back in a buffer, not a return value.** `serve()`
answers `void`, so what a host found goes into `machine::journal()` —
core's own observation buffer, not machine state, dropped by `reset()`
and absent from the state hash (`machine/journal.h`, `docs/seams.md` §3).
What crosses is capped at four kilobytes, which is about sixteen screens
of the panel the reader draws in; a longer entry is delivered truncated
and the reader says so, because a transcription with a silent hole in it
is the failure a player finds out about last.

**Both hosts hand over the same store.** The desktop's lives for the run
and is read at its start, so a player who ingested last week starts today
with `--seam journal` able to answer; the browser's is the tab's, filled
by the page's own file input — or, for a script, by `drive.mjs`'s
`--journal-store`, which reads a store file and hands the module its
bytes. Neither host writes to it from inside the game: a correction is a
page's or an editor's, and the reader only reads.

**What it has and has not been driven against** is in `docs/seams.md`
§10, and the short version belongs here too: the recognizer, the reader
and the service are checked in CI on all four targets, and the reader has
been driven against the real program off a store written by hand — but
**nobody has opened a real journal at a real citation**, because that
needs an edition and the table is empty (§3).
