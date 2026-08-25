# SPDX-License-Identifier: AGPL-3.0-only
#
# The VFS door (M5-D2, #170) over a real directory on a real disk.
#
# `af_machine_vfs_*` reaches an in-memory filesystem a browser handed the
# machine one file at a time; the wasm smoke check drives it there. This
# is the same three operations — walk the tree, read a file back whole,
# take one away — over `directory_vfs`, which is files on the player's
# own disk, so the pair can be compared rather than described.
#
# Its own script rather than a bare add_test for the reason
# run-smoke-program.cmake is: what is being asserted is what the run
# *said* and what it left on the disk, and CTest can only assert an exit
# code.
#
# The file it reads back holds "abc", so the digest it must print is the
# one FIPS 180-4 appendix B.1 gives — the same answer every other SHA-256
# in the world gives, which is the entire point of a fingerprint. Nothing
# here comes from anywhere but this repository (PLAN.md §6).

if(NOT HOST OR NOT DISK)
  message(FATAL_ERROR "run-vfs-door.cmake needs -DHOST= and -DDISK=")
endif()

set(save_dir "${DISK}/SAVE")
set(save_file "${save_dir}/SAVE1.DAT")
file(MAKE_DIRECTORY "${save_dir}")
file(WRITE "${save_file}" "abc")

# Either spelling of the separator, on purpose: what a person types is
# not what DOS writes, and deciding which is which is core's job and not
# this host's (machine/vfs.h's `canonicalize_host_path`).
execute_process(
  COMMAND "${HOST}" "${DISK}" HELLO.EXE --headless
    --vfs-list --vfs-get "SAVE/SAVE1.DAT" --vfs-remove "save\\save1.dat"
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

if(NOT code EQUAL 7)
  message(FATAL_ERROR
    "the program exits with code 7; the host returned '${code}'.\n"
    "stdout: ${out}\nstderr: ${err}")
endif()

# The listing walks the tree: the program at the root and the save one
# directory down, each at its own path. Before #170 the second was
# unreachable from outside the machine entirely.
foreach(expected
    "vfs \\\\HELLO.EXE"
    "vfs \\\\SAVE\\\\SAVE1.DAT 3"
    "vfs \\\\SAVE\\\\SAVE1.DAT 3 sha256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    "vfs deleting \\\\SAVE\\\\SAVE1.DAT"
    "vfs \\\\SAVE\\\\SAVE1.DAT deleted")
  if(NOT err MATCHES "${expected}")
    message(FATAL_ERROR
      "the host never said '${expected}'.\nstdout: ${out}\nstderr: ${err}")
  endif()
endforeach()

# And the delete was a delete: the file is gone from the player's disk,
# and the directory it was in is not — which is the decision abi.h
# records, made visible where it is least deniable.
if(EXISTS "${save_file}")
  message(FATAL_ERROR "--vfs-remove left ${save_file} on the disk")
endif()
if(NOT IS_DIRECTORY "${save_dir}")
  message(FATAL_ERROR
    "--vfs-remove took ${save_dir} away too; it removes a file and only a"
    " file")
endif()

message(STATUS
  "sdl host vfs: the tree listed, a save read back by its digest, and a"
  " real file deleted")
