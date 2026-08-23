// SPDX-License-Identifier: AGPL-3.0-only
//
// Getting a directory off the player's machine and into the page
// (M3-F2, #84).
//
// Browser-only, like app.mjs and for the same reason: it touches
// `document`, `DataTransfer` and `File`, none of which exist under node.
// host.mjs stays DOM-free so `ctest --preset wasm` can import it; this
// file is the half that cannot be.
//
// It is a **dev-page affordance and nothing more**. Onboarding,
// fingerprint UX and IndexedDB persistence are M6's reference shell; what
// this does is read the bytes and hand them over. Nothing is kept: a
// reload starts from an empty filesystem, which is the honest behaviour
// for scaffolding and one less thing to be wrong about a player's files.
//
//
// Two ways in, one answer
// -----------------------
//
// `<input type="file" webkitdirectory>` and a drop target, because
// browsers disagree about which one a person will reach for and both are
// three lines. `webkitdirectory` is unprefixed nowhere and standardised
// nowhere; every engine ships it under that name, which is why the
// attribute looks like a relic.
//
// Both produce the same thing: a list of `{ path, bytes }`, where `path`
// is the file's place **relative to the directory the player chose**.
// That directory is the disk, so it is the root and not a component of
// anything on it — which is why `webkitRelativePath`'s first component,
// the chosen directory's own name, comes off.
//
// It used to be `{ name, bytes }`, with everything above the leaf thrown
// away, because the door into the VFS reached the root and no further.
// The visible cost was that a browser could start a game and never
// resume one: `\SAVE\` is where a shipped save slot lives and it was
// dropped on the way in. #146 widened the door; this is the half above
// it.
//
//
// What this file deliberately does not do
// ---------------------------------------
//
// **It does not look at the names.** Not to filter, not to upper-case,
// not to decide what is a program, and — since #146 — not to decide what
// a separator is either. Whether `Save1.Dat` and `SAVE1.DAT` are the same
// file, and whether `SAVE/Save1.Dat` and `\SAVE\SAVE1.DAT` are, are both
// DOS rules; they live in core (`canonicalize()`) and the ABI runs every
// path through them (abi.h). So the browser's own `/` goes across
// untouched. A page that pre-judged any of this would be a second
// implementation of that rule, and two implementations of a naming rule
// eventually disagree.
//
// So a file whose name no DOS short name can equal — the PDF that comes
// with a boxed copy, say — is not filtered here. It is offered, refused
// by core, and reported as skipped. The refusal is the feature.

/// Read one `File` into a `Uint8Array`.
async function readFile(file) {
  const buffer = await file.arrayBuffer();
  return new Uint8Array(buffer);
}

/// Where the browser says this file sits, relative to the directory the
/// player chose.
///
/// `webkitRelativePath` is the chosen directory's own name followed by
/// the path below it, and is empty for a dropped file. The chosen
/// directory is the disk, so its name is the root: the first component
/// comes off and the rest goes across as it stands, separators included
/// (core takes `/`, abi.h).
function pathOf(file) {
  const relative = file.webkitRelativePath;
  if (!relative) return file.name;
  const cut = relative.indexOf('/');
  return cut === -1 ? relative : relative.slice(cut + 1);
}

/// Read every `File` in `files` into `{ path, bytes }`, in the order the
/// browser listed them.
async function readAll(files) {
  const out = [];
  for (const file of files) {
    out.push({ path: pathOf(file), bytes: await readFile(file) });
  }
  return out;
}

/// One dropped entry, at `prefix` — a file read into `out`, or a
/// directory descended into with its own name added to the prefix.
async function readDroppedEntry(entry, out, prefix) {
  const path = prefix === '' ? entry.name : `${prefix}/${entry.name}`;
  if (entry.isFile) {
    const file = await new Promise((resolve, reject) =>
      entry.file(resolve, reject),
    );
    out.push({ path, bytes: await readFile(file) });
    return;
  }
  if (entry.isDirectory) await readDroppedChildren(entry, out, path);
}

/// Everything inside one dropped `FileSystemDirectoryEntry`, recursively.
///
/// Dropping a folder gives entries rather than files, and the directory
/// reader hands them over in batches until it answers an empty one —
/// which is the part of this API that is easy to get wrong, because a
/// single `readEntries()` call looks as though it worked.
async function readDroppedChildren(entry, out, prefix) {
  const reader = entry.createReader();
  for (;;) {
    const batch = await new Promise((resolve, reject) =>
      reader.readEntries(resolve, reject),
    );
    if (batch.length === 0) return;
    for (const child of batch) {
      await readDroppedEntry(child, out, prefix);
    }
  }
}

/// Wire `input` (a `<input type="file" webkitdirectory>`) and `dropZone`
/// (any element) so that either one calls `onFiles` with the list of
/// `{ path, bytes }` the player chose.
///
/// `onFiles` is called from inside the browser's own event handler, which
/// matters: it is a user gesture, and a user gesture is what a page needs
/// before it may start an AudioContext. The dev page instantiates the
/// module here for exactly that reason.
export function wireDirectoryPicker({ input, dropZone, onFiles, onError }) {
  const deliver = async (produce) => {
    try {
      const files = await produce();
      if (files.length > 0) await onFiles(files);
    } catch (error) {
      if (onError) onError(error);
      else console.error(error);
    }
  };

  if (input) {
    input.addEventListener('change', () => {
      const chosen = Array.from(input.files ?? []);
      void deliver(() => readAll(chosen));
    });
  }

  if (dropZone) {
    // Both, and on the zone rather than the document: without
    // preventDefault on dragover the browser navigates to the dropped
    // file instead of letting the page have it.
    dropZone.addEventListener('dragover', (event) => {
      event.preventDefault();
      dropZone.classList.add('dragging');
    });
    dropZone.addEventListener('dragleave', () => {
      dropZone.classList.remove('dragging');
    });
    dropZone.addEventListener('drop', (event) => {
      event.preventDefault();
      dropZone.classList.remove('dragging');
      const items = Array.from(event.dataTransfer?.items ?? []);
      const entries = items
        .map((item) => item.webkitGetAsEntry?.())
        .filter(Boolean);

      void deliver(async () => {
        if (entries.length > 0) {
          const out = [];
          for (const entry of entries) {
            // A dropped *folder* is the disk, exactly as a chosen one is,
            // so its own name is the root rather than a component: its
            // children start at the empty prefix. A dropped file is
            // already at the root.
            if (entry.isDirectory) await readDroppedChildren(entry, out, '');
            else await readDroppedEntry(entry, out, '');
          }
          return out;
        }
        // No entry API: plain files, which is what dropping a selection
        // rather than a folder gives.
        return readAll(Array.from(event.dataTransfer?.files ?? []));
      });
    });
  }
}
