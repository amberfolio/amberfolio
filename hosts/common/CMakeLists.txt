# SPDX-License-Identifier: AGPL-3.0-only
#
# What both hosts share (M5-D1, #169).
#
# There was no such directory until the host-service door: the SDL host
# and the web host had no C++ in common, because the web host's C++ is
# the ABI and the SDL host's is a window and an audio device. A
# `seam_host_services` is the first thing that has to be the *same object*
# on both — see hosts/common/include/amberfolio/host/host_services.h for
# why it is C++ inside the module rather than JS on the page.
#
# It links only the core, so it builds under Emscripten as it stands, and
# adds nothing to the wasm module's dependency footprint that was not
# already there.

# --- The edition table, compiled from data/editions.json (#207) --------
#
# One table, two shapes. A page has to render "here is what you dropped
# and here is what is missing" while the wasm module is still
# downloading, so the requirements have to be a file a site can fetch —
# `data/editions.json`, attached to every release beside manifest.json
# (docs/hosts.md §5). A desktop host has neither a page nor a fetch, so
# it needs the same facts linked in. Rather than write them twice, the
# JSON *is* the table and this turns it into the arrays behind
# `edition_requirements_table()`.
#
# At configure time, in CMake, because CMake is already required and
# nothing else here is: a python3 in the build path would be a new
# prerequisite on three platforms for a file of a hundred and thirty
# lines. CMAKE_CONFIGURE_DEPENDS below is what makes editing the JSON
# re-run this, the way tests/CMakeLists.txt watches the conformance
# manifest.
set(_amberfolio_editions_json "${PROJECT_SOURCE_DIR}/data/editions.json")
set_property(DIRECTORY APPEND PROPERTY
  CMAKE_CONFIGURE_DEPENDS "${_amberfolio_editions_json}")
file(READ "${_amberfolio_editions_json}" _amberfolio_editions_text)

# The schema is a version, and it is checked rather than assumed: a
# consumer pins it (docs/hosts.md §5), so a table that changed shape
# under this parser must stop the configure and not compile to something
# subtly wrong.
string(JSON _amberfolio_editions_schema GET
  "${_amberfolio_editions_text}" schema)
if(NOT _amberfolio_editions_schema STREQUAL "amberfolio.editions/1")
  message(FATAL_ERROR
    "${_amberfolio_editions_json} declares schema "
    "'${_amberfolio_editions_schema}'; this parser reads "
    "amberfolio.editions/1. A schema change is a change to what every "
    "consumer pinning a release reads, so it is a decision and not a "
    "field edit.")
endif()

set(AMBERFOLIO_EDITION_ARTIFACTS "")
set(AMBERFOLIO_EDITIONS "")
set(AMBERFOLIO_EDITION_ARTIFACT_COUNT 0)
string(JSON AMBERFOLIO_EDITION_COUNT LENGTH
  "${_amberfolio_editions_text}" editions)
if(AMBERFOLIO_EDITION_COUNT LESS 1)
  message(FATAL_ERROR
    "${_amberfolio_editions_json} names no editions: a build that "
    "recognises a program image and cannot say what that edition is "
    "made of is the gap #207 exists to close.")
endif()

