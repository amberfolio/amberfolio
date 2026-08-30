# SPDX-License-Identifier: AGPL-3.0-only
#
# Tesseract, Leptonica and a JPEG decoder, built once and linked into the
# desktop host (M5-E3c, #216).
#
# **Off by default**, and that is the whole reason this is bearable.
# `tesseract_ocr.h` argued at length against linking an OCR engine, and
# the argument was never about licences — Tesseract is Apache-2.0 and
# Leptonica BSD-2-Clause, both of which CONTRIBUTING.md's inbound rule
# allows. It was about what every contributor pays at every configure for
# a feature that runs once during onboarding. An option nobody turns on
# costs nothing at all, so that objection is answered rather than
# overruled, and what is left is the thing it was standing in the way of:
# a build where the journal reader works for a player who has installed
# nothing.
#
#
# ExternalProject, not FetchContent, and libjpeg-turbo insists
# ------------------------------------------------------------
#
# Every other dependency here is `FetchContent_MakeAvailable`, which is
# `add_subdirectory` — the dependency joins *our* build graph. libjpeg-turbo
# refuses that outright ("cannot be integrated into another build system
# using add_subdirectory(); use ExternalProject_Add() instead"), and for
# these three it is the wrong shape anyway. They are pinned, they never
# change, and nothing here will ever edit them: what is wanted is a
# configure, a build and an install that happen **once per build tree** and
# are never re-entered. That is what ExternalProject does, behind a stamp
# file, and it is why turning this option on costs one slow build rather
# than a slow build every time.
#
# The three are chained because each finds the one before it through
# `CMAKE_PREFIX_PATH`, and the order is the dependency order: a JPEG
# decoder, then Leptonica which reads images with it, then Tesseract which
# reads pages with Leptonica.
#
#
# Why a JPEG decoder is not optional here
# ---------------------------------------
#
# Leptonica's image formats are each behind a `find_package`, so they
# switch themselves off when the library is not there — silently, and with
# a working build at the end of it. The first real journal edition's pages
# are every one of them `/DCTDecode` (#212), so a Leptonica built without
# libjpeg produces a host that links, runs, and reads nothing. Everything
# else is switched **off** on purpose: PNG, TIFF, GIF, WebP and OpenJPEG
# are formats no edition in the table needs, and each is another library
# to fetch and another way for this to break on somebody's machine.
#
#
# The language data is not code, and is not committed
# ---------------------------------------------------
#
# `eng.traineddata` is a trained model — four megabytes of it — under the
# same Apache-2.0 as Tesseract itself. It is fetched here rather than
# committed for the reason the conformance vectors and SDL3 are fetched:
# this repository's history can never be rewritten (CLAUDE.md), so a blob
# committed once is permanent for every clone anybody ever makes. The
# content guard enforces that with a size cap, and the browser already
# fetches this very file through `scripts/fetch-ocr-engine.py`.

include_guard(GLOBAL)

option(AMBERFOLIO_LINK_TESSERACT
  "Build Tesseract, Leptonica and libjpeg-turbo and link them into the \
desktop host, so a player needs no OCR engine installed. Off by default: \
it is a long one-time build, and the host works without it by running an \
engine the player already has (hosts/sdl/src/tesseract_ocr.h)." OFF)

if(NOT AMBERFOLIO_LINK_TESSERACT)
  return()
endif()

include(ExternalProject)

# Pinned, not floating — the same rule every other fetched dependency
# follows: a build of a given commit of this repository fetches the same
# library every time.
set(AMBERFOLIO_TESSERACT_TAG "5.5.1" CACHE STRING
  "tesseract git tag to build")
set(AMBERFOLIO_LEPTONICA_TAG "1.85.0" CACHE STRING
  "leptonica git tag to build")
set(AMBERFOLIO_LIBJPEG_TAG "3.1.0" CACHE STRING
  "libjpeg-turbo git tag to build")
# The language Tesseract reads with, and where its model comes from. The
# same file, from the same tag, that the browser's engine uses.
set(AMBERFOLIO_TESSDATA_TAG "4.1.0" CACHE STRING
  "tessdata_fast git tag to fetch eng.traineddata from")

# **One configuration, named at configure time**, and this is the wrinkle
# worth knowing about. The presets here use Ninja Multi-Config, so the host
# is built as Debug and Release out of one tree — but on MSVC a static
# library built against one C runtime cannot be linked into a binary using
# another, and this engine hands back memory the host frees. Building the
# engine twice would double an already long build for no gain, and
# `IMPORTED_LOCATION` does not take a generator expression anyway.
#
# So the engine is built once, for the configuration named here, and a
# host built in the other one will not link it. That is a loud failure
# rather than a quiet one, and the fix is one flag.
set(AMBERFOLIO_OCR_CONFIG "Debug" CACHE STRING
  "which configuration the linked OCR engine is built for; a host built in another one will not link against it")
set_property(CACHE AMBERFOLIO_OCR_CONFIG PROPERTY STRINGS Debug Release)

set(_af_ocr "${CMAKE_BINARY_DIR}/ocr")
set(AMBERFOLIO_TESSDATA_DIR "${_af_ocr}/share/tessdata" CACHE INTERNAL
  "where the desktop host's linked engine looks for eng.traineddata")

# One configuration for all three, and the runtime library matters: MSVC
# will not link objects built against a different C runtime, and this is
# the one thing that turns a working chain into a wall of duplicate-symbol
# errors.
if(AMBERFOLIO_OCR_CONFIG STREQUAL "Debug")
  set(_af_ocr_crt "MultiThreadedDebugDLL")
  set(_af_ocr_d "d")
