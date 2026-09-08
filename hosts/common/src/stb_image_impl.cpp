// SPDX-License-Identifier: AGPL-3.0-only
//
// stb_image's implementation, alone in a translation unit of its own.
//
// The header is a library and a library's source in one file, and which
// of the two it is depends on a macro. Everything that includes it to
// *call* it gets the declarations; this file, once, gets the code — the
// arrangement stb asks for, and the reason it is here rather than at the
// top of journal_jpeg.cpp: warnings. Our baseline is `-Werror` and a
// third-party implementation is not held to it, so this one source is
// compiled with warnings off (hosts/common/CMakeLists.txt) and there is
// nothing of ours in it to lose by that.
//
// What is switched off is as important as what is on:
//
// - **JPEG only.** Every other format stb decodes is a format no tabled
//   edition has, and each is more code in a player's download and another
//   parser reading a file this project did not write.
// - **No stdio.** Nothing here ever hands it a path; a page arrives as
//   the bytes of a stream inside a document already in memory, and the
//   file-reading half would be a door onto the player's disk that no
//   caller needs.
// - **No thread-local failure strings**, because the one caller wants a
//   yes or a no: a reason from inside a decoder is not a sentence a
//   player can act on, and `journal_page_decoder` reports the filter it
//   could not read, which is (`docs/journal.md` §4).

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include <stb_image.h>
