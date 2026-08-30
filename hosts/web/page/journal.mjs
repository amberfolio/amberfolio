// SPDX-License-Identifier: AGPL-3.0-only
//
// The browser half of the journal's ingestion (M5-E3, #174).
//
// The module does the reading: it hashes the document, looks the edition
// up, follows each entry's offset, inflates the stream, undoes the
// predictor and crops the region (`hosts/common/.../journal_extract.h`).
// What is left for this file is the part that can only happen in a
// browser — running an OCR engine that is JavaScript — and the loop that
// puts the two together.
//
// DOM-free, like host.mjs and for the same reason: `ctest --preset wasm`
// imports it under node, where there is no `document`. What it does touch
// is `fetch` and `createImageBitmap`, and both are behind a guard,
// because the engine is optional.
//
//
// The engine: pinned, served from here, never a CDN
// ------------------------------------------------
//
// `.tesseract-js-version` pins tesseract.js the way `.emscripten-version`
// pins emsdk. `scripts/fetch-ocr-engine.py` fetches it, its wasm core and
// its language data into the build tree beside this file, and nothing is
// committed — the same arrangement the conformance vectors have.
//
// **The deployed page does not reach a CDN at runtime.** #174 permits it
// only if the page says so, and a page that quietly fetched several
// megabytes of somebody else's JavaScript the moment a player picked a
// file would be doing something a player did not ask for and could not
// see. So the loader looks in one place — `ENGINE_URL`, beside the module
// — and when it is not there it says exactly that, names the version, and
// names the script that fetches it. An ingestion then still runs: every
// entry is decoded, nothing is recognized, and the page reports both
// numbers.
//
//
// Why the loop is inverted here and not on the desktop
// ---------------------------------------------------
//
// The desktop's engine is a program this project runs and waits for, so
// the whole ingestion is one call (`journal_ingester::run`). tesseract.js
// is asynchronous, so the C++ side cannot drive it; this file drives the
// C++ side instead — extract entry *i*, recognize its pixels, hand the
// text back, next — which is the shape `journal_ingest.h` was written for
// and says so.

/// Where the engine is looked for. One place, beside the page's own
/// files, and no fallback: see this file's top comment.
export const ENGINE_URL = './vendor/tesseract/tesseract.min.js';

/// What `scripts/fetch-ocr-engine.py` puts there, so the message a player
/// sees names the thing they have to run.
export const ENGINE_HINT =
  'this build ships without one; a local build can fetch the pinned' +
  ' tesseract.js with scripts/fetch-ocr-engine.py';

/// `journal_trouble::none`. Every journal call in the module answers one
/// of that enum's values rather than an `AF_*` code, because each of the
/// dozen reasons is a different sentence a player needs (hosts/web/src/
/// main.cpp says so); zero is the only one that is not a problem.
export const JOURNAL_OK = 0;

/// Read one of the module's "write a NUL-terminated string into my
/// buffer" answers. The same two-call shape host.mjs's own `#text` uses:
/// ask with null for the length, allocate, ask again.
function readText(module, call) {
  const length = call(0, 0);
  if (length === 0) return '';
  const scratch = module._malloc(length + 1);
  if (scratch === 0) throw new Error('out of wasm heap while reading a string');
  try {
    const written = call(scratch, length + 1);
    const bytes = module.HEAPU8.subarray(scratch, scratch + written);
    // UTF-8: unlike every other string that crosses this boundary, this
    // one is a transcription of a printed page and may carry anything
    // the engine read.
    return new TextDecoder().decode(bytes);
  } finally {
    module._free(scratch);
  }
}

/// Put `text` into linear memory as NUL-terminated UTF-8 and call `use`.
function withUtf8(module, text, use) {
  const bytes = new TextEncoder().encode(String(text ?? ''));
  const scratch = module._malloc(bytes.length + 1);
  if (scratch === 0) throw new Error('out of wasm heap while passing a string');
  try {
    module.HEAPU8.set(bytes, scratch);
    module.HEAPU8[scratch + bytes.length] = 0;
    return use(scratch, bytes.length);
  } finally {
    module._free(scratch);
  }
}

/// The module's own words for a trouble code, so a browser and a terminal
/// describe one finding identically.
export function troubleName(module, code) {
  return readText(module, (out, cap) =>
    module._af_web_journal_trouble_name(code, out, cap),
  );
}

