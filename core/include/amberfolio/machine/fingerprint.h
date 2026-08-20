// SPDX-License-Identifier: AGPL-3.0-only
//
// The identity of a file on the machine's filesystem: its SHA-256.
//
// PLAN.md §2 makes this the fact a player's copy is known by, and PLAN.md
// §5 makes it the key a seam is keyed on. M3-F1 (#83) is where it first
// appears in a run — the boot driver prints it at load — and M4's
// fingerprint table is what will look it up.
//
// It is a free function over `filesystem` rather than a method on it,
// for the same reason `canonicalize()` is a free function over paths
// (vfs.h): a backend implements storage and nothing else, and a rule
// every backend would have to implement identically is a rule that
// belongs above all of them. A directory-backed host (#54) and the
// in-memory backend (memory_vfs.h) therefore cannot disagree about what
// the fingerprint of a file is, because neither of them computes one.
//
// It reads the file in `sha256_hasher::block_bytes`-aligned chunks
// through a stack buffer, so hashing a 200 KiB overlay costs a few
// kilobytes of stack and no allocation at all — the rule the rest of
// core lives by (PLAN.md §4), and the reason this cannot simply read the
// file into a vector and hash that.

#pragma once

#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"

namespace amberfolio::machine {

/// The SHA-256 of every byte of `path`, as `fs` holds it.
///
/// A `vfs_error` if the file cannot be opened or read — the same errors
/// `open()` and `read()` answer with, passed through rather than
/// translated, because a fingerprint that failed failed for a filesystem
/// reason and there is nothing this layer can add. The handle is closed
/// on every path out.
[[nodiscard]] vfs_result<sha256_digest> fingerprint_file(filesystem& fs,
                                                         const dos_path& path);

}  // namespace amberfolio::machine
