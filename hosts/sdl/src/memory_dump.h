// SPDX-License-Identifier: AGPL-3.0-only
//
// `--dump-memory`: the machine's memory and the processor's registers
// beside a dump (#619). The instrument a program's facts are found with
// (`docs/seams.md` §8.1): which segment a module is in, where a record
// is, what a byte reads on a given screen.
//
// **What it writes is the player's.** A `.ram` holds the program that was
// running, every byte of it, so it never goes anywhere but where `--dump`
// was told to write (CONTRIBUTING.md, the content rule; #134).

#pragma once

#include <string>

#include "amberfolio/machine/machine.h"

namespace amberfolio::sdl {

/// The registers as one line of `name=HEX` fields, in the order a person
/// reads a far address: the code segment and instruction pointer first,
/// then the other segments, the stack, and the general registers. Ends
/// in a newline.
[[nodiscard]] std::string registers_line(const machine::machine& box);

/// Write `STEM.ram`, the whole address space as the machine holds it (a
/// megabyte, byte N of the file being linear address N), and `STEM.regs`,
/// `registers_line()`. False when either could not be written.
[[nodiscard]] bool write_memory(const std::string& stem,
                                const machine::machine& box);

}  // namespace amberfolio::sdl