/// The bitmap the last `extract` produced, as an `ImageData` an OCR
/// engine will take.
///
/// The view is re-derived on every call and never kept: a `_malloc`
/// anywhere may have grown linear memory, which detaches every existing
/// typed array (abi.h's own warning about the framebuffer). And it is
/// copied out rather than viewed, for the same reason plus one more —
/// the module reuses one buffer per entry, so a view would show the next
/// page's pixels by the time an asynchronous engine got round to reading
/// it.
///
/// A real `ImageData` where there is one, and the same three fields
/// otherwise: node has no such constructor and the smoke check runs
/// there. Nothing downstream needs more than `{ width, height, data }`,
/// and `createImageBitmap` — which does need the real thing — is only
/// reached from the browser engine.
/// Which shape the last extraction produced (#212).
export const JOURNAL_GRAY = 0;
export const JOURNAL_JPEG = 1;

/// The entry's scan, whichever of the two shapes it is.
///
/// `{ kind: 'gray', image }` for a page this module decoded, already
/// cropped; `{ kind: 'jpeg', bytes, region }` for one it did not, where
/// the bytes are the whole page and the region is the part of it that is
/// the entry (`hosts/common/.../journal_ocr.h` says who applies it).
/// Null when the last extraction produced nothing.
///
/// The bytes are **copied** out of the module's heap. A typed array over
/// wasm memory is detached the moment that memory grows, and the next
/// thing this page does with a scan is hand it to an asynchronous engine
/// — so a view would be a use-after-free with a stack trace in somebody
/// else's library.
export function currentScan(module) {
  if (module._af_web_journal_encoding() === JOURNAL_JPEG) {
    const size = module._af_web_journal_encoded_size();
    if (size === 0) return null;
    const at = module._af_web_journal_encoded_bytes();
    return {
      kind: 'jpeg',
      bytes: module.HEAPU8.slice(at, at + size),
      region: {
        left: module._af_web_journal_region_left(),
        top: module._af_web_journal_region_top(),
        width: module._af_web_journal_region_width(),
        height: module._af_web_journal_region_height(),
      },
    };
  }
  const image = currentImage(module);
  return image === null ? null : { kind: 'gray', image };
}

/// The words of `data` that fall inside `region`, as lines.
///
/// tesseract.js reports a `bbox` per word for the same reason Tesseract's
/// `tsv` output does, so this is the browser's half of one rule written
/// once in `journal_ocr.h`: an encoded scan is a whole page, and the
/// entry is a rectangle of it. A word counts as inside when its centre
/// is, which is what gives the same answer a crop would have.
export function wordsWithin(data, region) {
  const words = data?.words ?? [];
  const right = region.left + region.width;
  const bottom = region.top + region.height;
  const lines = [];
  let current = null;
  let previous = null;
  for (const word of words) {
    const box = word?.bbox;
    const text = word?.text ?? '';
    if (!box || text === '') continue;
    const cx = (box.x0 + box.x1) / 2;
    const cy = (box.y0 + box.y1) / 2;
    if (cx < region.left || cx >= right || cy < region.top || cy >= bottom) {
      continue;
    }
    // `word.line` is the object tesseract.js groups words by; identity is
    // what says two words share one, and there is nothing else to compare.
    if (current === null || word.line !== previous) {
      current = [];
      lines.push(current);
      previous = word.line;
    }
    current.push(text);
  }
  return lines.map((line) => line.join(' ')).join('\n');
}

export function currentImage(module) {
  const width = module._af_web_journal_image_width();
  const height = module._af_web_journal_image_height();
  if (width === 0 || height === 0) return null;
  const at = module._af_web_journal_image_bytes();
  const gray = module.HEAPU8.subarray(at, at + width * height);
  const rgba = new Uint8ClampedArray(width * height * 4);
  for (let i = 0; i < gray.length; ++i) {
    const v = gray[i];
    rgba[i * 4] = v;
    rgba[i * 4 + 1] = v;
    rgba[i * 4 + 2] = v;
    rgba[i * 4 + 3] = 255;
  }
  if (typeof ImageData === 'function') {
    return new ImageData(rgba, width, height);
  }
  return { width, height, data: rgba };
}

