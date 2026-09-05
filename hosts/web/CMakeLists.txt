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

add_executable(amberfolio-web src/main.cpp src/demo_program.cpp)

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

# amberfolio::programs is tests/programs's `assembler` — src/demo_program.cpp
# uses it to build the M2-H2 (#55) dev-page demo program at compile time
# (see that file's own top comment for the reasoning and for where #56's
# canonical composite program is meant to slot in). It builds under
# Emscripten and links nothing but the core (tests/programs/CMakeLists.txt),
# so this adds nothing to the module's dependency footprint beyond the
# assembler's own handful of source files.
target_link_libraries(amberfolio-web PRIVATE
  amberfolio::core
  # The `seam_host_services` both hosts attach (M5-D1, #169). One object,
  # in hosts/common, so a callout means the same thing in a browser as on
  # a desktop; it links only the core, so it adds nothing to the module's
  # dependency footprint.
  amberfolio::host_services
  amberfolio::programs
  amberfolio::warnings)

# The export list is the ABI's guest list, and it lives on this side of the
# boundary on purpose: core/include/amberfolio/abi.h says what may be
# called, the host says what a browser can reach. Every name here must
# survive the link — see the comment in src/main.cpp.
#
# M2-F4 (#45) grew this from two names to the whole platform interface.
# The failure mode that introduces is quiet: a function added to abi.h and
# not added here is simply absent from the module, and nothing in the
# build says so. tests/smoke.mjs therefore asserts the list, so the guard
# is a red test rather than a puzzled host author.
#
# _malloc and _free are on the list deliberately. A JS host needs a buffer
# in linear memory to pull audio into and to hand a program image out of;
# giving it the allocator is how it makes one, and it frees its own —
# which is each side keeping its own, not ownership crossing the boundary
# (abi.h's rule).
#
# A list joined into the flag rather than one long quoted string: a
# backslash-continued string would put the guest list one escaping
# question away from being wrong, and a list is what this is.
set(_amberfolio_web_export_names
  _main
  _malloc
  _free
  _af_version
  _af_ticks_per_second
  _af_frame_width
  _af_frame_height
  _af_palette_entries
  _af_machine_create
  _af_machine_destroy
  _af_machine_attach_reference_devices
  _af_machine_reset
  _af_machine_run_until
  _af_machine_time
  _af_machine_steps
  _af_machine_stopped
  _af_machine_stop_reason
  _af_machine_set_speed
  _af_machine_set_trace
  _af_machine_stop_report
  _af_machine_trace_report
  _af_machine_framebuffer
  _af_machine_palette
  _af_machine_frame_generation
  _af_machine_render_audio
  _af_machine_audio_underruns
  _af_machine_audio_resyncs
  _af_machine_audio_log_edges
  _af_machine_audio_logging_edges
  _af_machine_audio_read_edges
  _af_machine_audio_edges_dropped
  _af_machine_audio_edges_pending
  _af_machine_post_key
  _af_machine_set_wall_clock
  _af_machine_read_log
  _af_machine_log_pending
  _af_machine_log_dropped
  _af_machine_clear_log
  _af_machine_read_console
  _af_machine_console_pending
  _af_machine_console_dropped
  _af_machine_vfs_clear
  _af_machine_vfs_put
  _af_machine_vfs_count
  _af_machine_vfs_name_at
  _af_machine_vfs_path_at
  _af_machine_vfs_size_at
  _af_machine_vfs_size
  _af_machine_vfs_get
  _af_machine_vfs_remove
  _af_machine_vfs_bytes_used
  _af_machine_vfs_generation
  _af_machine_vfs_fingerprint
  _af_machine_load_from_vfs
  _af_machine_load_error
  _af_machine_edition
  _af_machine_program_fingerprint
  _af_machine_seam_count
  _af_machine_seam_id
  _af_machine_seam_about
  _af_machine_seam_state
  _af_machine_seam_reason
  _af_machine_seam_reading
  _af_machine_seam_armed
  _af_machine_seam_fired
  _af_machine_seam_enable
  _af_machine_seam_disable
  _af_machine_seam_triggered
  _af_machine_seam_waiting
  _af_machine_seam_reached
  _af_machine_seam_waited
  _af_machine_seam_pulled_at
  _af_machine_seam_pull
  _af_machine_seam_host_calls
  _af_machine_seam_host_argument
  _af_machine_seam_gate
  _af_machine_present_document
  _af_machine_document_count
  _af_machine_document_name_at
  _af_machine_code_wheel_answered
  _af_machine_set_code_wheel_answered
  _af_machine_write_memory
  _af_machine_read_memory
  _af_machine_set_entry
  _af_machine_state_hash
  _af_machine_verify_recording
  # Web-host-specific, not part of core/include/amberfolio/abi.h: the
  # M2-H2 (#55) embedded demo program, assembled at build time from
  # src/demo_program.cpp and handed out the same "pointer into linear
  # memory plus a length" way abi.h's own buffers are (see main.cpp), and
  # the M4-F4 (#98) seam probe — tests/programs' program and its seam,
  # staged for the node smoke check to toggle through the ABI.
  _af_web_demo_program_bytes
  _af_web_demo_program_size
  _af_web_probe_program_bytes
  _af_web_probe_program_size
  _af_web_probe_seam_register
  # The M5-D1 (#169) host-service door: attaching this module's
  # `seam_host_services` to a machine is a *host's* decision, so it is a
  # host export rather than part of af_machine_attach_reference_devices.
  # The tick beside it is this implementation's own — the count and the
  # argument are core's, above.
  _af_web_attach_host_services
  _af_web_host_service_at
  _af_web_automap_store
  # The M5-E3 (#174) journal ingestion. The loop is inverted here --
  # tesseract.js is on the far side of this boundary and is
  # asynchronous, so the page asks for one entry's pixels, recognizes
  # them, and hands the text back (src/main.cpp says so at length). The
  # last five are the synthetic probe document, which is test apparatus
  # and is a host export for the same reason the probe seam is one.
  #
  # The `encoding`/`encoded`/`region` names are M5-E3a's (#212): an entry
  # whose filter this module does not decode reaches the page as the
  # stream's own bytes and a rectangle, and the page's engine reads the
  # page and keeps the words inside it. `part_count` and the index every
  # accessor takes are M5-E3b's (#214): an entry that flows between
  # columns is more than one of them.
  _af_web_journal_ingest
  _af_web_journal_trouble_name
  _af_web_journal_fingerprint
  _af_web_journal_edition_name
  _af_web_journal_entry_count
  _af_web_journal_entry_citation
  _af_web_journal_extract
  _af_web_journal_image_bytes
  _af_web_journal_image_width
  _af_web_journal_image_height
  _af_web_journal_encoding
  _af_web_journal_part_count
  _af_web_journal_encoded_bytes
  _af_web_journal_encoded_size
  _af_web_journal_region_left
  _af_web_journal_region_top
  _af_web_journal_region_width
  _af_web_journal_region_height
  _af_web_journal_set_text
  _af_web_journal_correct
  _af_web_journal_text
  _af_web_journal_set_engine
  _af_web_journal_store_size
  _af_web_journal_store_recognized
  _af_web_journal_store_corrections
  _af_web_journal_store_write
  _af_web_journal_store_read
  _af_web_journal_store_changed
  _af_web_journal_store_clear_changed
  _af_web_journal_seen_restore
  _af_web_journal_store_clear
  _af_web_journal_store_fingerprint
  # The code wheel's answer, kept between visits (M6-C1b, #292): the
  # journal store's four doors, one artifact over, plus the lookup that
  # tells a machine what this tab remembers about the copy it is
  # running.
  _af_web_code_wheel_store_write
  _af_web_code_wheel_store_read
  _af_web_code_wheel_store_changed
  _af_web_code_wheel_store_clear_changed
  _af_web_code_wheel_store_clear
  _af_web_code_wheel_apply
  _af_web_journal_probe
  _af_web_journal_probe_bytes
  _af_web_journal_probe_size
  _af_web_journal_probe_text
  _af_web_journal_probe_hash)