else()
  set(_af_ocr_crt "MultiThreadedDLL")
  set(_af_ocr_d "")
endif()

set(_af_ocr_args
  -DCMAKE_INSTALL_PREFIX=${_af_ocr}
  -DCMAKE_PREFIX_PATH=${_af_ocr}
  -DCMAKE_BUILD_TYPE=${AMBERFOLIO_OCR_CONFIG}
  -DCMAKE_MSVC_RUNTIME_LIBRARY=${_af_ocr_crt}
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DBUILD_SHARED_LIBS=OFF)

# What the three install, spelled before they are declared: Ninja needs to
# know which rule produces a file before anything can depend on it, and
# these carry Leptonica's version and MSVC's debug `d`.
if(MSVC)
  set(_af_lept_name "leptonica-${AMBERFOLIO_LEPTONICA_TAG}${_af_ocr_d}")
  set(_af_tess_name "tesseract55${_af_ocr_d}")
else()
  set(_af_lept_name "leptonica")
  set(_af_tess_name "tesseract")
endif()

set(_af_lib "${_af_ocr}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}")
set(_af_suffix "${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(_af_jpeg_lib "${_af_lib}jpeg-static${_af_suffix}")
set(_af_lept_lib "${_af_lib}${_af_lept_name}${_af_suffix}")
set(_af_tess_lib "${_af_lib}${_af_tess_name}${_af_suffix}")

ExternalProject_Add(amberfolio-libjpeg
  GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git
  GIT_TAG ${AMBERFOLIO_LIBJPEG_TAG}
  GIT_SHALLOW TRUE
  CMAKE_ARGS ${_af_ocr_args}
    -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF
  BUILD_BYPRODUCTS "${_af_jpeg_lib}")

# Everything but JPEG off: see the header. `BUILD_PROG` is Leptonica's
# forty-odd command-line tools, which nothing here runs.
ExternalProject_Add(amberfolio-leptonica
  DEPENDS amberfolio-libjpeg
  GIT_REPOSITORY https://github.com/DanBloomberg/leptonica.git
  GIT_TAG ${AMBERFOLIO_LEPTONICA_TAG}
  GIT_SHALLOW TRUE
  CMAKE_ARGS ${_af_ocr_args}
    -DBUILD_PROG=OFF -DSW_BUILD=OFF
    -DENABLE_JPEG=ON
    -DENABLE_ZLIB=OFF -DENABLE_PNG=OFF -DENABLE_GIF=OFF
    -DENABLE_TIFF=OFF -DENABLE_WEBP=OFF -DENABLE_OPENJPEG=OFF
  BUILD_BYPRODUCTS "${_af_lept_lib}")

# The training tools are a second program and a second dependency set;
# curl and libarchive are for fetching models over the network, which this
# does not do; ScrollView is a debug GUI written in Java.
ExternalProject_Add(amberfolio-tesseract
  DEPENDS amberfolio-leptonica
  GIT_REPOSITORY https://github.com/tesseract-ocr/tesseract.git
  GIT_TAG ${AMBERFOLIO_TESSERACT_TAG}
  GIT_SHALLOW TRUE
  CMAKE_ARGS ${_af_ocr_args}
    -DSW_BUILD=OFF -DBUILD_TRAINING_TOOLS=OFF -DBUILD_TESTS=OFF
    -DDISABLE_CURL=ON -DDISABLE_ARCHIVE=ON -DDISABLE_TIFF=ON
    -DGRAPHICS_DISABLED=ON -DDISABLED_LEGACY_ENGINE=ON
    -DUSE_SYSTEM_ICU=OFF -DOPENMP_BUILD=OFF -DINSTALL_CONFIGS=OFF
  BUILD_BYPRODUCTS "${_af_tess_lib}")

# The model. Downloaded rather than cloned: tessdata_fast's history is
# hundreds of megabytes of models for languages nobody here asked for.
ExternalProject_Add(amberfolio-tessdata
  DOWNLOAD_DIR "${AMBERFOLIO_TESSDATA_DIR}"
  URL "https://github.com/tesseract-ocr/tessdata_fast/raw/${AMBERFOLIO_TESSDATA_TAG}/eng.traineddata"
  DOWNLOAD_NO_EXTRACT TRUE
  DOWNLOAD_NAME eng.traineddata
  CONFIGURE_COMMAND "" BUILD_COMMAND "" INSTALL_COMMAND "")

# What the host links. Imported rather than found: `find_package` runs at
# configure time and these are built at build time, so the paths are
# declared and the ordering is a target dependency.
#
# The library names carry Leptonica's version and MSVC's debug `d`, which
# is why they are spelled from the cache variables rather than guessed.
function(_amberfolio_ocr_library name file)
  add_library(${name} STATIC IMPORTED GLOBAL)
  set_target_properties(${name} PROPERTIES
    IMPORTED_LOCATION "${file}"
    INTERFACE_INCLUDE_DIRECTORIES "${_af_ocr}/include")
endfunction()

_amberfolio_ocr_library(amberfolio::jpeg "${_af_jpeg_lib}")
_amberfolio_ocr_library(amberfolio::leptonica "${_af_lept_lib}")
_amberfolio_ocr_library(amberfolio::tesseract "${_af_tess_lib}")

# The include directory has to exist at configure time or CMake refuses
# the interface property, and it will not until the first build.
file(MAKE_DIRECTORY "${_af_ocr}/include")

add_custom_target(amberfolio-ocr-engine
  DEPENDS amberfolio-tesseract amberfolio-tessdata)
