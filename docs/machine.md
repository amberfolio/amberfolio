# Extending the machine

This is the sibling of [`cpu-implementation.md`](cpu-implementation.md).
That one is for touching the interpreter; this one is for everything
around it — adding a device, adding a BIOS or DOS service, or working out
where a new piece of hardware belongs.

It was written at M2's closeout, for M3. When the game first boots it
will stop, loudly, on some service this machine does not have; the log
line will say which, and this document is how you go from that line to a
handler.

Read it once. It assumes you know what a PC is and nothing about this
repository.

The short version:

> A **device** answers bus cycles inside the ports and memory it claimed.
> A **service** is a native function behind an interrupt vector. Neither
> knows about the other, and neither invents an answer it does not have.

- [1. The layer in ten minutes](#1-the-layer-in-ten-minutes)
- [2. Adding a device](#2-adding-a-device)
- [3. Adding a service](#3-adding-a-service)
- [4. Virtual time](#4-virtual-time)
- [5. Log, don't fake — what it means here](#5-log-dont-fake--what-it-means-here)
- [6. Talking to a host](#6-talking-to-a-host)
- [7. Testing](#7-testing)

---

## 1. The layer in ten minutes

Twenty headers under `core/include/amberfolio/machine/`. Most work needs
five of them. All are commented at length — this section is the map, not
a substitute for reading the one you are about to call into.

### The machine — `machine.h`

`machine` owns the RAM, the maps, the devices, the scheduler, the service
floor and the processor, and **it is the `cpu::bus`**. A memory cycle
arrives at `machine::read_memory`, the memory map classifies the address,
and it goes to RAM, to a device, or nowhere. The CPU never learns which.

`step()` is one scheduling step. `run(until)` steps until virtual time
reaches a tick you name. Both are described in §4.

### The maps — `memory_map.h`, `port_map.h`

An address gets one of four answers: `ram`, `rom`, `device`, or
`open_bus`. A port gets a device or nothing. Unclaimed space is not an
error — it reads `0xFF` and swallows writes, because that is what an
unterminated bus does — but the first touch of each 4 KiB page and each
port is reported, once, per reset.

`memory().ram()` is the documented back door: the machine's own writers —
the loader, the BIOS setup, a test — write memory *as the machine*, not
as the program, so they bypass device routing and the ROM refusal.

### The device contract — `device.h`

Four things: what you claim (`claims()`), how you answer inside it
(`read_port`/`write_port`/`read_memory`/`write_memory`), how you come
back to power-on (`reset()`), and how you refuse something
(`report_fault()`).

There is deliberately **no time on this interface**. See §4.

### The service floor — `service_floor.h`

The IVT and the BDA are real memory. Every provided vector points at a
one-byte `IRET` stub in segment F000; the machine compares CS at each
step boundary and, when execution reaches a stub, runs the native handler
and then lets the CPU execute the `IRET` itself.

This is why a program that hooks a vector works **by construction**: it
overwrites the IVT entry, the entry stops pointing at our stub, and the
handler simply becomes unreachable. Nothing detects hooking, because
detecting hooking is guessing.

`service_floor::reset()` is also this machine's **power-on self test**,
and both halves of it matter. It lays the vector table, the stubs and the
BDA down in memory — and then it programs the hardware a PC's ROM
programs before the first program runs: PIT channel 0 at 18.2 Hz and the
8259's ICW sequence, as real bus cycles, to whichever of the two are
attached. M2 had only the memory half, and the shape of that gap is worth
remembering: nothing refused anything, no line was logged, and M3's first
boot simply sat in a two-instruction loop watching `40:6C` for a change
that had no way to arrive. Log-don't-fake cannot catch a program that
never asked.

### The platform interface — `platform.h`

The seam to the hosts, and the one file to read before writing host code.
One rule shapes all of it:

> **Core → host is pulled. Host → core is pushed. Nothing in core ever
> calls out.**

Frames, audio and console bytes are buffered and taken. Keys, the wall
clock and the program are pushed in. The VFS is the one deliberate
exception, and the file says why.

---

## 2. Adding a device

1. **New header and source** under `core/include/amberfolio/machine/` and
   `core/src/machine/`, one sorted line each into `core/CMakeLists.txt`.
2. **Derive from `device`.** Return your ports and memory windows from
   `claims()`; the spans must be valid while `machine::attach()` runs.
3. **If you need a moment in time, derive from `scheduled` as well** and
   register with `machine::schedule()` — a *separate* call from
   `attach()`, because a type deriving from both converts to each base
   equally well and one overload would be ambiguous. The PIT is the
   worked example: it is attached once and scheduled twice, one
   participant per channel.
4. **Refuse what you do not implement** with `report_fault(at, detail)`.
   §5 is about what that means.
5. **Tests** under `tests/core/machine/`, driving the device through real
   ports and real addresses rather than private state.

The house pattern for anything bounded is a fixed-capacity `std::array`
with a documented capacity constant and a loud failure when it is
exhausted — `memory_map::max_windows`, `port_map::max_ranges`,
`machine::max_devices`, `scheduler::max_participants`. **`core/` has no
allocator**: no `std::vector`, no `std::map`, no `<memory>`, no
exceptions. The standard headers in use are `<cstdint>`, `<cstddef>`,
`<array>`, `<span>`, `<bit>`, `<compare>`, `<atomic>` and `<new>`.

---

## 3. Adding a service

This is what M3 will spend most of its time doing.

1. **Write the handler** as a `service_handler` — a plain function
   pointer, so it has nowhere to keep state. State that a handler needs
   lives on `machine` and is reached through it, exactly as `dos()`,
   `input()`, `wall()` and `console()` already are.
2. **Install it** into the floor. `install_int10()`, `install_dos_services()`
   and `keyboard_service::install()` are the three worked examples.
3. **Report flags through the stack image.** DOS's convention is CF set
   on failure with the code in AX; `service::frame` names the offsets of
   the pushed IP, CS and FLAGS so no call site spells `SP+4`. This is
   what a handler written in 8086 would do, and it is why the return goes
   through a real `IRET`.
4. **Refuse the rest.** A vector serving many functions by AH must refuse
   an AH it does not know as loudly as an unbacked vector — the floor's
   own null-handler check cannot see inside one.

If a handler must let the machine run and then carry on — the timer
handler chains INT 1Ch and still owes an EOI — claim a **continuation
stub**, set IP to it, and deliver the interrupt. The pushed return
address is then the second half of your handler. This is the same thing a
BIOS written in 8086 gets free from the instruction after its `INT`.

**A native handler and its stub's `IRET` must not be split by an
interrupt.** If one is delivered between them, the `IRET` that eventually
returns to the stub arrives at the boundary test again and runs the
handler a second time. `machine::step()` therefore dispatches deadlines
*before* the CS compare, and the compare defers to
`cpu::processor::interrupt_due()`. Do not reorder those two without
reading why they are in that order.

---

## 4. Virtual time

**Everything machine-visible is counted in ticks of the PIT input clock,
1,193,182 Hz** (`clock.h`). Not microseconds and not CPU clocks: the PIT
counts in exactly this unit and the speaker's square wave is integrated
from it, so anything else would round at the one place the arithmetic has
to be exact.

**Nothing under `core/` may read host time.** No `<chrono>`, no
`std::time`. That rule is what makes a run recordable and replayable
(PLAN.md §4), and since M4 it is enforced mechanically:
`scripts/check-host-time.sh` runs in CI's guards job and fails on any
clock read under `core/` (#78; `docs/replay.md` is what the rule buys).

A step costs a fixed number of ticks under the speed governor; per-opcode
cycle counting is an explicit non-goal. Devices do not tick. **They
compute.** The PIT turns a channel's count into a formula evaluated on
demand and posts its next output edge as a deadline; the renderer arms a
60 Hz frame boundary. Nothing walks forward one tick at a time.

Two properties of `scheduled` that matter and are easy to miss:

- **A handler is called with the tick it armed, not the tick the machine
  reached.** A device that re-arms from `due` therefore cannot drift,
  however coarse the step cost is.
- **Ties break by registration order**, which is fixed by how the machine
  is wired rather than by anything the program does — a determinism
  requirement, not a convenience.

---

## 5. Log, don't fake — what it means here

CLAUDE.md states the rule without qualification: an unimplemented
service, register or port is **a loud log line and a clean stop**, never
a silently guessed answer.

Concretely, at this layer:

| you are | you refuse with | the machine does |
|---|---|---|
| a device | `report_fault(at, detail)` | stops with `unimplemented_device`, reports it |
| a service handler | `stop_unimplemented_function(at)` | stops with `unimplemented_service` |
| a handler declining one request | `stop_unsupported_request(at)` | stops with `unsupported_request` |

The distinction in the last two rows is real: `unimplemented_service`
means nothing was installed behind that vector, `unsupported_request`
means a handler ran and said no to this particular call.

**Open bus is not faking.** An address or port nothing claims reads
`0xFF` and drops writes because that is the true hardware answer; it is
reported once per page and per port, and the machine keeps running. The
difference from a refusal is that nobody is inventing anything.

A cautionary note from M2's own history: the EGA originally refused
registers with a device-local halt, because `device` had no channel back
to the machine. It was inert rather than wrong — but nothing stopped and
nothing was logged, which is not the rule. That gap was filed (#65),
closed by the fault channel in #46, and the EGA joined it at closeout. If
you find yourself inventing a private way to say no, that is the signal
to fix the shared one instead.

### When the honest answer is a notice and not a stop

There is a third row that does not fit the table, and it is worth
knowing before you reach for a stop: a request the machine can honestly
*record* but not honestly *perform*.

The worked example is `INT 10h AH=00h AL=03h`. M3's first boot asks for
80x25 text on its way to graphics, the way most programs of the era do.
This machine has no text path at all — no CRTC, no character generator,
nothing claiming B8000 — so there is nothing to program. But refusing
ends the run of every program that merely passes through text, over a
mode whose output nothing was ever going to look at.

So the mode number goes into the BDA, `AH=0Fh` reports it back, nothing
reaches the adapter, and the machine says so once through
`notice_kind::undisplayable_video_mode`. The notice is what keeps this
from being an accommodation: it is a worklist line, in the same channel
as an open-bus touch, and a reader of the boot log sees exactly what the
machine agreed to do and did not do.

The test for whether you are in this row rather than inventing something:
**can you state, in the log line, precisely what did not happen?** If you
can, the notice is the honest answer. If the line would have to say
"handled it somehow", it is a stop.

### The refusal a reader actually sees

A stop is only half of "log, don't fake"; the other half is that the line
it produces has to be worth reading. `machine/report.h` is that line, and
it is formatted **in core** rather than in each host, because M3's exit
criterion is desktop *and* web and the two have to print the same
sentence at the same step for the comparison to mean anything (#84).

```
amberfolio: stop reason=unimplemented_service steps=99172 ticks=396688 frames=20 cs=F000 ip=0121 at=0B5D2
amberfolio: stop call=INT21 ah=35 al=00 ax=3500 from=0B58:0052 outcome=handled
amberfolio: stop next=INT 21h AH=35h AL=00h
```

Three things follow from that shape, and they are the whole reason the
machine keeps anything beyond `stop_record`:

- **`machine::steps()`.** Ticks and steps are the same fact twice only
  while the speed governor is left alone; the step count is what stays
  comparable between two runs, and it is what "at the same step" means.
- **`machine::last_service_call()` and `last_device_stop()`.** Kept
  unconditionally, because they are what turn `reason=... at=0B5D2` into
  a worklist entry. `outcome=` is the field that tells the two refusals
  above apart: `unimplemented` is a vector nothing backs,
  `handled` with a service-shaped stop is a handler that ran and said no
  to this AH.
- **`machine::trace()`.** The last 256 instructions and 64 service calls,
  in a fixed ring, **off unless a caller asks** — one branch per step
  when it is off (`machine/trace.h`). It answers the question a bare
  address cannot: how the program got there.

The `next=` line is the M3 method in one sentence: it names the one
service, register or opcode to widen, so the worklist is written by the
machine rather than inferred by whoever is reading the log.

### The other line: which file (M4-G3 #104, M4-G4 #105)

A service call says `INT21 ax=3D02` and where it came from. It cannot say
*which file*, because the record is built as the stub is reached and the
path does not exist as an answer until the handler has resolved it — and
"which file" is what a shop's item data and a save game's write path are
both made of.

So the DOS layer reports a second, much quieter record of its own:

```
amberfolio: file mkdir \SAVE handle=0000 access_denied from=0B58:1823
amberfolio: file create \SAVE\SAVGAMA.DAT handle=0006 none from=0B58:1458
amberfolio: file close \SAVE\SAVGAMA.DAT handle=0006 none from=0B58:14A8
amberfolio: file open \SAVE\BOB.CHA handle=0006 none from=0B58:1458
amberfolio: file unlink \SAVE\BOB.CHA handle=0000 none from=0B58:162D
```

That is a Gold Box save game in five lines: make the save directory and
ignore the refusal, write the slot, move the party's character files into
it. The rules the channel keeps:

- **The naming calls only** — `AH=39h/3Ch/3Dh/41h`, plus `AH=3Eh` because
  a save is a file that has to *close* before a player can be told it was
  written. Reads and writes name a handle, and a line per 512-byte chunk
  would bury what the channel is for under what the service trace already
  shows.
- **After the outcome, not before.** The point of the record is the path
  and the answer, and neither exists until the handler has both.
- **Refusals are reported, not swallowed.** "Is there a save in slot A"
  is a question a program asks by opening a file, and `file_not_found` is
  the answer — the same rule §5 states for the machine as a whole, one
  layer up.
- **The path is the canonical one**, not the bytes at `DS:DX`, so a log
  line and the filesystem agree about what was touched.

A handler that starts naming files adds its `floor.report_file()` call in
the same change, the way §3 says a service closing a boot-log line adds
its call to the synthetic boot. `machine_program::file_trace` in
`tests/programs` is where that gets asserted, in order and including the
refusals.

---

## 6. Talking to a host

Read `platform.h`'s design essay first; it was written so the host issues
needed no further design conversation, and it is still the answer.

The parts that catch people:

- **Frames are pulled**, with a monotonic generation counter. A slow host
  drops frames; it never slows the machine.
- **`audio_timeline::render()` is the only core function callable off the
  machine thread**, and by exactly one thread — not one at a time. The
  edge list is canonical machine state; the float samples are not, which
  is what keeps audio out of replay hashes.
- **Input is stamped with the machine's own clock**, and a host may only
  post between `run()` calls, so the stamp is a settled step-boundary
  value.
- **The wall clock is a seed plus virtual time**, so date/time reads are
  deterministic and a replay injects a recorded value.

The C ABI (`abi.h`) mirrors all of this for the wasm host: an opaque
handle, no structs by value, nothing returned that the other side must
free. **A symbol missing from `-sEXPORTED_FUNCTIONS` in
`hosts/web/CMakeLists.txt` silently does not exist** — the smoke test
checks the export list for exactly that reason.

---

## 7. Testing

Three tiers, and they answer different questions.

**Unit tests** (`tests/core/machine/`) drive one device or one service
through its real ports and addresses. This is where exhaustive
table-driven coverage belongs — the EGA's write pipeline is the model.

**Machine programs** (`tests/programs/machine_*.cpp`) are self-written
8086 programs run through the whole machine to program exit. They are
M2's exit criterion and the thing that proves the pieces compose. Note
two constraints: the M1 flat-bus programs must keep passing unchanged,
and **this apparatus must stay free of GoogleTest**, because it is the
only test code that builds under Emscripten — which is what makes
`ctest --preset wasm` run the interpreter rather than merely compile it.

`synthetic_boot` is the M3 member of that list and the one to extend
when you add a service. It is shaped like the thing CI can never run: a
stub that unpacks the rest of itself and jumps into it, a module loaded
off the filesystem and entered with a far call through a relocated
pointer, and a call to every service the real boot turned out to need.
**A service that closes a boot-log line adds its call here in the same
change that implements it** — that program's coverage is the record of
what M3 added.

**Host smoke tests** run a program through a host headlessly and assert
what came out.

### A warning about goldens

Two separate M2 bugs were found by refusing to trust a hash:

- a framebuffer golden that faithfully recorded a band drawn 648 pixels
  wide instead of 2568, because programming an indexed EGA register
  leaves the value in `AL` and clobbered the mask;
- a framebuffer hash pinned over an all-black frame that had never been
  composed, because `scheduler::arm()` silently no-ops on a participant
  that was never registered.

In both cases every other assertion passed. **A hash tells you something
changed; it never tells you the thing was ever right.** Assert at least
one expectation you derived by hand — a named pixel, a run length, a
generation counter greater than zero — alongside every hash you pin.

### The commands

```sh
cmake --preset windows-msvc          # or linux-gcc, linux-clang, macos, wasm
cmake --build --preset windows-msvc
ctest --preset windows-msvc -L unit  # the unit suite
ctest --preset windows-msvc -L smoke # the hosts
ctest --preset wasm                  # the machine programs under node

bash scripts/check-format.sh
bash scripts/check-tidy.sh build/windows-msvc
```

Always rebuild before `ctest`. A stale binary reports "100% tests passed"
from a build that failed, and the tell is a test total that is not the
baseline plus your additions — which cost real time in M2 more than once.
