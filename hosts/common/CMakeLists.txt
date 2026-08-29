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

add_library(amberfolio-host-services STATIC
  src/host_services.cpp
  src/automap_store.cpp
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
  src/journal_probe.cpp)
add_library(amberfolio::host_services ALIAS amberfolio-host-services)

target_include_directories(amberfolio-host-services PUBLIC include)

# libdeflate is the extractor's inflate, and it is the first third-party
# library that reaches a shipped host -- cmake/AmberfolioLibdeflate.cmake
# says why a hand-written one was the wrong call. PRIVATE: it is an
# implementation detail of journal_extract.cpp and nothing above this
# library ever names it.
include(AmberfolioLibdeflate)

target_link_libraries(amberfolio-host-services
  PUBLIC amberfolio::core
  PRIVATE amberfolio::warnings libdeflate::libdeflate_static)

# The object's own suite. The *mechanism* it plugs into is core's and is
# tested there (tests/core/machine/seam_test.cpp); this asserts the thing
# the two hosts actually attach — that it takes the call, reads the
# machine at the moment of the call, and writes nothing.
if(AMBERFOLIO_BUILD_TESTS)
  include(AmberfolioGoogleTest)
  include(GoogleTest)

  add_executable(amberfolio-host-services-tests
    tests/host_services_test.cpp
    tests/automap_store_test.cpp
    tests/journal_facts_test.cpp
    tests/journal_extract_test.cpp
    tests/journal_store_test.cpp
    tests/journal_ingest_test.cpp)
  target_link_libraries(amberfolio-host-services-tests PRIVATE
    amberfolio::core
    amberfolio::host_services
    amberfolio::warnings
    GTest::gmock_main)

  gtest_discover_tests(amberfolio-host-services-tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS unit)
endif()
