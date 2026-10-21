#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Write the fanfare seam's score table from the composer's write list.

The fanfare (``core/src/machine/seam_fanfare.cpp``, #538) is a piece
written for this project for the Tandy 1000's sound chip.  Its source is a
list of writes to the chip's port, one a line, ``TICK BYTE``: the PIT tick
(1,193,182 a second) from the piece's start and the byte written, both in
decimal, in order, with a last line ``end TICK``.  This turns that list
into ``core/src/machine/fanfare_score.cpp``:

    python3 scripts/fanfare-score.py SCORE.txt > core/src/machine/fanfare_score.cpp
    clang-format -i core/src/machine/fanfare_score.cpp   # lays the table out

The list is checked as it is read: ticks in order, bytes that are bytes,
and an end at or after the last write.
"""

import sys

HEADER = """\
// SPDX-License-Identifier: AGPL-3.0-only
//
// The fanfare seam's score (seam_fanfare.cpp, #538): every byte the piece
// writes to the Tandy sound chip's port, and the PIT tick from the piece's
// start it is written at. The piece was written for this project, for
// this seam; nothing in it comes from the program.
//
// Written by scripts/fanfare-score.py from the composer's list of writes.
// Edit that list and run the script again rather than editing this table.

#include "fanfare_score.h"

#include <array>
#include <span>

namespace amberfolio::machine {
namespace {

"""

FOOTER = """\
}}}};

}}  // namespace

std::span<const chip_cue> fanfare_score() noexcept {{ return cues; }}

}}  // namespace amberfolio::machine
"""


def read(path):
    writes = []
    end = None
    with open(path, encoding="ascii") as f:
        for number, line in enumerate(f, 1):
            fields = line.split()
            if not fields:
                continue
            if fields[0] == "end":
                end = int(fields[1])
                break
            tick, value = int(fields[0]), int(fields[1])
            if not 0 <= value <= 0xFF:
                sys.exit(f"{path}:{number}: {value} is not a byte")
            if writes and tick < writes[-1][0]:
                sys.exit(f"{path}:{number}: tick {tick} is before the last")
            writes.append((tick, value))
    if end is None or (writes and end < writes[-1][0]):
        sys.exit(f"{path}: no end line at or after the last write")
    return writes, end


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: fanfare-score.py SCORE.txt")
    writes, _ = read(sys.argv[1])
    out = [HEADER]
    out.append(f"constexpr std::array<chip_cue, {len(writes)}> cues{{{{\n")
    out.append(",\n".join(f"    {{{tick}, 0x{value:02X}}}"
                          for tick, value in writes))
    out.append("\n")
    out.append(FOOTER.format())
    # Through the buffer: a text stdout on Windows ends every line CRLF.
    sys.stdout.buffer.write("".join(out).encode("ascii"))


if __name__ == "__main__":
    main()
