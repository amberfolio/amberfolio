# SPDX-License-Identifier: AGPL-3.0-only
#
# stb_image (public domain / MIT), fetched at configure time like every
# other dependency and never committed here.
#
# It decodes the `/DCTDecode` pages of a player's own journal, which is
# the one thing an entry that is a *picture* needs and an entry that is
# text does not: a page of text reaches an OCR engine as the stream's own
# bytes and the engine decodes it (`docs/journal.md` §4a), and a drawing
# has no engine to hand that work to.
#
#
# This reverses #212, and the reason it can
# -----------------------------------------
#
# #212 refused to put a JPEG decoder in this project, and #328 built
# `host::journal_page_decoder` as a door so that a host could fill it out
# of something it linked for another reason. What that produced was a
# feature only one build has: the `AMBERFOLIO_LINK_TESSERACT` desktop
# host made pictures out of Leptonica, a default desktop build reported
# `pictures=0/14`, and the browser made none at all — so a player got the
# words of an entry and not the map (#345). The door is the right shape
# and the wrong default.
#
# What decides it is the same sentence AmberfolioLibdeflate.cmake ends on:
# a decoder written here could only ever be tested against streams this
# tree wrote, and a decoder tested against its own encoder is tested
# against its own misreadings. So: not ours, and not optional.
#
#
# Why this one
# ------------
#
# libjpeg-turbo is the reference implementation and is already built by
# `AmberfolioTesseract.cmake` — for the OCR chain, where it is Leptonica's
# dependency rather than ours. It cannot be the always-on answer: it
# refuses `add_subdirectory` outright and insists on `ExternalProject`,
# which means one static library per configuration on MSVC (the
# `AMBERFOLIO_OCR_CONFIG` wrinkle that file already carries) and a
# cross-compiled sub-build for wasm. A dependency every configure pays
# for has to be the shape every other one here is.
#
# stb_image is one header, builds identically on all five presets, and
# decodes baseline and progressive JPEG — which is every page any tabled
# edition has. What it does not do is arithmetic coding, 12-bit samples
# and CMYK; those come back as an honest false and the name of the filter,
# which is `docs/machine.md` §5's rule and the same answer a build with no
# decoder used to give.
#
# It is compiled into `amberfolio::host_services` and reaches both hosts,
# so every build of this project reduces a picture the same way and one
# document has one store fingerprint wherever it was ingested — which was
# not true while Leptonica did it on one build and nothing did it on the
# others (`docs/journal.md` §11.2).
#
# Public domain (Unlicense) or MIT at the user's option, so compatible
# with our AGPL-3.0-only outbound licence (CONTRIBUTING.md). NOTICE.md
# says so.

include_guard(GLOBAL)
include(FetchContent)

# Pinned by content, not by branch: the URL names a commit and the hash is
# the file. A build of a given commit of this repository fetches the same
# header every time, and a substituted one does not configure.
set(AMBERFOLIO_STB_IMAGE_COMMIT
  "f0569113c93ad095470c54bf34a17b36646bbbb5" CACHE STRING
  "nothings/stb commit to fetch stb_image.h from")
set(AMBERFOLIO_STB_IMAGE_SHA256
  "594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3"
  CACHE STRING "SHA-256 of the stb_image.h fetched above (v2.30)")

# One header, so there is nothing to unpack and no CMakeLists to add: what
# comes back is a directory with a file in it, which is exactly what an
# include directory is.
FetchContent_Declare(amberfolio_stb_image
  URL "https://raw.githubusercontent.com/nothings/stb/${AMBERFOLIO_STB_IMAGE_COMMIT}/stb_image.h"
  URL_HASH "SHA256=${AMBERFOLIO_STB_IMAGE_SHA256}"
  DOWNLOAD_NO_EXTRACT TRUE)

message(STATUS "fetching stb_image ${AMBERFOLIO_STB_IMAGE_COMMIT}")
FetchContent_MakeAvailable(amberfolio_stb_image)

add_library(amberfolio-stb-image INTERFACE)
add_library(amberfolio::stb_image ALIAS amberfolio-stb-image)
# SYSTEM: third-party headers are not held to our warning baseline. The
# *implementation* is a translation unit of ours and is silenced where it
# is compiled (hosts/common/CMakeLists.txt), because one header included
# with its implementation macro defined is code we own the compile of.
target_include_directories(amberfolio-stb-image SYSTEM
  INTERFACE "${amberfolio_stb_image_SOURCE_DIR}")
