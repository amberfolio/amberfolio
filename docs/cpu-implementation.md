# Implementing an instruction family

The architecture tour and the house style for touching CPU code. All
sixteen families are in and all 323 vector files pass; this is the guide
for changing any of it.

> A family is a source file under `core/src/cpu/instructions/`, a handler
> per encoding written as thin wiring over the ALU kernel, and one line
> per opcode in the dispatch table. The vectors say whether you are
> right. They are the authority, not the Intel manual.

## 1. The architecture in ten minutes

### The register file: `core/include/amberfolio/cpu/registers.h`

`registers` is a plain aggregate: eight 16-bit words, four segment
registers, IP, and the flag word. The enumerations are numbered the way
the encoding numbers them, so a decoded ModRM field becomes a register
with a cast:

```cpp
enum class reg16 : std::uint8_t { ax, cx, dx, bx, sp, bp, si, di };
enum class reg8  : std::uint8_t { al, cl, dl, bl, ah, ch, dh, bh };
enum class sreg  : std::uint8_t { es, cs, ss, ds };
```

`reg8` 4–7 are the *high* halves; `regs.get(reg8)` and `regs.set(reg8,
v)` do the shifting. `width` is `width::byte` or `width::word`; values
are `std::uint16_t` at both widths and `truncate(w, v)` keeps that
honest. The flag word is always *normalized* (`flag::normalize`: nine
defined bits, bit 1 and bits 12–15 read as 1). Use `regs.load_flags(v)`
**only** for a flag word from the program (POPF, IRET, SAHF); internal
updates go through the ALU kernel.

### The bus and addresses: `bus.h`, `address.h`

`bus` is an abstract byte-wide memory and port interface; handlers go
through `processor`. `address` is a `{segment, offset}` pair kept
unfolded: offsets wrap at 64 KiB inside the segment.
`physical_address(seg, off)` folds and wraps at 1 MiB.

### The ALU flag kernel: `alu.h`

**Use it. Never re-derive a flag.** Every primitive takes the current
flag word and returns a complete new one:

```cpp
struct result { std::uint16_t value; std::uint16_t flags; };

alu::result add(width, a, b, flags);   alu::result adc(width, a, b, flags);
alu::result sub(width, a, b, flags);   alu::result sbb(width, a, b, flags);
alu::result inc(width, a, flags);      alu::result dec(width, a, flags);
alu::result neg(width, a, flags);
alu::result bit_and(width, a, b, flags);
alu::result bit_or (width, a, b, flags);
alu::result bit_xor(width, a, b, flags);

std::uint16_t cmp (width, a, b, flags);  // flags only
std::uint16_t test(width, a, b, flags);
```

ADC reads CF from what you pass in, INC and DEC hand CF back, TF/IF/DF
carry through. Shifts, rotates, MUL/IMUL, DIV/IDIV and the BCD adjusts
live with their families and compose out of `alu::szp`, `alu::with_szp`
and `flag::with`. NOT sets no flags.

### The decoder and dispatch: `decoder.h`, `dispatch.h`, `processor.h`

A handler is `using handler = void (*)(processor&);`. By the time it is
called, `processor::step()` has consumed every prefix into
`current().prefixes`, consumed the ModRM byte and displacement if the
opcode has one (`has_modrm(opcode)`: 68 of 256), computed the effective
address into `current().ea` including the default segment and any
override, and looked the opcode up (through the ModRM `reg` field for a
group). Immediates are yours to fetch.

| You call | You get |
| --- | --- |
| `cpu.read_rm(w)` / `cpu.write_rm(w, v)` | the r/m operand: a register when ModRM says so, memory at `ea` otherwise |
| `cpu.read_reg(w)` / `cpu.write_reg(w, v)` | the reg operand |
| `cpu.fetch_byte()` / `cpu.fetch_word()` | the next byte/word of the instruction stream; leaves IP at the next instruction |
| `cpu.read(w, addr)` / `cpu.write(w, addr, v)` | memory at an address you formed |
| `cpu.regs()` | the register file, flags included |
| `cpu.current()` | prefixes, ModRM, `ea`, `start_ip` |
| `cpu.push_word(v)` / `cpu.pop_word()` | the stack: SP moves first on a push, last on a pop, wraps in 16 bits |
| `cpu.deliver_interrupt(n)` | the interrupt sequence |

