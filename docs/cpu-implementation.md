# Implementing an instruction family

This is the playbook for M1's wide phase: sixteen instruction-family
issues (#18–#33), implemented in parallel, against a framework that is
already in the tree. Each of those issues is deliberately terse — it
lists opcodes, vector files and the quirks peculiar to that family, and
defers everything else to this document.

Read this once before you write code. It assumes you know what an 8086
is and nothing whatsoever about this repository.

The short version:

> A family is a new source file under `core/src/cpu/instructions/`, a
> handler per encoding written as thin wiring over the ALU kernel, one
> line per opcode in the dispatch table, one line per vector file in the
> conformance registry — and then the vectors say whether you are right.
> They are the authority, not the Intel manual.

- [1. The architecture in ten minutes](#1-the-architecture-in-ten-minutes)
- [2. The workflow](#2-the-workflow)
- [3. A worked example](#3-a-worked-example)
- [4. The commands](#4-the-commands)
- [5. The exactness rule](#5-the-exactness-rule)
- [6. Log, don't fake](#6-log-dont-fake)
- [7. Debugging a failing vector](#7-debugging-a-failing-vector)
- [8. The pull-request checklist](#8-the-pull-request-checklist)

---

## 1. The architecture in ten minutes

Six headers, and most families use four of them. All of them are
commented at length — this section is the map, not a substitute for
reading the one you are about to call into.

### The register file — `core/include/amberfolio/cpu/registers.h`

`registers` is a plain aggregate: eight 16-bit words, four segment
registers, IP, and the flag word. The enumerations are numbered the way
the *encoding* numbers them, which is what lets a decoded ModRM field
become a register with a cast:

```cpp
enum class reg16 : std::uint8_t { ax, cx, dx, bx, sp, bp, si, di };
enum class reg8  : std::uint8_t { al, cl, dl, bl, ah, ch, dh, bh };
enum class sreg  : std::uint8_t { es, cs, ss, ds };
```

Note `reg8`: 4–7 are the *high* halves. `regs.get(reg8)` and
`regs.set(reg8, v)` do the shifting; there is deliberately no
`std::uint8_t&` into the middle of a word, because that would bake this
host's byte order into the emulated machine's.

`width` is the operand-size distinction in the form an instruction
carries it — `width::byte` or `width::word`. Values are `std::uint16_t`
at both widths, with a byte living in the low eight bits, and
`truncate(w, v)` is what keeps that honest.

The flag word is always *normalized*: the nine defined bits, plus the
hardwired ones (bit 1 and bits 12–15 read back as 1). `flag::normalize`
is the single place that fact lives. Use `regs.load_flags(v)` **only**
for a flag word that came from the program — POPF, IRET, SAHF. Internal
updates go through the ALU kernel, which cannot break the invariant
because it only ever writes bits in `flag::defined`.

### The bus and addresses — `bus.h`, `address.h`

`bus` is an abstract byte-wide memory and a port interface. The
conformance harness implements it over a vector's sparse memory; M2 will
implement it over real RAM and the device map. Your handler never sees
it directly — it goes through `processor`.

`address` is a `{segment, offset}` pair, kept unfolded on purpose:
offsets wrap at 64 KiB *inside* the segment and never carry into the
segment number. `physical_address(seg, off)` folds them and wraps again
at 1 MiB.

### The ALU flag kernel — `alu.h`

**Use it. Never re-derive a flag.** Sixteen parallel families each
working out what overflow means is sixteen chances to be subtly wrong in
a way that surfaces as a wrong branch fifty thousand instructions later.

Every primitive takes the *current* flag word and returns a complete new
one:

```cpp
struct result { std::uint16_t value; std::uint16_t flags; };

alu::result add(width, a, b, flags);   alu::result adc(width, a, b, flags);
alu::result sub(width, a, b, flags);   alu::result sbb(width, a, b, flags);
alu::result inc(width, a, flags);      alu::result dec(width, a, flags);
alu::result neg(width, a, flags);
alu::result bit_and(width, a, b, flags);
alu::result bit_or (width, a, b, flags);
alu::result bit_xor(width, a, b, flags);

std::uint16_t cmp (width, a, b, flags);  // flags only — nothing to write back
std::uint16_t test(width, a, b, flags);
```

Taking the old flags in is the whole trick behind "which flags does this
instruction leave alone": ADC reads CF out of what you pass in, INC and
DEC hand your CF straight back, and TF/IF/DF are carried through by
everything.

Shifts and rotates, MUL/IMUL, DIV/IDIV and the BCD adjusts are **not**
here — their flag rules are peculiar enough to belong with their family
issues (#24–#27). They are not on their own, though: `alu::szp`,
`alu::with_szp` and `flag::with` are the pieces they compose out of, so
the SZP core is still written once. NOT is not here and never will be —
on an 8086 it sets no flags at all.

### The decoder and dispatch — `decoder.h`, `dispatch.h`, `processor.h`

A handler is a bare function pointer:

```cpp
using handler = void (*)(processor&);
```

By the time it is called, `processor::step()` has:

1. consumed every prefix byte into `current().prefixes`;
2. consumed the ModRM byte and its displacement, if the opcode has one
   (`has_modrm(opcode)` is a fixed encoding fact — 68 of the 256 do);
3. computed the effective address into `current().ea`, **including the
   default segment and any override**;
4. looked the opcode up — through the ModRM `reg` field for a group
   opcode — and found you.

What is left is the immediates, and those are yours to fetch:

| You call | You get |
| --- | --- |
| `cpu.read_rm(w)` / `cpu.write_rm(w, v)` | the r/m operand: a register when ModRM says so, memory at `ea` otherwise |
| `cpu.read_reg(w)` / `cpu.write_reg(w, v)` | the reg operand, always a register |
| `cpu.fetch_byte()` / `cpu.fetch_word()` | the next byte/word of the instruction stream; leaves IP where the next instruction begins |
| `cpu.read(w, addr)` / `cpu.write(w, addr, v)` | memory at an address you formed yourself |
| `cpu.regs()` | the register file, flags included |
| `cpu.current()` | what the decoder worked out — prefixes, ModRM, `ea`, `start_ip` |
| `cpu.push_word(v)` / `cpu.pop_word()` | the stack: SP moves first on a push, last on a pop, and wraps in 16 bits |
| `cpu.deliver_interrupt(n)` | the interrupt sequence — see below |

Two things the decoder does *not* do for you, because they are per-family:

- **The `w` and `d` bits.** Most opcodes encode width in bit 0 and
  operand direction in bit 1. There is no helper; your family derives
  them from the opcode, which is usually just a different handler per
  encoding.
- **Addresses you form yourself.** String operations, the stack, and
  the far-pointer loads build addresses out of registers rather than out
  of a ModRM byte. Segment override handling is then yours too — and
  the rules differ per instruction (a string op's DS:SI is overridable,
  its ES:DI is not).

The dispatch table is a value, not a global, so a test can run a
processor against a table of its own — see `tests/core/cpu/test_dispatch.h`.
`instruction_set()` is the one the machine runs.

Group opcodes — where the ModRM `reg` field names the instruction rather
than an operand — are `80 81 82 83 D0 D1 D2 D3 F6 F7 FE FF`, and they
live in a second table indexed `[group_slot(opcode)][reg]`. `8F`, `C6`
and `C7` are *not* groups: the 8086 ignores their reg field rather than
decoding it, so they get one handler each like any other opcode.

### Interrupts — `interrupts.h`

Three families need this and the rest can skip the section.

Everything that interrupts this machine goes through one sequence —
push FLAGS, clear IF and TF, push CS, push IP, load CS:IP from the
vector table entry at `vector * 4` — and that sequence is
`cpu.deliver_interrupt(n)`. **Call it; do not restate it.** INT / INT3 /
INTO (#31) and the divide error (#26) are the callers.

The address it pushes is IP *as you leave it*, which is what gives the
8086 its "return past the instruction" behaviour for a software
interrupt and for a divide error alike — fetch your immediate first and
you get it for nothing. It also ends a halt and abandons a repeated
string instruction that had not retired, so a handler does not have to
think about either.

Three entry points the framework owns and three families call:

| You call | When |
| --- | --- |
| `cpu.deliver_interrupt(n)` | INT n, INT3, INTO (#31); divide error (#26) |
| `cpu.inhibit_interrupts()` | STI (#33); `MOV Sreg, r/m` (#18); `POP Sreg` (#23) — recognition is held off until after the next instruction |
| `cpu.halt()` | HLT (#33). The step loop reports `halted` until an interrupt ends it |

IRET (#31) is the one that goes the other way: `pop_word()` three times,
IP then CS then FLAGS, and the flag word goes through
`regs().load_flags()` because it came from the program.

What you do **not** have to do is arrange for TF, IF, the STI window or
an interrupted REP to behave. That is `processor::step()` and
interrupts.cpp, it is unit-tested in `tests/core/cpu/interrupts_test.cpp`,
and none of it is visible to a handler. Read interrupts.h's header
comment before you touch any of it anyway — the vectors cannot check a
single one of those behaviours, so the comment is the specification.

### The conformance harness — `tests/conformance/`

The oracle is [SingleStepTests/8088](https://github.com/SingleStepTests/8088):
MIT-licensed JSON captured from real silicon, one file per opcode (per
group entry for the group opcodes), around ten thousand cases in each.
It is pinned to a commit, never committed here, and fetched into a cache
outside the source tree by `scripts/fetch-conformance-vectors.py`, which
strips the per-cycle bus trace this emulator has no use for on the way
in.

Four pieces you will touch or read:

- **`registry.cpp`** — the enabled list. One line per stem. A stem not
  in it still registers as a CTest case and reports SKIPPED, which is
  what makes the milestone's remaining work visible in every run.
- **`vectors.h/.cpp`** — the reader. It folds each vector's recorded
  changes onto its before-state at load time, so `test.after` is a
  *complete* register file rather than a list of what was mentioned.
  `ram_after` stays sparse, because memory is too big to fold.
- **`machine.h/.cpp`** — one vector run against the interpreter, and the
  failure report. This is your primary debugging surface; §7 reads its
  output.
- **`conformance_test.cpp`** — registers one CTest case per stem at run
  time. The suite is `conformance`, the case is `op_<stem>` with dots
  turned into underscores: `80.0` becomes `conformance.op_80_0`.

Memory is modelled the way the vectors describe it, which is sparsely.
The suite lists every byte the real part *read*, plus every byte that
changed. A read of an address the vector never mapped is a **failure**,
not a shrug — the whole point of an oracle is that it knows and we do
not.

---

## 2. The workflow

### 0. Get the vectors

Once, ~726 MB fetched and condensed to ~200 MB cached:

```sh
python3 scripts/fetch-conformance-vectors.py
```

While iterating on one family you can take just what you need:

```sh
python3 scripts/fetch-conformance-vectors.py --stems 00 01 80.0
```

### 1. Create the family source file

`core/src/cpu/instructions/<family>.cpp`, starting with

```cpp
// SPDX-License-Identifier: AGPL-3.0-only
```

Nobody else has any reason to open this file, which is the point: it is
where all of your actual work goes.

Then add **one line, sorted**, to the source list in
`core/CMakeLists.txt`:

```cmake
add_library(amberfolio-core STATIC
  src/abi.cpp
  src/cpu/alu.cpp
  src/cpu/decoder.cpp
  src/cpu/dispatch.cpp
  src/cpu/instructions/add.cpp        # <- yours
  src/cpu/processor.cpp
  src/version.cpp)
```

### 2. Declare the handlers

In `core/include/amberfolio/cpu/instructions.h`, in your family's own
block under its own heading. Blocks are ordered the way M1's issues are
(#18 MOV/XCHG, #19 ADD/ADC, #20 SUB/SBB/CMP/NEG, …), which keeps two
families' additions apart in the file even when their opcodes are
adjacent.

`dispatch.cpp` includes this header and nothing else, so the include
list is not a shared line either. **Do not add an include to
`dispatch.cpp`.**

### 3. Implement, against the ALU kernel

Thin wiring. If you find yourself computing a carry, stop and look for
the primitive. If your family genuinely has flag rules of its own
(shifts, MUL, BCD), compose them out of `alu::with_szp` and `flag::with`
rather than restating SZP.

### 4. Wire the dispatch table

`core/src/cpu/dispatch.cpp`, in `build_instruction_set()`. **One line
per opcode, sorted, with the opcode in it.** Not a block, not a loop,
not a helper that fills a range:

```cpp
  t.primary[0x00] = &add_rm8_r8;
  t.primary[0x01] = &add_rm16_r16;
```

and for a group entry:

```cpp
  t.group[group_slot(0x80)][0] = &add_rm8_imm8;
```

This is the file all sixteen pull requests touch, and the one-line rule
is the whole reason they can be worked on at once: two families adding
adjacent opcodes produce a conflict git resolves by keeping both lines,
instead of one a human has to think about. A `for` loop that fills
`00`–`03` saves three lines and costs the next fifteen pull requests a
merge each.

**Leave opcodes you do not implement alone.** A null entry is not a gap
to be tidied up — it is what stops the machine loudly (§6), and it is
how the milestone knows what is left.

### 5. Enable the vector files

`tests/conformance/registry.cpp`, in the `enabled` set. Same rule, same
reason: **one line per stem, sorted**.

```cpp
      "00",
      "01",
      "80.0",
```

A stem belongs here once its family is implemented and its file passes
**in full**. Adding one that does not pass turns the build red, which is
the point.

### 6. Run your family's vectors

```sh
cmake --preset linux-gcc
cmake --build --preset linux-gcc
ctest --preset linux-gcc -R "conformance\.op_(00|01|80_0)$"
```

Iterate here. `AMBERFOLIO_CONFORMANCE_LIMIT` (§4) makes the loop fast
while you are still failing thousands; drop it before you believe the
result.

### 7. Run the full enabled set

```sh
ctest --preset linux-gcc -L conformance
```

No previously enabled stem may regress. This is an explicit acceptance
criterion on every family issue, and it is not hypothetical — a shared
helper that looked like an improvement is exactly how one family breaks
another.

### 8. Format, tidy, guards

```sh
bash scripts/check-format.sh
cmake --preset linux-clang && bash scripts/check-tidy.sh build/linux-clang
bash scripts/check-clean.sh
bash scripts/check-dco.sh
```

### 9. Commit and open the PR

`git commit -s` — every non-merge commit is DCO-signed, and the check
looks at **history**, not just your tip. Reference the issue. Both
acknowledgments in the pull-request template must be present and checked;
CI fails the PR otherwise.

### What a family touches, in full

Five files, and four of them take one sorted line each:

| File | What you add |
| --- | --- |
| `core/src/cpu/instructions/<family>.cpp` | everything — your own file |
| `core/CMakeLists.txt` | one line in the source list, sorted |
| `core/include/amberfolio/cpu/instructions.h` | your own declaration block |
| `core/src/cpu/dispatch.cpp` | one line per opcode, sorted |
| `tests/conformance/registry.cpp` | one line per stem, sorted |

(Plus `tests/CMakeLists.txt` if you add a unit-test source — see §3.)

Anything else you find yourself editing, stop and ask on the issue. It
is not necessarily wrong, but it is the kind of change that wants to be
its own commit with its own reasoning, not a silent passenger in a
family PR.

---

## 3. A worked example

ADD (issue #19), which is close to pure wiring.

`core/src/cpu/instructions/add.cpp`:

```cpp
// SPDX-License-Identifier: AGPL-3.0-only
//
// ADD and ADC (issue #19). All flag effects come from the ALU kernel;
// this file is operand plumbing and nothing else.

#include "amberfolio/cpu/instructions.h"

#include <cstdint>

#include "amberfolio/cpu/alu.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

/// r/m := r/m + reg.
void add_rm_reg(processor& cpu, width w) {
  // Read both operands into locals first. Argument evaluation order is
  // unspecified in C++, and read_rm can touch the bus — the harness
  // compares what the CPU asked memory for, so the order is observable.
  const std::uint16_t dst = cpu.read_rm(w);
  const std::uint16_t src = cpu.read_reg(w);

  const alu::result r = alu::add(w, dst, src, cpu.regs().flags);

  cpu.write_rm(w, r.value);
  // The whole word, not an OR: the kernel returns a complete flag word
  // with everything it does not touch carried through unchanged.
  cpu.regs().flags = r.flags;
}

/// AL/AX := AL/AX + imm. No ModRM byte, so the immediate follows the
/// opcode directly and the handler fetches it.
void add_acc_imm(processor& cpu, width w) {
  const std::uint16_t dst =
      w == width::byte ? std::uint16_t{cpu.regs().get(reg8::al)}
                       : cpu.regs()[reg16::ax];
  const std::uint16_t src =
      w == width::byte ? std::uint16_t{cpu.fetch_byte()} : cpu.fetch_word();

  const alu::result r = alu::add(w, dst, src, cpu.regs().flags);

  if (w == width::byte) {
    cpu.regs().set(reg8::al, static_cast<std::uint8_t>(r.value));
  } else {
    cpu.regs()[reg16::ax] = r.value;
  }
  cpu.regs().flags = r.flags;
}

}  // namespace

void add_rm8_r8(processor& cpu) { add_rm_reg(cpu, width::byte); }
void add_rm16_r16(processor& cpu) { add_rm_reg(cpu, width::word); }
void add_al_imm8(processor& cpu) { add_acc_imm(cpu, width::byte); }
void add_ax_imm16(processor& cpu) { add_acc_imm(cpu, width::word); }

}  // namespace amberfolio::cpu
```

Declared in `instructions.h`:

```cpp
// --- #19: ADD/ADC ----------------------------------------------------

void add_rm8_r8(processor& cpu);
void add_rm16_r16(processor& cpu);
void add_al_imm8(processor& cpu);
void add_ax_imm16(processor& cpu);
```

Wired in `dispatch.cpp`:

```cpp
  t.primary[0x00] = &add_rm8_r8;
  t.primary[0x01] = &add_rm16_r16;
  t.primary[0x04] = &add_al_imm8;
  t.primary[0x05] = &add_ax_imm16;
```

Enabled in `registry.cpp`:

```cpp
      "00",
      "01",
      "04",
      "05",
```

Handler names follow the mnemonic and its Intel-notation operands —
which is also how the vectors' own disassembly reads, so a failing
`test 4211 "add al, 1Bh"` points straight at `add_al_imm8`. The
convention matters less than the one-line rule; the family prefix is
what keeps two families' names from colliding.

### Unit tests

The vectors are the acceptance test, and for most families they are
enough. Add a unit test in `tests/core/cpu/` (and one sorted line to the
`amberfolio-core-tests` source list in `tests/CMakeLists.txt`) when you
want to pin something the vectors cannot reach, or to keep a reduced
repro of a bug you just spent an afternoon on.

`tests/core/cpu/test_bus.h` gives you a flat megabyte of RAM, a port
map, and a record of every access, so a test can assert the CPU touched
the bus the way the part would have — not merely that it produced the
right answer. `test_dispatch.h` gives you tables to run a processor
against when the point is the decoder rather than an instruction.

---

## 4. The commands

Build and test, on any platform (swap the preset):

```sh
cmake --preset linux-gcc          # or windows-msvc, macos, linux-clang
cmake --build --preset linux-gcc
ctest --preset linux-gcc          # unit tests + conformance + host smoke
```

Presets are `windows-msvc`, `macos`, `linux-gcc`, `linux-clang`,
`linux-asan-ubsan` and `wasm`. On Windows, build from a Visual Studio
developer shell and keep the checkout on a short path — the fetched
dependencies nest deeply enough that a long one trips the 260-character
limit while configuring.

Just the conformance binary, which is usually all you want while
iterating:

```sh
cmake --build --preset linux-gcc --target amberfolio-conformance-tests
```

Selecting cases:

```sh
ctest --preset linux-gcc -L unit                    # the fast suite
ctest --preset linux-gcc -L conformance             # every enabled stem
ctest --preset linux-gcc -R "conformance\.op_00$"   # one opcode
ctest --preset linux-gcc -R "conformance\.op_D0_[0-7]$"   # a group
ctest --preset linux-gcc -R "conformance\.op_(00|01|04|05)$"  # a family
```

`--output-on-failure` is already on via the preset, so the report lands
in your terminal.

The vectors:

```sh
python3 scripts/fetch-conformance-vectors.py              # all 323 files
python3 scripts/fetch-conformance-vectors.py --stems 00 01 80.0
python3 scripts/fetch-conformance-vectors.py --print-dir  # where they went
```

Environment knobs:

| Variable | What it does |
| --- | --- |
| `AMBERFOLIO_CONFORMANCE_LIMIT` | run only the first N vectors of each file. CI's matrix jobs use 500 and the sanitizer job 100; the full-suite job caps nothing |
| `AMBERFOLIO_CONFORMANCE_VECTORS` | where the condensed vectors live (default: your per-user cache directory) |
| `AMBERFOLIO_CONFORMANCE_REQUIRED` | missing vectors become a failure instead of a skip — what CI sets, so an un-run suite cannot pass silently |

Quick iterations:

```sh
AMBERFOLIO_CONFORMANCE_LIMIT=200 ctest --preset linux-gcc -R "conformance\.op_00$"
```

In PowerShell:

```powershell
$env:AMBERFOLIO_CONFORMANCE_LIMIT = '200'
ctest --preset windows-msvc -R "conformance\.op_00$"
```

**Drop the limit before you believe a green result.** A family that
passes 200 vectors and fails at 4,000 is the normal shape of a flag bug.

The gates, all five of which CI runs as exactly these scripts:

```sh
bash scripts/check-clean.sh    # content guard — every commit, index, worktree
bash scripts/check-dco.sh      # every non-merge commit signed off
bash scripts/check-format.sh   # clang-format over tracked C++
bash scripts/check-tidy.sh build/linux-clang   # needs a configured tree
bash scripts/check-shell.sh    # shellcheck over scripts/
```

The clang tools are pinned in `.llvm-version`, because they disagree
with themselves across major versions:

```sh
pip install "clang-format==$(cat .llvm-version)" "clang-tidy==$(cat .llvm-version)"
```

To fix formatting rather than be told about it, run `clang-format -i`
over the files the gate names. `check-tidy.sh` only needs a *configured*
tree — `cmake --preset linux-clang` is enough, no build.

The sanitizer preset, worth a run before you push if your family does
anything with memory:

```sh
cmake --preset linux-asan-ubsan
cmake --build --preset linux-asan-ubsan
AMBERFOLIO_CONFORMANCE_LIMIT=100 ctest --preset linux-asan-ubsan
```

---

## 5. The exactness rule

Flags match **bit for bit, undefined behaviour included**. The harness
carries no masks and none will be added — that is a settled M1 decision
(issue #35).

Officially-undefined bits are as binding here as documented ones,
because the vectors come off real silicon and the program we are going
to run came off the same era. Software of the period read undefined
flags, sometimes by accident and sometimes on purpose, and an emulator
that is "right except where Intel said it doesn't matter" is an emulator
that diverges somewhere unreproducible.

**When the manual and the vectors disagree, the vectors win.** Match
them, and add a short comment stating the quirk as fact — facts about
the 8086 are fine; that is not game content, and this repository's
clean-content rule (CONTRIBUTING.md) has never been about facts.

There is a live example of the marker this leaves in `core/src/cpu/alu.cpp`,
on AF after a logical operation: Intel documents it as undefined, the
kernel clears it because that is what an 8086 is reported to do, and the
comment says in as many words that this is the one value in the file not
yet checked against the vectors — naming issue #21 as where it gets
confirmed or corrected. Write that comment when you are in that
position. It is worth more than being quietly right.

Two things that follow:

- **Do not mask a failing bit to get green.** If you cannot make a bit
  match, say so on the issue. A known-wrong bit with a comment is a
  contribution; a hidden one is a liability.
- **Do not enable a stem that does not pass in full.** Skipped is an
  honest state and the milestone's burn-down chart depends on it.

---

## 6. Log, don't fake

PLAN.md §3: an unimplemented instruction, register or port is a loud log
line and a clean stop, never a silently guessed answer.

For you this has one practical consequence, and it is a discipline
rather than a mechanism: **anything outside your family's scope stays on
the unimplemented-stop path.** Never guess a neighbouring opcode into
existence because it looked easy and the table entry was right there. An
opcode with no handler stops the machine with a record naming it
(`stop_reason::unimplemented_opcode`, and the ModRM `reg` field too for
a group opcode, so "FF is unimplemented" says which of five instructions
was wanted). That stop is not a defect to be papered over — it is how
the wide phase knows what is left, and how M1-C1 knows when it is done.

If your family genuinely needs a neighbour implemented, that is a
comment on the issue, not a quiet extra line in the dispatch table.
Somebody else's PR is going to add that line, and git will hand you both
a conflict you did not need.

---

## 7. Debugging a failing vector

### Reading the report

The harness prints up to ten failures per file in full, then counts the
rest — a newly-wired family can fail thousands, and a CI log with ten
thousand register diffs in it is one nobody reads.

A failure looks like this:

```
04 test 4211  "add al, 1Bh"  bytes 04 1B
  FLAGS expected F013 [......A.C]  got F003 [........C]  differ: AF
```

One bit, named. That is the normal shape of a real failure once the
obvious things work: here AL was F5h, the sum carried out of bit 3, and
the handler derived its own flags instead of calling `alu::add`.

Line by line:

- **`00`** — the stem, then the test's `idx` within the file (what the
  suite calls a test by), the suite's own disassembly, and the encoded
  instruction bytes with prefixes included. The disassembly usually
  tells you which handler you are in; the bytes tell you which encoding.
- **A register line** per register that differs, in reading order (AX BX
  CX DX SP BP SI DI CS DS ES SS IP).
- **The FLAGS line** gives both words in hex, both rendered as
  `ODITSZAPC` with a dot for each clear bit, and then names the bits
  that differ. "the flags disagree" is not a diagnosis; "AF" is.
- **A memory line** per byte that differs, up to twelve, then a count of
  the rest. Three shapes, and they mean different things:
  `expected NN  got MM` (wrote the wrong value), `expected NN, and
  nothing was written there` (the vector says the instruction wrote a
  byte and your handler did not), and `written NN, and the vector does
  not account for it` (your handler wrote somewhere it should not have).

Four other reports mean something specific:

```
  stopped at F000:0100 on opcode FF /3 — no handler for it in the dispatch table
```

You enabled a stem whose handler is not wired, or the group entry index
is wrong. The `/3` is the ModRM `reg` field.

```
  the instruction had not retired after 200 iterations
```

A repeated string instruction that never stopped repeating — the
handler kept calling `keep_repeating()` without the count running out.

```
  ports: 2 of 3 transactions were never made
```

The vector scripted port traffic your handler did not do. Traffic in the
wrong order, to the wrong port, or of the wrong value is reported per
transaction instead (`port op 1: expected a read of port 001B, …`).

```
  read of 0123A, which the vector does not map
```

The vectors list every byte the real part fetched, so a non-prefetching
core reads a *subset* of them. Reading outside that set almost always
means an effective address came out wrong — which is a decoding
assumption in your handler, not a bug in the decoder. (Capped at eight
per test; the harness returns FF and carries on so you get the register
diff too.)

### Reproducing one test

There is no per-test selector; the granularity is the file. The loop
that works:

```sh
AMBERFOLIO_CONFORMANCE_LIMIT=50 ctest --preset linux-gcc -R "conformance\.op_00$"
```

Then take the failing case's `bytes` and its before-state into a unit
test in `tests/core/cpu/` using `test_bus.h`, where you can put a
breakpoint on it. That test is often worth keeping.

### The common traps

In roughly the order they bite:

- **Byte-half registers.** `reg8` 4–7 are AH/CH/DH/BH, not more low
  halves. `read_rm(width::byte)` and `read_reg(width::byte)` handle it;
  a handler that reaches for `regs()[static_cast<reg16>(field)]` on a
  byte operand is the bug.
- **The default segment for BP forms.** An effective address built on BP
  defaults to SS, everything else to DS — *except* mod 00 rm 110, which
  is a bare 16-bit address and a DS one. The decoder already does this.
  If you form addresses yourself, you own it.
- **CF preservation on INC/DEC.** INC is not ADD-of-1. `alu::inc` and
  `alu::dec` hand your CF straight back; deriving flags yourself is how
  you lose it.
- **A sparse final state means "unchanged".** The vector files record
  only what changed — but the reader already folds that onto the before
  state, so `test.after` is complete. The trap is in the *other*
  direction: a register you never wrote is expected to still hold its
  before value, and a handler that clobbers something it had no business
  touching fails on a register the instruction has nothing to do with.
- **64 KiB offset wrap.** A word at offset FFFF is the byte at FFFF and
  the byte at 0000 *of the same segment*. `processor::read_word` does
  this; address arithmetic you do yourself must wrap in 16 bits too, and
  must not carry into the segment number.
- **Flag-word assignment.** `cpu.regs().flags = r.flags` — the kernel
  returns a complete word. Don't OR bits in, and don't route internal
  updates through `load_flags` (that is for POPF/IRET/SAHF, and it
  normalizes a program-supplied value).
- **Operand read order.** Evaluate operands into locals before calling
  the kernel. Argument evaluation order is unspecified in C++, and the
  harness compares what the CPU asked memory for.
- **Prefixes on instructions that have no use for them.** The vectors
  prepend random prefixes, including segment overrides on instructions
  with no memory operand. The decoder records and the handler ignores;
  a handler that reacts to `has_segment_override` without needing to
  will fail on vectors that look unrelated.
- **IP after the instruction.** Immediates you fetch advance IP, which
  is what leaves it at the next instruction. A relative jump's target is
  relative to IP *after* the whole instruction — that is, where it sits
  once your handler has fetched its displacement.

---

## 8. The pull-request checklist

Before you open it:

- [ ] Every stem listed on the issue is enabled in `registry.cpp` and
      passes **in full** — no `AMBERFOLIO_CONFORMANCE_LIMIT` set.
- [ ] `ctest --preset linux-gcc -L conformance` is green: no previously
      enabled stem regressed.
- [ ] `ctest --preset linux-gcc -L unit` is green.
- [ ] Only the five files of §2 are touched, and the four shared ones
      only by sorted one-liners.
- [ ] New source files carry `// SPDX-License-Identifier: AGPL-3.0-only`.
- [ ] `check-format.sh`, `check-tidy.sh`, `check-clean.sh`,
      `check-dco.sh` all pass locally.
- [ ] Every non-merge commit is signed off (`git commit -s`; `git rebase
      --signoff` if you forgot — the check reads history, not just your
      tip).
- [ ] The PR body keeps both template acknowledgments, checked.
- [ ] The PR references its issue.

What must be green in CI:

| Job | What it is |
| --- | --- |
| `guards` | the guard self-test, content guard, DCO, clang-format, shellcheck |
| `tidy` | clang-tidy against a configured `linux-clang` tree |
| `vectors` | fetches/caches the condensed vectors per OS; everything below depends on it |
| `conformance (full suite)` | every vector of every enabled file, nothing capped — the milestone's exit criterion |
| `build (×5)` | windows-msvc, macos-universal, linux-gcc, linux-clang, wasm — each building and testing, with the conformance suite capped at 500 vectors per file on the four native rows |
| `sanitizers` | ASan + UBSan over the tests, 100 vectors per file |
| `acknowledgments` | the two checked boxes in the PR template |

The four native build rows matter more than they look: they are how a
family that behaves differently under another compiler gets caught
before it reaches `main`.

One last repo rule, because it saves review time: **style is not a
review topic here.** It is settled by `.clang-format` and `.clang-tidy`.
Don't argue formatting in prose — change the config, in its own PR, with
its own reasoning.
