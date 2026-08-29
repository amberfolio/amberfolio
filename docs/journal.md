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
| The fact table | `hosts/common/.../journal_facts.h` | Per edition, per entry: the page, the stream's offset and length, the image's shape, the region that is the entry. |
| The extractor | `hosts/common/.../journal_extract.h` | Follows an offset, inflates the stream, undoes the predictor, expands samples to gray, crops the region. |
| The engine | `hosts/common/.../journal_ocr.h` | One virtual call. The desktop runs the player's own Tesseract; the browser drives tesseract.js. |
| The store | `hosts/common/.../journal_store.h` | Entry number to text, with what the engine read and what a person corrected kept apart. |
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

Every real journal is an unrecognized edition today. `known_journals()`
is empty and `known_documents()` has no journal line, for the reason
`machine/document.cpp` already gives about its own table: a fingerprint
is a fact about a file somebody actually hashed, and nobody here has
hashed that one. The offsets are worse — they are facts about a document
somebody has to sit down with.

So adding an edition is adding data, in two places:

1. **The gate**, in `core/src/machine/document.cpp`: the SHA-256 of the
   whole file, a name a player would recognize, and
   `document_kind::journal`.
2. **The insides**, in `hosts/common/src/journal_facts.cpp`: the same
   fingerprint, and one `journal_entry_fact` per entry — the entry
   number the game itself uses, the page it is on, the byte offset of the
   stream's first data byte, its `/Length`, the image dictionary's
   `/Width`, `/Height`, `/BitsPerComponent`, component count, filter and
   `/DecodeParms /Predictor`, whether it is `/Decode [1 0]`, and the
   rectangle of the decoded image that is the entry.

The suite checks the two against each other, so an edition in one and not
the other fails in CI rather than on a player's machine. It also checks
every row's shape: a region inside its image, no two rows for one entry,
and a filter this build can actually decode.

`--journal` on a document whose edition is not in the table prints its
fingerprint, which is the first half of the row somebody has to write.

## 4. What the extractor decodes, and what it refuses

Implemented: `/FlateDecode` and unfiltered streams; 1 and 8 bits per
component; one component (gray or bilevel) and three (RGB, converted to
gray by the standard luma weights); PNG predictors 10–15, all five row
filters; `/Decode [1 0]` and `/ImageMask`.

Refused **by name**, which is "log, don't fake" one level up from a
service: `/DCTDecode`, `/CCITTFaxDecode`, `/JBIG2Decode`, TIFF's
predictor 2, and any bit depth or component count not listed above. None
of them was built on spec. A scanned journal may well turn out to be
JPEG or group-4 fax, and the day somebody has a document that says so is
the day that code gets written — with the document in front of them. What
a `/DCTDecode` edition would want is not a decoder: it is passing the
stream's own bytes through to the engine, which both Tesseract and
tesseract.js read directly, with the region becoming the engine's
business rather than the extractor's.

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

**Desktop: the player's own Tesseract, run as a program.** Not linked,
not vendored, not fetched by the build. Tesseract is a large C++
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
amberfolio-journal 1
edition <64 hex>
engine tesseract 5.5.1
scanned 12 431
<431 bytes><newline>
corrected 12 438
<438 bytes><newline>
```

Two texts per entry, and only one of them is ever overwritten. Ingestion
replaces `scanned` and never touches `corrected`; a reader shows the
correction where there is one. That is #174's "a player can fix an OCR
error and the fix survives re-ingestion", and it is the reason there are
two fields rather than one — a single text cannot tell "the player fixed
this" from "the engine happened to get it right", so re-ingesting with a
better engine would either destroy every correction or keep every
mistake.

Length-prefixed so that a transcription containing a line beginning
`scanned 3 4` cannot be read as a header. Strict on the way in: a file
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
byte-deterministic PDF this project generates, with two image XObjects in
it chosen for what they exercise — eight-bit gray with no predictor, and
one-bit inverted with a different PNG row filter on every row. The fact
table for it is what the generator *measured while generating*, which is
what gathering a real edition's facts looks like minus the generator.

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

- **No real edition has been ingested.** The fact table is empty. Until
  somebody sits down with a document, every real journal takes the
  unrecognized path, which is the fail-closed direction and is the whole
  of what this build claims.
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
  probe's streams are stored deflate blocks, because nothing in this tree
  compresses anything. That is libdeflate's business and it is tested
  against the world's compressors, which is why it is used
  (`cmake/AmberfolioLibdeflate.cmake`).

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

**One host service.** The seam calls `journal_open` with an entry number;
a host's `serve()` looks it up in the store above and answers. There are
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
