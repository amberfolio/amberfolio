// SPDX-License-Identifier: AGPL-3.0-only

#include "journal_host.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_ingest.h"
#include "amberfolio/host/journal_ocr.h"
#include "amberfolio/host/journal_probe.h"
#include "amberfolio/host/journal_score.h"
#include "amberfolio/host/journal_store.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/sha256.h"
#include "document_control.h"
#include "ocr_discovery.h"
#include "options.h"
#include "tesseract_ocr.h"
#include "user_files.h"
#if AMBERFOLIO_HAVE_LINKED_TESSERACT
#include "tesseract_linked_ocr.h"
#endif

namespace amberfolio::sdl {

namespace {

#if AMBERFOLIO_HAVE_LINKED_TESSERACT
/// Where a linked build looks for `eng.traineddata`.
///
/// Beside the executable first, because that is where a packaged build
/// would put it and a player's copy must not depend on a path from the
/// machine it was built on; then the build tree's own fetched copy, which
/// is what a developer has and what CMake compiled in.
[[nodiscard]] std::string linked_tessdata_path() {
  std::error_code why;
  const char* base = SDL_GetBasePath();
  if (base != nullptr) {
    std::filesystem::path beside = std::filesystem::path(base) / "tessdata";
    if (std::filesystem::exists(beside / "eng.traineddata", why)) {
      return beside.string();
    }
  }
  return AMBERFOLIO_TESSDATA_DIR;
}
#endif

/// The engine this run reads with, when nobody named one (#382).
///
/// Beside the binary, then each directory of `PATH`, and then a report
/// of what was looked for -- `ocr_discovery.h` has the order and the
/// argument for it. Empty when nothing was found, in which case the
/// report has already been printed and the caller says "no engine" in
/// its own words.
[[nodiscard]] std::string discover_journal_ocr() {
  const char* base = SDL_GetBasePath();
#ifdef _WIN32
  constexpr char separator = ';';
#else
  constexpr char separator = ':';
#endif
  // SDL's own reader, not `std::getenv`: it is the thread-safe one on
  // every platform this ships to, and MSVC deprecates the other outright.
  const char* path_variable = SDL_getenv("PATH");
  const std::vector<std::string> places = sdl::ocr_candidates(
      base == nullptr ? std::string_view{} : std::string_view(base),
      path_variable == nullptr ? std::string_view{}
                               : std::string_view(path_variable),
      separator, sdl::ocr_engine_filename);
  std::error_code why;
  for (const std::string& one : places) {
    if (std::filesystem::is_regular_file(one, why)) {
      std::fprintf(stderr, "amberfolio: journal-ocr discovered %s\n",
                   one.c_str());
      return one;
    }
  }
  // The third thing, and not the absence of the first two. A player who
  // is told "no engine" and nothing else has no next move; one who is
  // told the name and the places has several.
  std::fprintf(stderr,
               "amberfolio: journal-ocr not found - looked for %.*s in %zu"
               " place(s)\n",
               static_cast<int>(sdl::ocr_engine_filename.size()),
               sdl::ocr_engine_filename.data(), places.size());
  constexpr std::size_t listed = 12;
  for (std::size_t i = 0; i < places.size() && i < listed; ++i) {
    std::fprintf(stderr, "amberfolio: journal-ocr looked %s\n",
                 places[i].c_str());
  }
  if (places.size() > listed) {
    std::fprintf(stderr, "amberfolio: journal-ocr looked ... and %zu more\n",
                 places.size() - listed);
  }
  return {};
}

}  // namespace

std::vector<std::string> present_digest(machine::machine& box,
                                        const sha256_digest& digest) {
  // The two outcomes, and the words for them, are `document_control.h`'s:
  // the page says them too, and a sentence built here would be a sentence
  // no test can read.
  const std::vector<std::string> lines =
      sdl::document_lines(sdl::present_document_to(box.seams(), digest));
  for (const std::string& line : lines) {
    std::fprintf(stderr, "amberfolio: %s\n", line.c_str());
  }
  return lines;
}

std::vector<std::string> present_document(machine::machine& box,
                                          const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::string trouble = "document " + path + " could not be read";
    std::fprintf(stderr, "amberfolio: %s\n", trouble.c_str());
    return {std::move(trouble)};
  }
  sha256_hasher hasher;
  std::array<char, std::size_t{64} * 1024> buffer{};
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize got = file.gcount();
    if (got <= 0) {
      break;
    }
    hasher.update(std::span<const std::uint8_t>(
        // The one place this host looks at a document's bytes, and it
        // hands every one of them straight to the hasher.
        reinterpret_cast<const std::uint8_t*>(buffer.data()),
        static_cast<std::size_t>(got)));
  }
  return present_digest(box, hasher.finish());
}