math(EXPR _amberfolio_edition_last "${AMBERFOLIO_EDITION_COUNT} - 1")
foreach(_amberfolio_e RANGE ${_amberfolio_edition_last})
  string(JSON _amberfolio_ed GET
    "${_amberfolio_editions_text}" editions ${_amberfolio_e})
  string(JSON _amberfolio_ed_id GET "${_amberfolio_ed}" id)
  string(JSON _amberfolio_ed_name GET "${_amberfolio_ed}" name)
  string(JSON _amberfolio_ed_boot GET "${_amberfolio_ed}" boot)
  string(JSON _amberfolio_ed_fingerprint GET "${_amberfolio_ed}" fingerprint)
  string(JSON _amberfolio_ed_artifacts LENGTH "${_amberfolio_ed}" artifacts)

  set(_amberfolio_ed_offset ${AMBERFOLIO_EDITION_ARTIFACT_COUNT})
  math(EXPR _amberfolio_a_last "${_amberfolio_ed_artifacts} - 1")
  foreach(_amberfolio_a RANGE ${_amberfolio_a_last})
    string(JSON _amberfolio_art GET
      "${_amberfolio_ed}" artifacts ${_amberfolio_a})
    string(JSON _amberfolio_art_kind GET "${_amberfolio_art}" kind)
    string(JSON _amberfolio_art_required GET "${_amberfolio_art}" required)
    # The four optional keys. ERROR_VARIABLE rather than a membership
    # test: a directory has no digest and no size and a document has no
    # filename, and "" / 0 is exactly what the C++ reads those as.
    string(JSON _amberfolio_art_name ERROR_VARIABLE _amberfolio_ignored
      GET "${_amberfolio_art}" name)
    string(JSON _amberfolio_art_about ERROR_VARIABLE _amberfolio_ignored
      GET "${_amberfolio_art}" about)
    string(JSON _amberfolio_art_sha ERROR_VARIABLE _amberfolio_ignored
      GET "${_amberfolio_art}" sha256)
    string(JSON _amberfolio_art_size ERROR_VARIABLE _amberfolio_ignored
      GET "${_amberfolio_art}" size)
    string(JSON _amberfolio_art_document ERROR_VARIABLE _amberfolio_ignored
      GET "${_amberfolio_art}" document)
    foreach(_amberfolio_field name about sha size document)
      if(_amberfolio_art_${_amberfolio_field} MATCHES "NOTFOUND$")
        set(_amberfolio_art_${_amberfolio_field} "")
      endif()
    endforeach()
    if(_amberfolio_art_size STREQUAL "")
      set(_amberfolio_art_size 0)
    endif()
    if(_amberfolio_art_document STREQUAL "")
      set(_amberfolio_art_document none)
    endif()
    if(_amberfolio_art_required)
      set(_amberfolio_art_required_cxx true)
    else()
      set(_amberfolio_art_required_cxx false)
    endif()
    string(APPEND AMBERFOLIO_EDITION_ARTIFACTS
      "        {.name = \"${_amberfolio_art_name}\","
      " .about = \"${_amberfolio_art_about}\","
      " .fingerprint = \"${_amberfolio_art_sha}\",\n"
      "         .size = ${_amberfolio_art_size}U,"
      " .kind = artifact_kind::${_amberfolio_art_kind},\n"
      "         .document = machine::document_kind::"
      "${_amberfolio_art_document},\n"
      "         .required = ${_amberfolio_art_required_cxx}},\n")
    math(EXPR AMBERFOLIO_EDITION_ARTIFACT_COUNT
      "${AMBERFOLIO_EDITION_ARTIFACT_COUNT} + 1")
  endforeach()

  string(APPEND AMBERFOLIO_EDITIONS
    "        {.id = \"${_amberfolio_ed_id}\",\n"
    "         .name = \"${_amberfolio_ed_name}\",\n"
    "         .boot = \"${_amberfolio_ed_boot}\",\n"
    "         .fingerprint = \"${_amberfolio_ed_fingerprint}\",\n"
    "         .artifacts = std::span<const edition_artifact>{\n"
    "             all_artifacts.data() + ${_amberfolio_ed_offset},\n"
    "             ${_amberfolio_ed_artifacts}}},\n")
endforeach()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/edition_table.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/amberfolio/host/edition_table.h"
  @ONLY)

add_library(amberfolio-host-services STATIC
  src/host_services.cpp
  src/slot_store.cpp
  # What each edition is made of, and what a host has in front of it
  # (M6, #207). Here rather than in core because it is not the machine's
  # business: core keys its seam tables on one fingerprint, and this is
  # the checklist a *host* renders before anything is loaded.
  src/edition_facts.cpp
  # What a player answered, so the code wheel asks once and not once a
  # launch (M6-C1b, #292). Here for the same reason the two above are:
  # both hosts keep the identical thing, and only where the bytes land
  # differs.
  src/code_wheel_store.cpp
  # The journal's ingestion (M5-E3, #174): the edition fact table, the
  # extractor, the text store, and the synthetic document the whole
  # pipeline is checked against. Here rather than in either host because
  # both do the identical thing with it -- the only difference between a
  # desktop ingestion and a browser one is which OCR engine is on the far
  # side of journal_ocr.h.
  src/journal_facts.cpp
  src/journal_extract.cpp
  src/journal_store.cpp
  src/journal_ingest.cpp
  # The entries that are pictures (#328): a printed drawing reduced, once,
  # to the four tones and the box the in-game reader draws in. Beside the
  # extractor because it is the same half of the pipeline -- something a
  # host does to a player's own document, on the player's own machine, and
  # writes into the same store.
  src/journal_picture.cpp
  # The decoder in front of the reducer (#345). A picture has no OCR
  # engine to hand a `/DCTDecode` page to, so before this the only build
  # that could make one was the one that had linked Leptonica for another
  # reason -- cmake/AmberfolioStbImage.cmake is why that stopped being
  # good enough and why the library it calls is the one it is.
  src/journal_jpeg.cpp
  # How well an ingestion went, as a number (#315). In the library and
  # not in a test, because the thing that has to answer it is a *host*
  # reporting to a player -- and because the ground truth is the store's
  # own corrections, which only a running host holds.
  src/journal_score.cpp
  src/journal_probe.cpp
  # The keys a host still holds a make for (#313), so a window that loses
  # the keyboard can let go of them through the machine's own key path.
  # Both hosts keep this bookkeeping; the page keeps its twin in JS.
  src/held_keys.cpp)
