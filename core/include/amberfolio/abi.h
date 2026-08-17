// SPDX-License-Identifier: AGPL-3.0-only
//
// The core's C ABI — the surface a non-C++ host talks to. It exists for
// the wasm build (PLAN.md §4: "compiled ... via Emscripten to wasm32 with
// a small C ABI for the JS host"), but it is plain C in every build, so a
// native host or a test can call exactly what the browser calls.
//
// M0 content is one function. The narrow platform interface — frame out,
// audio pull, input in, VFS, clock — grows here in M2. Rules for what may
// be added: C linkage only, no structs by value, no ownership handed
// across the boundary that the other side has to free, and nothing that
// can throw. Which of these symbols the wasm module actually exports is
// decided at link time by hosts/web/CMakeLists.txt.

#ifndef AMBERFOLIO_ABI_H
#define AMBERFOLIO_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The version of the core, packed as 0x00MMmmpp: major in bits 16-23,
/// minor in 8-15, patch in 0-7. One scalar rather than three calls, and
/// directly comparable — the JS side unpacks it the same way AF_VERSION_*
/// does below.
///
/// This is the *linked* version (see amberfolio::linked_version), which is
/// the only kind that means anything across an ABI: a JS host has no
/// headers it was compiled against.
uint32_t af_version(void);

#ifdef __cplusplus
}  // extern "C"
#endif

// No casts in these: the operands are already uint32_t, and the core is
// built with -Wold-style-cast, which a C-compatible header cannot satisfy
// with a cast in it.
#define AF_VERSION_MAJOR(v) (((v) >> 16) & 0xFFu)
#define AF_VERSION_MINOR(v) (((v) >> 8) & 0xFFu)
#define AF_VERSION_PATCH(v) ((v) & 0xFFu)

#endif  // AMBERFOLIO_ABI_H