list(JOIN _amberfolio_web_export_names "," AMBERFOLIO_WEB_EXPORTS)

target_link_options(amberfolio-web PRIVATE
  # A factory function instead of a module that grabs the global scope,
  # exported as ESM so index.html and the node check import it the same
  # way. ENVIRONMENT trims the glue to the two we actually load in.
  "-sMODULARIZE=1"
  "-sEXPORT_ES6=1"
  "-sENVIRONMENT=web,node"
  "-sEXPORTED_FUNCTIONS=${AMBERFOLIO_WEB_EXPORTS}"
  # The typed-array views over linear memory. A JS host reads the
  # framebuffer through HEAPU8 and writes sample buffers through HEAPF32,
  # and recent Emscripten only attaches them to the module object when
  # they are asked for by name. Note the trap abi.h states: a view is
  # detached when memory grows, so a host re-derives it rather than
  # caching one.
  "-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF32"
  # The reference device set (core/src/abi.cpp's `reference_devices`,
  # attached through af_machine_attach_reference_devices()) adds the
  # EGA's 256 KiB of planar VRAM and the in-memory VFS's 8 MiB on top of
  # the machine's own megabyte, all static storage. Emscripten's default
  # initial heap is smaller than that; growth is what tests/programs'
  # own amberfolio-bench already asks for on this toolchain, for the same
  # reason (its CMakeLists.txt says so).
  #
  # M3-F2 (#84) is what puts a player's whole directory in that 8 MiB —
  # a hundred and twenty-odd files sharing one arena rather than each
  # holding a fixed 256 KiB, which is why the number did not have to grow
  # with the file count (core/include/amberfolio/machine/memory_vfs.h).
  "-sALLOW_MEMORY_GROWTH=1")

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
#
# They land flat, in one directory, whatever subdirectory of the source
# tree they came from — which is why `tools/drive.mjs` and
# `tests/smoke.mjs` both import `./host.mjs` and not a path with a `..`
# in it. The served directory is the layout, and the source tree only
# says what each file is for.
set(_amberfolio_web_files
  "${_amberfolio_web_index}"
  "${CMAKE_CURRENT_SOURCE_DIR}/page/host.mjs"
  "${CMAKE_CURRENT_SOURCE_DIR}/page/app.mjs"
  "${CMAKE_CURRENT_SOURCE_DIR}/page/audio-worklet.mjs"
  "${CMAKE_CURRENT_SOURCE_DIR}/page/picker.mjs"
  "${CMAKE_CURRENT_SOURCE_DIR}/page/journal.mjs"
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/drive.mjs"
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
  # The committed sessions come in by path rather than being copied
  # beside the module: they are source, and a check that read a stale
  # copy of a golden would be the one failure this whole apparatus exists
  # to make impossible (tests/sessions/README.md).
  add_test(NAME wasm-module-smoke
    COMMAND "${AMBERFOLIO_NODE}"
      "${AMBERFOLIO_WEB_OUTPUT_DIR}/smoke.mjs"
      --expect "${PROJECT_VERSION}"
      --sessions "${PROJECT_SOURCE_DIR}/tests/sessions")
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

unset(_amberfolio_web_export_names)
unset(_amberfolio_web_index)
unset(_amberfolio_web_files)
unset(_amberfolio_web_copies)
unset(_amberfolio_web_file)
unset(_amberfolio_web_name)
unset(_amberfolio_web_copy)