/// Load the pinned tesseract.js, or answer null with a reason.
///
/// It is loaded by `<script>` rather than `import`: tesseract.js ships a
/// UMD bundle that defines a global, and asking a browser to treat it as
/// an ES module would be asking it to be something it is not.
export async function loadEngine({ url = ENGINE_URL, language = 'eng' } = {}) {
  if (typeof document === 'undefined') {
    return { engine: null, why: 'there is no browser here to load one into' };
  }
  try {
    const present = await fetch(url, { method: 'HEAD' });
    if (!present.ok) throw new Error(`${present.status}`);
  } catch {
    return { engine: null, why: `no OCR engine at ${url} - ${ENGINE_HINT}` };
  }

  await new Promise((resolve, reject) => {
    const tag = document.createElement('script');
    tag.src = url;
    tag.onload = resolve;
    tag.onerror = () => reject(new Error(`${url} would not load`));
    document.head.append(tag);
  });
  const lib = globalThis.Tesseract;
  if (!lib || typeof lib.createWorker !== 'function') {
    return { engine: null, why: `${url} is not tesseract.js` };
  }

  // Every path it may reach for, named, and all of them under the
  // directory the library itself came from. Left to its defaults,
  // tesseract.js fetches its worker, its wasm core and its language data
  // from a CDN at the moment of the first recognition — which is exactly
  // the thing this page does not do (see the top of this file).
  const base = url.slice(0, url.lastIndexOf('/') + 1);
  const worker = await lib.createWorker(language, undefined, {
    workerPath: `${base}worker.min.js`,
    corePath: base,
    langPath: base,
  });
  return {
    engine: {
      name: `tesseract.js ${lib.version ?? '(unversioned)'}`,
      async recognize(scan) {
        if (scan.kind === 'jpeg') {
          // The stream, under its own type, exactly as the document holds
          // it — this page has not decoded it and has no business
          // claiming to know more about it than the document did (#212).
          // The browser decodes it, tesseract.js reads the whole page,
          // and the region keeps the entry's words.
          const blob = new Blob([scan.bytes], { type: 'image/jpeg' });
          const { data } = await worker.recognize(blob);
          return wordsWithin(data, scan.region).replace(/\s+$/, '');
        }
        const bitmap = await createImageBitmap(scan.image);
        const { data } = await worker.recognize(bitmap);
        bitmap.close();
        return (data?.text ?? '').replace(/\s+$/, '');
      },
      async close() {
        await worker.terminate();
      },
    },
    why: null,
  };
}

/// Ingest `bytes` — a whole document, as the player's file input gave it.
///
/// `engine` is anything with a `recognize(scan)` and a `name`; null
/// is a legitimate answer and produces a report with every entry
/// extracted and none recognized, which is what a page with no engine
/// installed should show rather than an error.
///
/// `onProgress({ index, count, number })` is called before each entry, so
/// a page can say where it is; a hundred entries through a wasm OCR
/// engine is not a moment.
export async function ingestJournal(
  module,
  bytes,
  { engine = null, onProgress = null } = {},
) {
  const scratch = module._malloc(bytes.length);
  if (scratch === 0) throw new Error('out of wasm heap while passing a document');
  let opened;
  try {
    module.HEAPU8.set(bytes, scratch);
    opened = module._af_web_journal_ingest(scratch, bytes.length);
  } finally {
    // The module copied it (hosts/web/src/main.cpp), so this is ours to
    // free the moment the call is over.
    module._free(scratch);
  }

  const fingerprint = readText(module, (out, cap) =>
    module._af_web_journal_fingerprint(out, cap),
  );
  if (opened !== JOURNAL_OK) {
    return {
      ok: false,
      trouble: troubleName(module, opened),
      fingerprint,
      edition: null,
      entries: 0,
      extracted: 0,
      recognized: 0,
      engine: null,
    };
  }

  const edition = readText(module, (out, cap) =>
    module._af_web_journal_edition_name(out, cap),
  );
  const engineName = engine ? engine.name : 'none';
  withUtf8(module, engineName, (ptr) => module._af_web_journal_set_engine(ptr));

  const count = module._af_web_journal_entry_count();
  let extracted = 0;
  let recognized = 0;
  let firstTrouble = null;
  for (let index = 0; index < count; ++index) {
    const number = module._af_web_journal_entry_number(index);
    if (onProgress) onProgress({ index, count, number });

    const why = module._af_web_journal_extract(index);
    if (why !== JOURNAL_OK) {
      firstTrouble ??= { number, what: troubleName(module, why) };
      continue;
    }
    ++extracted;
    if (!engine) {
      firstTrouble ??= { number, what: 'there is no OCR engine to read it with' };
      continue;
    }
    const scan = currentScan(module);
    // One entry at a time, deliberately: the whole reason the module
    // extracts on demand is that only one page's scan is in memory at
    // once (hosts/common/.../journal_ingest.h).
    const text = scan === null
      ? ''
      : await engine.recognize(scan, { index, number });
    if (!text) {
      firstTrouble ??= { number, what: 'the OCR engine did not read it' };
      continue;
    }
    withUtf8(module, text, (ptr) => module._af_web_journal_set_text(number, ptr));
    ++recognized;
  }

  return {
    ok: true,
    trouble: null,
    firstTrouble,
    fingerprint,
    edition,
    entries: count,
    extracted,
    recognized,
    engine: engineName,
    store: {
      size: module._af_web_journal_store_size(),
      recognized: module._af_web_journal_store_recognized(),
      corrections: module._af_web_journal_store_corrections(),
      fingerprint: readText(module, (out, cap) =>
        module._af_web_journal_store_fingerprint(out, cap),
      ),
    },
  };
}