void ingest_journal(machine::machine& box, const options& opts,
                    host::journal_store& store) {
  std::ifstream file(opts.journal, std::ios::binary);
  if (!file) {
    std::fprintf(stderr, "amberfolio: journal %s could not be read\n",
                 opts.journal.c_str());
    return;
  }
  const std::string bytes((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
  const std::span<const std::uint8_t> document(
      reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());

  host::journal_ingester ingester(opts.journal_probe
                                      ? host::journal_probe_table()
                                      : host::known_journals());
  const host::journal_trouble opened = ingester.begin(document);

  // The same sentence `--document` would have printed, off the digest
  // this already computed: a journal is a document, and presenting it is
  // what satisfies a journal-gated seam.
  static_cast<void>(present_digest(box, ingester.fingerprint()));

  if (opened != host::journal_trouble::none) {
    std::fprintf(stderr, "amberfolio: journal unrecognized sha256=%s - %s\n",
                 ingester.fingerprint_hex().c_str(),
                 host::journal_trouble_name(opened));
    return;
  }
  std::fprintf(stderr, "amberfolio: journal %.*s entries=%zu\n",
               static_cast<int>(ingester.edition()->name.size()),
               ingester.edition()->name.data(), ingester.entries());
  // An edition typeset as text is read out of the document itself (#398),
  // so there is no engine to look for and nothing to say about one that
  // is missing: the search below would only report a problem this
  // edition does not have.
  const bool own_text = ingester.reads_own_text();
  if (own_text) {
    std::fprintf(stderr,
                 "amberfolio: journal text read from the document itself -"
                 " no OCR engine needed\n");
  }

  // The engine, and what it is. Each way this can go is said out loud:
  // the fixture the probe installs, the engine built into this binary,
  // the player's own installed one, or none — and "none" is a sentence
  // rather than a silence, because a store with no text in it and no
  // explanation is the failure a player finds out about last
  // (`tesseract_ocr.h`).
  //
  // The player's own engine is **discovered** rather than named since
  // #382: an empty `--journal-ocr` means nobody said, and this host goes
  // and looks before it says there is nothing. A build that carries its
  // own engine answers that emptiness first and never searches, because
  // the engine it carries is the one it was built to use.
  host::journal_probe_ocr fixture;
  std::string named = opts.journal_ocr;
#if AMBERFOLIO_HAVE_LINKED_TESSERACT
  const bool carried = true;
#else
  const bool carried = false;
#endif
  if (named.empty() && !opts.journal_probe && !carried && !own_text) {
    named = discover_journal_ocr();
  }
  sdl::tesseract_ocr tesseract(named);
#if AMBERFOLIO_HAVE_LINKED_TESSERACT
  // A build that carries its own engine uses it, because a player who has
  // installed nothing is the reason it was carried (#216). Saying
  // `--journal-ocr PATH` still reaches for a program: a player with a
  // newer engine than the one this was built against should be able to
  // ask for it.
  sdl::tesseract_linked_ocr linked(linked_tessdata_path());
#endif
  host::journal_ocr* engine = nullptr;
  if (own_text) {
    // Nothing to choose: `run()` never asks an engine about this edition.
  } else if (opts.journal_ocr == "none") {
    std::fprintf(stderr,
                 "amberfolio: journal no engine asked for - the entries"
                 " will be read and no text kept\n");
  } else if (opts.journal_probe) {
    engine = &fixture;
#if AMBERFOLIO_HAVE_LINKED_TESSERACT
  } else if (opts.journal_ocr.empty() && linked.available()) {
    engine = &linked;
#endif
  } else if (!named.empty() && tesseract.available()) {
    engine = &tesseract;
  } else if (named.empty()) {
    // Nothing was named and nothing was found; the search above has
    // already said what it looked for and where.
    std::fprintf(stderr,
                 "amberfolio: journal no engine - install one, or say"
                 " --journal-ocr PATH, or --journal-ocr none\n");
  } else {
    std::fprintf(stderr,
                 "amberfolio: journal no engine - '%s' did not answer;"
                 " install it or say --journal-ocr PATH\n",
                 named.c_str());
  }
  if (engine != nullptr) {
    std::fprintf(stderr, "amberfolio: journal engine %.*s\n",
                 static_cast<int>(engine->engine().size()),
                 engine->engine().data());
  }

  // And who turns a page this build does not decode into samples, for
  // the entries that are pictures (#328). Every build has one now
  // (`host/journal_jpeg.h`); this used to be the line where a build that
  // had linked Leptonica for the OCR got pictures and every other build
  // got none (#345). Still printed, because which decoder read a page is
  // a fact about the store that was written.
  if (ingester.page_decoder() != nullptr) {
    std::fprintf(stderr, "amberfolio: journal pages decoded by %s\n",
                 ingester.page_decoder()->name());
  }

  // Where the text goes, and what is already there. Read first, so a
  // correction a player made survives this ingestion — which is the
  // whole reason the store is read at all rather than written fresh.
  const std::string path = journal_store_path(opts);
  if (path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: journal this platform does not say where"
                 " per-user data lives; say --journal-store PATH\n");
    return;
  }
  store.clear();
  if (std::ifstream existing(path, std::ios::binary); existing) {
    const std::string text((std::istreambuf_iterator<char>(existing)),
                           std::istreambuf_iterator<char>());
    if (const host::journal_trouble why = store.parse(text);
        why != host::journal_trouble::none) {
      // Refused rather than overwritten: whatever that file is, it is
      // not this build's, and a player's transcription is not something
      // to write over on a guess.
      std::fprintf(stderr, "amberfolio: journal store %s - %s\n", path.c_str(),
                   host::journal_trouble_name(why));
      return;
    }
  }

  const host::journal_ingest_report report = ingester.run(engine, store);
  std::fprintf(stderr,
               "amberfolio: journal entries=%u extracted=%u recognized=%u\n",
               report.entries, report.extracted, report.recognized);
  // Both numbers, always, and even when the edition has no pictures at
  // all: `pictures=0/0` is a journal of prose and `pictures=0/14` is a
  // build with nothing to decode its pages with, and a single count
  // could not tell a player which they had (#328).
  std::fprintf(stderr, "amberfolio: journal pictures=%u/%u\n", report.pictures,
               report.art);
  if (report.first_art_trouble != host::journal_trouble::none) {
    std::fprintf(stderr, "amberfolio: journal picture of %s %u: %s\n",
                 machine::journal_kind_name(report.first_art_failure.kind),
                 static_cast<unsigned>(report.first_art_failure.number),
                 host::journal_trouble_name(report.first_art_trouble));
  }
  if (report.first_trouble != host::journal_trouble::none) {
    // The section as well as the number, because three of them number
    // from their own bases and "entry 4" would name three things (#218).
    std::fprintf(stderr, "amberfolio: journal %s %u: %s\n",
                 machine::journal_kind_name(report.first_failure.kind),
                 static_cast<unsigned>(report.first_failure.number),
                 host::journal_trouble_name(report.first_trouble));
  }

  // How well it went, in the two ways there are to know (#315). Both are
  // printed because they answer different questions and neither is
  // available on its own: the engine's own confidence needs nobody to
  // have corrected anything and is only a proxy, and the error rate is
  // the real thing but exists only where a person has already been in.
  if (const host::journal_reading_quality how = report.reading(); how.known) {
    std::fprintf(stderr,
                 "amberfolio: journal read confidence=%.1f doubtful=%zu/%zu"
                 " (%.1f%%)\n",
                 how.confidence, how.doubtful, how.words,
                 how.doubtful_share() * 100.0);
    // The three the engine liked least, because "which of my ninety-nine
    // entries should I look at" is the question after an ingestion and a
    // single average cannot answer it.
    const std::vector<host::journal_item_quality> worst = report.worst_first();
    for (std::size_t i = 0; i < worst.size() && i < 3U; ++i) {
      std::fprintf(stderr,
                   "amberfolio: journal least sure of %s %u:"
                   " confidence=%.1f words=%zu\n",
                   machine::journal_kind_name(worst[i].what.kind),
                   static_cast<unsigned>(worst[i].what.number),
                   worst[i].reading.confidence, worst[i].reading.words);
    }
  }
  if (const host::journal_store_report scored =
          host::score_journal_store(store);
      scored.characters.taken) {
    // Against the player's own corrections, which is the only ground
    // truth this project may ever have (`host/journal_score.h`). The
    // rates and the counts; not a word of what was compared.
    std::fprintf(stderr,
                 "amberfolio: journal score corrected=%zu characters=%.2f%%"
                 " words=%.2f%%\n",
                 scored.items.size(), scored.characters.rate() * 100.0,
                 scored.words.rate() * 100.0);
  } else {
    std::fprintf(stderr,
                 "amberfolio: journal score nothing to measure against -"
                 " correct an entry in the store and re-ingest, and this"
                 " says whether it read better\n");
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const std::string serialized = store.serialize();
  out.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
  out.flush();
  if (!out) {
    std::fprintf(stderr, "amberfolio: journal store %s could not be written\n",
                 path.c_str());
    return;
  }
  // The bytes are on disk, so the flag comes down (M5-C1, #229). It is
  // raised by every write to a store now rather than by the log alone,
  // and an ingestion is a few hundred of them — so without this the run
  // loop below would write this same file again on its first frame.
  store.clear_changed();

  std::array<char, sha256_digest::text_length + 1> hex{};
  static_cast<void>(format_hex(store.fingerprint(), hex));
  // The fingerprint, and not a word of what is in it: a store is a
  // player's own document read off a player's own copy, and a hash names
  // it without carrying any of it (`host/journal_store.h`).
  std::fprintf(stderr,
               "amberfolio: journal store %s entries=%zu corrections=%zu"
               " pictures=%zu sha256=%s\n",
               path.c_str(), store.size(), store.corrections(),
               store.picture_count(), hex.data());
}

void bring_up_journal(machine::machine& box, const options& opts,
                      host::journal_store& journal_text) {
  const bool wants_journal =
      !opts.journal.empty() || !opts.journal_store.empty() ||
      opts.cite_all_journal ||
      std::ranges::find(opts.seams, "journal") != opts.seams.end();
  if (!opts.journal.empty()) {
    ingest_journal(box, opts, journal_text);
  } else if (wants_journal) {
    // Only for a run that asked for the reader, or one that said where a
    // store is. A player who did neither is not owed a line about a file
    // they have no use for, and the seam being named is the one signal
    // available before `enable()` — which happens next, and after this so
    // that the store is there the first time a point can be reached.
    //
    // **And `--journal-store` is the other signal, because of `--replay`**
    // (#235). A replay takes its seams from the recording, which is read
    // further down, so at this point `opts.seams` is empty however many
    // seams the run is about to turn on — and the reader would replay
    // with no text and hash differently than it recorded. Somebody who
    // named a store meant it.
    load_journal_store(opts, journal_text);
  }

  // The read log into the machine the reader draws it from. The log left
  // the per-user store file (#351), so what a version 5 store holds is
  // nothing and what a version 4 one holds is this player's list from
  // before slots; a slot's own list arrives when the program loads it.
  //
  // Into `machine::journal_state`, which is observation there and
  // configuration here, which is why it travels this way round rather
  // than living in either place alone (`machine/journal.h`). Those eight
  // lines were in main.cpp and nowhere else, so the browser did not have
  // them and forgot every `*` on reload (#237); they are
  // `host::restore_journal_log()` now, in `hosts/common`, where both
  // hosts reach them and a test holds the ordering down.
  //
  // **Outside the branch above**, unlike before: a run that ingested a
  // journal goes on to play, and one that restored no log would have the
  // first citation overwrite the list with a list of one.
  host::restore_journal_log(box.journal(), journal_text);
  if (wants_journal) {
    // Said out loud for the reason the store's own line is: a reader that
    // comes up with an empty `Notes` list is either a party nothing has
    // cited or a store that is not being read, and those are not the
    // same thing.
    std::fprintf(stderr, "amberfolio: journal log seen=%zu\n",
                 journal_text.seen().size());
  }
}

void cite_everything(machine::machine& box, host::journal_store& journal_text) {
  // The debug cheat (#301): everything the store holds, onto the log, so
  // `Notes` lists all of it. A host action and not a seam — the log is
  // host-writable and the store is the host's, so nothing in the machine
  // has to know (`host/journal_store.h` has the whole argument). It is
  // also the same write the `journal_seen` service makes, so the loop
  // below keeps it the way it keeps a real citation — for good.
  //
  // **After the seeding above, not before it (#343).** `cite_all_journal`
  // stamps every row with `box.wall().at(box.time())`, read at the
  // instant the cheat fires — and before this moved, that instant was
  // whatever the machine's clock was before a single tick had been asked
  // of it and before `set_wall_time()` had said anything to it at all:
  // the unseeded 1980-01-01 default, never "this host" and never the
  // moment the cheat actually ran. The cheat's own design — one `when`
  // for every row — was never the bug; running it before the clock had
  // an answer was.
  const std::size_t cited = host::cite_all_journal(box, journal_text);
  if (cited == 0) {
    std::fprintf(stderr,
                 "amberfolio: journal nothing to cite - no journal has"
                 " been ingested, so the log is as it was\n");
  } else {
    std::fprintf(stderr,
                 "amberfolio: journal cited all %zu - the Notes log holds"
                 " every entry (log=%zu) until something replaces it\n",
                 cited, box.journal().seen().size());
  }
}
}  // namespace amberfolio::sdl
