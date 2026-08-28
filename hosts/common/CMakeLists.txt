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
  src/automap_store.cpp)
add_library(amberfolio::host_services ALIAS amberfolio-host-services)

target_include_directories(amberfolio-host-services PUBLIC include)

target_link_libraries(amberfolio-host-services
  PUBLIC amberfolio::core
  PRIVATE amberfolio::warnings)

# The object's own suite. The *mechanism* it plugs into is core's and is
# tested there (tests/core/machine/seam_test.cpp); this asserts the thing
# the two hosts actually attach — that it takes the call, reads the
# machine at the moment of the call, and writes nothing.
if(AMBERFOLIO_BUILD_TESTS)
  include(AmberfolioGoogleTest)
  include(GoogleTest)

  add_executable(amberfolio-host-services-tests
    tests/host_services_test.cpp
    tests/automap_store_test.cpp)
  target_link_libraries(amberfolio-host-services-tests PRIVATE
    amberfolio::core
    amberfolio::host_services
    amberfolio::warnings
    GTest::gmock_main)

  gtest_discover_tests(amberfolio-host-services-tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS unit)
endif()