/// What a reader shows for one entry — the correction if there is one,
/// the scan otherwise. The in-game reader (M5-E4, #175) asks for the same
/// thing from inside the module, through the `journal_open` host service;
/// the page uses this to show a player what was read before they go in.
export function journalText(module, number) {
  return readText(module, (out, cap) =>
    module._af_web_journal_text(number, out, cap),
  );
}

/// A store's own bytes back into the module (M5-E4, #175).
///
/// The reader is answered out of this tab's store, so anything that wants
/// a reader without an ingestion in front of it — a player restoring the
/// text they exported, `tools/drive.mjs` pointed at a file — puts the
/// store in here first. Answers a `journal_trouble`: a file that is not
/// exactly the format is refused whole rather than half-read, which is
/// `journal_store.h`'s own rule and the reason it is strict.
export function readStore(module, text) {
  return withUtf8(module, text, (ptr, size) =>
    module._af_web_journal_store_read(ptr, size),
  );
}

/// A person's correction to one entry.
export function correctJournalEntry(module, number, text) {
  return withUtf8(module, text, (ptr) =>
    module._af_web_journal_correct(number, ptr),
  );
}

/// The store as its file would be, so a page can hand a player the text
/// they spent an hour correcting. The browser keeps no file in M5 (#174)
/// and the page says so; this is what makes that survivable.
export function serializeStore(module) {
  return readText(module, (out, cap) =>
    module._af_web_journal_store_write(out, cap),
  );
}

/// The probe: a synthetic document this project generates, so the whole
/// ingestion can be driven with no real one anywhere (`journal_probe.h`).
/// Test apparatus — `tests/smoke.mjs` is its only caller.
export function probeDocument(module) {
  module._af_web_journal_probe(1);
  const at = module._af_web_journal_probe_bytes();
  const size = module._af_web_journal_probe_size();
  return module.HEAPU8.slice(at, at + size);
}

/// FNV-1a, 32-bit, over a bitmap's bytes. The same hash `tests/smoke.mjs`
/// uses on a framebuffer, and for the same reason: no dependency, and
/// every implementation of it agrees with every other one.
/// The same hash over plain bytes, for a scan that is a stream rather
/// than pixels (#212).
export function hashBytes(bytes) {
  let hash = 0x811c9dc5;
  for (let i = 0; i < bytes.length; ++i) {
    hash ^= bytes[i];
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash >>> 0;
}

export function hashPixels(image) {
  let hash = 0x811c9dc5;
  const { data } = image;
  // The gray channel only: the other three are a copy of it and the
  // alpha is 255 everywhere (`currentImage`), so hashing all four would
  // be hashing the same bytes four times.
  for (let i = 0; i < data.length; i += 4) {
    hash ^= data[i];
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash >>> 0;
}

/// The fixture engine, on this side of the boundary: the probe's own
/// words for the probe's own pixels, and nothing for anything else.
///
/// It compares the pixels it was handed against the module's *other*
/// derivation of them — `journal_probe_expected()`, generated from the
/// same description the document was generated from — so it cannot be
/// satisfied by an extraction that went wrong anywhere. That is what
/// makes a store with the probe's words in it evidence rather than
/// decoration, and it is the same argument the desktop fixture makes
/// (`hosts/common/.../journal_probe.h`).
export function probeEngine(module) {
  return {
    name: 'amberfolio journal probe fixture (page)',
    recognize(scan, { index }) {
      if (!scan) return '';
      // Whichever shape the entry is (#212): the module hashes the one it
      // expected, and this hashes the one it was handed. They are the
      // same FNV-1a over the same bytes, so a passthrough that altered a
      // single byte of a stream is a mismatch and an empty answer.
      const hash =
        scan.kind === 'jpeg'
          ? hashBytes(scan.bytes)
          : scan.image && scan.image.width !== 0
            ? hashPixels(scan.image)
            : null;
      if (hash === null || hash !== module._af_web_journal_probe_hash(index)) {
        return '';
      }
      return readText(module, (out, cap) =>
        module._af_web_journal_probe_text(index, out, cap),
      );
    },
  };
}