add_library(amberfolio::host_services ALIAS amberfolio-host-services)

target_include_directories(amberfolio-host-services PUBLIC include)

# The generated edition table (#207). PRIVATE: it is what
# src/edition_facts.cpp is built out of, and nothing above this library
# has any business including it — `edition_facts.h` is the surface.
target_include_directories(amberfolio-host-services PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated")

# libdeflate is the extractor's inflate, and it is the first third-party
# library that reaches a shipped host -- cmake/AmberfolioLibdeflate.cmake
# says why a hand-written one was the wrong call. PRIVATE: it is an
# implementation detail of journal_extract.cpp and nothing above this
# library ever names it.
include(AmberfolioLibdeflate)
include(AmberfolioStbImage)

# stb ships its implementation inside the header it declares from, behind
# a macro, and stb_image_impl.cpp is the one translation unit that defines
# it. Its own target, and the only one here that does **not** link
# amberfolio::warnings: our baseline is warnings-as-errors and a
# third-party implementation is not held to it. Everything of ours that
# *calls* it is, which is why this is a target of one file rather than a
# flag on a source of the library.
add_library(amberfolio-stb-image-impl STATIC src/stb_image_impl.cpp)
target_link_libraries(amberfolio-stb-image-impl PUBLIC amberfolio::stb_image)

target_link_libraries(amberfolio-host-services
  PUBLIC amberfolio::core
  PRIVATE amberfolio::warnings libdeflate::libdeflate_static
          amberfolio-stb-image-impl)

# The object's own suite. The *mechanism* it plugs into is core's and is
# tested there (tests/core/machine/seam_test.cpp); this asserts the thing
# the two hosts actually attach — that it takes the call, reads the
# machine at the moment of the call, and writes nothing.
if(AMBERFOLIO_BUILD_TESTS)
  include(AmberfolioGoogleTest)
  include(GoogleTest)

  add_executable(amberfolio-host-services-tests
    tests/host_services_test.cpp
    tests/slot_store_test.cpp
    tests/code_wheel_store_test.cpp
    tests/edition_facts_test.cpp
    tests/journal_facts_test.cpp
    tests/journal_extract_test.cpp
    tests/journal_ocr_test.cpp
    tests/journal_picture_test.cpp
    tests/journal_jpeg_test.cpp
    tests/journal_store_test.cpp
    tests/journal_ingest_test.cpp
    tests/journal_score_test.cpp
    tests/held_keys_test.cpp)
  target_link_libraries(amberfolio-host-services-tests PRIVATE
    amberfolio::core
    amberfolio::host_services
    amberfolio::warnings
    GTest::gmock_main)

  # Where the committed recordings live, for the one case that reads a
  # descriptor: the edition table's file list against the pristine disk
  # `party.session` pins (#207). A compile definition and not a search,
  # for tests/CMakeLists.txt's reason — the sessions are source, and a
  # test that hunted for them could pass by finding nothing.
  target_compile_definitions(amberfolio-host-services-tests PRIVATE
    "AMBERFOLIO_SESSIONS_DIR=\"${PROJECT_SOURCE_DIR}/tests/sessions\"")

  gtest_discover_tests(amberfolio-host-services-tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS unit)
endif()
