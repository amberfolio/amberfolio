# SPDX-License-Identifier: AGPL-3.0-only
#
# The WebAssembly host — the core compiled to wasm32 plus the hand-written
# JS that drives it (PLAN.md §4). Only reachable through the Emscripten
# toolchain; the top-level CMakeLists refuses the option otherwise.
#
# The build tree layout this produces is also the layout the page is
# served from: the module, its glue, and the JS all end up in one
# directory, and scripts/serve-web.py serves exactly that directory. It
# exists rather than a bare `python -m http.server` because the MIME type
# a browser needs for an ES module is not the one Windows reports.

add_executable(amberfolio-web src/main.cpp)

# The one directory the module, its glue, the JS and the page share — the
# thing that gets served. Spelled out rather than left to the generator's
# default because the copy rules below need to name it, and a custom
# command's OUTPUT may not contain a target-dependent generator
# expression. $<CONFIG> is fine there, and a genex in this property is
# also what stops a multi-config generator appending the config twice.
set(AMBERFOLIO_WEB_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")

# amberfolio.mjs, not amberfolio.js: EXPORT_ES6 emits an ES module, and
# node only treats a .js file as one when a package.json says so.
set_target_properties(amberfolio-web PROPERTIES
  OUTPUT_NAME amberfolio
  SUFFIX ".mjs"
  RUNTIME_OUTPUT_DIRECTORY "${AMBERFOLIO_WEB_OUTPUT_DIR}")

target_link_libraries(amberfolio-web PRIVATE
  amberfolio::core
  amberfolio::warnings)

target_link_options(amberfolio-web PRIVATE
  # A factory function instead of a module that grabs the global scope,
  # exported as ESM so index.html and the node check import it the same
  # way. ENVIRONMENT trims the glue to the two we actually load in.
  "-sMODULARIZE=1"
  "-sEXPORT_ES6=1"
  "-sENVIRONMENT=web,node"
  # The export list is the ABI's guest list, and it lives on this side of
  # the boundary on purpose: core/include/amberfolio/abi.h says what may
  # be called, the host says what a browser can reach. Every name here
  # must survive the link — see the comment in src/main.cpp.
  "-sEXPORTED_FUNCTIONS=_main,_af_version")

# The page is generated rather than copied: it carries the AGPL-3.0-only
# §13 source offer, which has to name the commit this build came from,
# and only the thing doing the building knows that (AMBERFOLIO_SOURCE_URL
# in the top-level CMakeLists). @ONLY because the page's script is full of
# JS ${...} that CMake must keep its hands off.
#
# Configured into a private directory rather than straight into the served
# one because configure_file runs at configure time and so cannot write
# through the $<CONFIG> genex in AMBERFOLIO_WEB_OUTPUT_DIR; the copy rules
# below carry it the last step, same as the hand-written files.
set(_amberfolio_web_index "${CMAKE_CURRENT_BINARY_DIR}/generated/index.html")
configure_file(page/index.html.in "${_amberfolio_web_index}" @ONLY)

# The JS and the page are not compiled, they are placed: they have to sit
# beside the generated glue to import it. Listed one by one rather than
# globbed so a new file is a deliberate act.
set(_amberfolio_web_files
  "${_amberfolio_web_index}"
  "${CMAKE_CURRENT_SOURCE_DIR}/page/host.mjs"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.mjs")

# A file-level rule per copy, rather than one POST_BUILD command on the
# module: POST_BUILD only runs when the module relinks, so editing the JS
# alone — the common case once M2 starts — would leave the build tree
# stale.
set(_amberfolio_web_copies "")
foreach(_amberfolio_web_file IN LISTS _amberfolio_web_files)
  get_filename_component(_amberfolio_web_name "${_amberfolio_web_file}" NAME)
  set(_amberfolio_web_copy
    "${AMBERFOLIO_WEB_OUTPUT_DIR}/${_amberfolio_web_name}")
  add_custom_command(OUTPUT "${_amberfolio_web_copy}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${_amberfolio_web_file}" "${_amberfolio_web_copy}"
    DEPENDS "${_amberfolio_web_file}"
    COMMENT "Placing ${_amberfolio_web_name} beside the wasm module"
    VERBATIM)
  list(APPEND _amberfolio_web_copies "${_amberfolio_web_copy}")
endforeach()

add_custom_target(amberfolio-web-page DEPENDS ${_amberfolio_web_copies})
add_dependencies(amberfolio-web amberfolio-web-page)

# The module smoke check (issue #2's acceptance, and the wasm leg of the CI
# matrix). It asserts the ABI answers with the version CMake built, not
# merely that the module instantiated.
find_program(AMBERFOLIO_NODE NAMES node nodejs
  DOC "node executable used to run the wasm module's headless smoke check")

if(AMBERFOLIO_NODE)
  add_test(NAME wasm-module-smoke
    COMMAND "${AMBERFOLIO_NODE}"
      "${AMBERFOLIO_WEB_OUTPUT_DIR}/smoke.mjs"
      --expect "${PROJECT_VERSION}")
  set_tests_properties(wasm-module-smoke PROPERTIES LABELS smoke)
else()
  # Loud and unregistered rather than quietly skipped: the test presets set
  # noTestsAction=error, so a CI box without node fails instead of passing
  # an empty suite.
  message(WARNING
    "node was not found, so the wasm module smoke check is not registered. "
    "Set AMBERFOLIO_NODE to point at one (emsdk ships a node under "
    "$ENV{EMSDK}/node).")
endif()

unset(_amberfolio_web_index)
unset(_amberfolio_web_files)
unset(_amberfolio_web_copies)
unset(_amberfolio_web_file)
unset(_amberfolio_web_name)
unset(_amberfolio_web_copy)