The decoder does not derive the `w` and `d` bits (one handler per
encoding instead), and does not form addresses for string, stack or
far-pointer instructions; segment override rules there are per
instruction (a string op's DS:SI is overridable, its ES:DI is not).

The dispatch table is a value (`instruction_set()` is the one the
machine runs; `tests/core/cpu/test_dispatch.h` builds others). Group
opcodes `80 81 82 83 D0 D1 D2 D3 F6 F7 FE FF` live in a second table
indexed `[group_slot(opcode)][reg]`. `8F`, `C6` and `C7` are not groups.

### Interrupts: `interrupts.h`

One sequence for everything: push FLAGS, clear IF and TF, push CS, push
IP, load CS:IP from `vector * 4`. It is `cpu.deliver_interrupt(n)`; call
it, do not restate it. The pushed IP is IP as you leave it, so fetch your
immediate first. It also ends a halt and abandons an unretired REP.

| You call | When |
| --- | --- |
| `cpu.deliver_interrupt(n)` | INT n, INT3, INTO; divide error |
| `cpu.inhibit_interrupts()` | STI; `MOV Sreg, r/m`; `POP Sreg` (recognition held off one instruction) |
| `cpu.halt()` | HLT; the step loop reports `halted` until an interrupt ends it |

IRET pops IP, CS, FLAGS and routes the flag word through
`regs().load_flags()`. TF, IF, the STI window and an interrupted REP are
`processor::step()`'s and `interrupts.cpp`'s, tested in
`tests/core/cpu/interrupts_test.cpp`; the vectors cannot check any of
them, so `interrupts.h`'s header comment is the specification.

### The conformance harness: `tests/conformance/`

The oracle is [SingleStepTests/8088](https://github.com/SingleStepTests/8088),
MIT-licensed JSON from real silicon, one file per opcode (per group entry
for groups), about ten thousand cases each, pinned to a commit, fetched
into a cache outside the tree by `scripts/fetch-conformance-vectors.py`.

- `vector-files.txt` / `registry.cpp`: the manifest, generated at
  configure time and length-checked against the pin.
- `vectors.h/.cpp`: the reader; folds recorded changes onto the before
  state so `test.after` is complete. `ram_after` stays sparse.
- `machine.h/.cpp`: one vector against the interpreter, and the failure
  report (§7).
- `conformance_test.cpp`: one CTest case per stem, `conformance.op_<stem>`
  with dots as underscores (`80.0` is `conformance.op_80_0`).

Memory is sparse: every byte the real part read plus every byte that
changed. A read of an unmapped address is a **failure**.

## 2. The workflow

### 0. Get the vectors

```sh
python3 scripts/fetch-conformance-vectors.py                 # ~726 MB fetched, ~200 MB cached
python3 scripts/fetch-conformance-vectors.py --stems 00 01 80.0
```

### 1. Create the family source file

`core/src/cpu/instructions/<family>.cpp`, starting with
`// SPDX-License-Identifier: AGPL-3.0-only`, plus one sorted line in the
source list in `core/CMakeLists.txt`.

### 2. Declare the handlers

In `core/include/amberfolio/cpu/instructions.h`, in the family's own
block under its own heading. `dispatch.cpp` includes this header and
nothing else; **do not add an include to `dispatch.cpp`.**

### 3. Implement, against the ALU kernel

Thin wiring. If you are computing a carry, look for the primitive. A
family with flag rules of its own composes them out of `alu::with_szp`
and `flag::with`.

### 4. Wire the dispatch table

`core/src/cpu/dispatch.cpp`, in `build_instruction_set()`. **One line
per opcode, sorted, with the opcode in it.** No loops, no range fillers:

```cpp
  t.primary[0x00] = &add_rm8_r8;
  t.primary[0x01] = &add_rm16_r16;
  t.group[group_slot(0x80)][0] = &add_rm8_imm8;
```

**Leave opcodes you do not implement alone.** A null entry stops the
machine loudly (§6).

### 5. The vector files

Nothing to enable: every stem the pin has is registered and expected to
pass.

### 6. Run your family's vectors

```sh
cmake --preset linux-gcc
cmake --build --preset linux-gcc
ctest --preset linux-gcc -R "conformance\.op_(00|01|80_0)$"
```

`AMBERFOLIO_CONFORMANCE_LIMIT` (§4) makes the loop fast; drop it before
you believe the result.

### 7. Run the whole suite

```sh
ctest --preset linux-gcc -L conformance
```

All 323 files; none may regress.

### 8. Format, tidy, guards

```sh
bash scripts/check-format.sh
cmake --preset linux-clang && bash scripts/check-tidy.sh build/linux-clang
bash scripts/check-clean.sh
bash scripts/check-dco.sh
```

### 9. Commit and open the PR

`git commit -s`; reference the issue; both PR-template acknowledgments
checked.

### What a family touches, in full

| File | What you add |
| --- | --- |
| `core/src/cpu/instructions/<family>.cpp` | everything |
| `core/CMakeLists.txt` | one sorted line |
| `core/include/amberfolio/cpu/instructions.h` | your declaration block |
| `core/src/cpu/dispatch.cpp` | one line per opcode, sorted |

Plus `tests/CMakeLists.txt` for a unit-test source.
`tests/conformance/vector-files.txt` changes only when the pin changes.
Anything else you find yourself editing wants its own commit and its own
reasoning.

## 3. A worked example

ADD, which is close to pure wiring. `core/src/cpu/instructions/add.cpp`:

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

Handler names follow the mnemonic and its Intel-notation operands, which
is how the vectors' own disassembly reads.

### Unit tests

Add one in `tests/core/cpu/` (and a sorted line in `tests/CMakeLists.txt`)
to pin something the vectors cannot reach or to keep a reduced repro.
`tests/core/cpu/test_bus.h` gives a flat megabyte, a port map and a
record of every access; `test_dispatch.h` gives tables for testing the
decoder.

## 4. The commands

```sh
cmake --preset linux-gcc          # or windows-msvc, macos, linux-clang, linux-asan-ubsan, wasm
cmake --build --preset linux-gcc
ctest --preset linux-gcc
cmake --build --preset linux-gcc --target amberfolio-conformance-tests   # just the harness

ctest --preset linux-gcc -L unit
ctest --preset linux-gcc -L conformance
ctest --preset linux-gcc -R "conformance\.op_00$"
ctest --preset linux-gcc -R "conformance\.op_D0_[0-7]$"
ctest --preset linux-gcc -R "conformance\.op_(00|01|04|05)$"

python3 scripts/fetch-conformance-vectors.py --print-dir
AMBERFOLIO_CONFORMANCE_LIMIT=200 ctest --preset linux-gcc -R "conformance\.op_00$"
```

`--output-on-failure` is already on via the presets. On Windows, build
from a VS developer shell on a short path; in PowerShell set
`$env:AMBERFOLIO_CONFORMANCE_LIMIT = '200'`.

| Variable | What it does |
| --- | --- |
| `AMBERFOLIO_CONFORMANCE_LIMIT` | first N vectors of each file. CI's matrix jobs use 500, the sanitizer job 100, the full-suite job nothing |
| `AMBERFOLIO_CONFORMANCE_VECTORS` | where the condensed vectors live |
| `AMBERFOLIO_CONFORMANCE_REQUIRED` | missing vectors fail instead of skip (CI sets it) |

**Drop the limit before you believe a green result.** The gates and the
sanitizer preset are in CONTRIBUTING.md; `check-tidy.sh` needs only a
configured tree.

## 5. The exactness rule

Flags match **bit for bit, undefined behaviour included**. The harness
carries no masks and none will be added (#35). Software of the period
read undefined flags. **When the manual and the vectors disagree, the
vectors win**: match them and add a comment stating the quirk as a fact
(`core/src/cpu/alu.cpp`'s AF-after-logical comment is the model). Do not
mask a failing bit to get green; a known-wrong bit with a comment on the
issue is a contribution, a hidden one is a liability.

## 6. Log, don't fake

An opcode with no handler stops the machine with a record naming it
(`stop_reason::unimplemented_opcode`, plus the ModRM `reg` field for a
group). Never guess a neighbouring opcode into existence; if your family
needs one, say so on the issue.

## 7. Debugging a failing vector

### Reading the report

Up to ten failures per file in full, then a count.

```
04 test 4211  "add al, 1Bh"  bytes 04 1B
  FLAGS expected F013 [......A.C]  got F003 [........C]  differ: AF
```

The stem, the test's `idx`, the suite's disassembly, the encoded bytes
with prefixes; then a line per differing register (AX BX CX DX SP BP SI
DI CS DS ES SS IP); the FLAGS line in hex and as `ODITSZAPC` with the
differing bits named; a memory line per differing byte (up to twelve) in
one of three shapes: `expected NN  got MM`, `expected NN, and nothing was
written there`, `written NN, and the vector does not account for it`.

Four other reports:

- `stopped at F000:0100 on opcode FF /3 — no handler for it in the
  dispatch table`: no handler, or a wrong group index (`/3` is the `reg`
  field).
- `the instruction had not retired after 200 iterations`: a REP that
  kept calling `keep_repeating()`.
- `ports: 2 of 3 transactions were never made`: scripted port traffic
  not done; wrong order, port or value is reported per transaction.
- `read of 0123A, which the vector does not map`: almost always a wrong
  effective address in your handler (capped at eight per test; the
  harness returns FF and carries on).

### Reproducing one test

Granularity is the file:

```sh
AMBERFOLIO_CONFORMANCE_LIMIT=50 ctest --preset linux-gcc -R "conformance\.op_00$"
```

Then take the failing case's bytes and before-state into a unit test on
`test_bus.h`, where a breakpoint works.

### The common traps

- **Byte-half registers.** `reg8` 4–7 are AH/CH/DH/BH.
  `regs()[static_cast<reg16>(field)]` on a byte operand is the bug.
- **The default segment for BP forms** is SS, everything else DS, except
  mod 00 rm 110 (a bare address, DS). The decoder does this; addresses
  you form yourself are yours.
- **CF preservation on INC/DEC.** INC is not ADD-of-1; `alu::inc` and
  `alu::dec` hand CF back.
- **A register you never wrote must still hold its before value.** The
  reader folds changes, so `test.after` is complete; a clobbered
  unrelated register fails.
- **64 KiB offset wrap.** A word at FFFF is bytes FFFF and 0000 of the
  same segment; never carry into the segment.
- **Flag-word assignment.** `cpu.regs().flags = r.flags`. Don't OR bits
  in; don't route internal updates through `load_flags`.
- **Operand read order.** Evaluate operands into locals before calling
  the kernel; the harness compares what the CPU asked memory for.
- **Prefixes on instructions that have no use for them.** The vectors
  prepend random prefixes; the decoder records, the handler ignores.
- **IP after the instruction.** A relative jump's target is relative to
  IP after the whole instruction, once the displacement is fetched.

## 8. The pull-request checklist

- [ ] Every stem on the issue passes in full, no `AMBERFOLIO_CONFORMANCE_LIMIT`.
- [ ] `ctest --preset linux-gcc -L conformance` green: all 323 files.
- [ ] `ctest --preset linux-gcc -L unit` green.
- [ ] Only the four files of §2 touched; the shared three by sorted one-liners.
- [ ] New files carry `// SPDX-License-Identifier: AGPL-3.0-only`.
- [ ] `check-format.sh`, `check-tidy.sh`, `check-clean.sh`, `check-dco.sh` pass.
- [ ] Every non-merge commit signed off (`git rebase --signoff` if you forgot).
- [ ] The PR body keeps both template acknowledgments, checked, and references its issue.

CI jobs: `guards` (guard self-test, content guard, DCO, clang-format,
shellcheck), `tidy`, `vectors` (fetches/caches per OS), `conformance
(full suite)`, `build (x5)` (windows-msvc, macos-universal, linux-gcc,
linux-clang, wasm; conformance capped at 500 per file on the native
rows), `sanitizers` (100 per file), `acknowledgments`.
