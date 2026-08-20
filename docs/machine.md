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
(PLAN.md §4), and it is enforced in review rather than mechanically —
which is a known gap, filed rather than forgotten.

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
