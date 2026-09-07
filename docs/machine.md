# Extending the machine

The sibling of [`cpu-implementation.md`](cpu-implementation.md): adding
a device, adding a BIOS or DOS service, or working out where a new piece
of hardware belongs.

> A **device** answers bus cycles inside the ports and memory it claimed.
> A **service** is a native function behind an interrupt vector. Neither
> knows about the other, and neither invents an answer it does not have.

## 1. The layer in ten minutes

Headers under `core/include/amberfolio/machine/`; each is commented at
length.

- **`machine.h`**: `machine` owns the RAM, the maps, the devices, the
  scheduler, the service floor and the processor, and **is the
  `cpu::bus`**. A memory cycle arrives at `machine::read_memory`, the
  memory map classifies the address, and it goes to RAM, a device, or
  nowhere. `step()` is one scheduling step; `run(until)` steps until
  virtual time reaches a tick.
- **`memory_map.h`, `port_map.h`**: an address is `ram`, `rom`,
  `device` or `open_bus`; a port is a device or nothing. Unclaimed space
  reads `0xFF` and swallows writes, and the first touch of each 4 KiB
  page and each port is reported once per reset. `memory().ram()` is the
  back door for the machine's own writers (loader, BIOS setup, tests),
  bypassing device routing and the ROM refusal.
- **`device.h`**: `claims()`, `read_port`/`write_port`/`read_memory`/
  `write_memory`, `reset()`, `report_fault()`. No time on this interface
  (§4).
- **`service_floor.h`**: the IVT and BDA are real memory. Every provided
  vector points at a one-byte `IRET` stub in segment F000; the machine
  compares CS at each step boundary, runs the native handler when
  execution reaches a stub, and lets the CPU execute the `IRET`. A
  program that hooks a vector works by construction.
  `service_floor::reset()` is the power-on self test: it lays down the vector table, stubs and BDA, then
  programs PIT channel 0 at 18.2 Hz and the 8259's ICW sequence through
  real bus cycles, then clears the display buffer through the video
  card's own write pipeline. A missing half of that shows as a boot that
  stops making progress with nothing logged, since log-don't-fake cannot
  catch a program that never asked.
- **`platform.h`**: the seam to the hosts. **Core to host is pulled;
  host to core is pushed; nothing in core ever calls out.** The VFS is
  the one deliberate exception, and the file says why.

## 2. Adding a device

1. New header and source under `core/include/amberfolio/machine/` and
   `core/src/machine/`, one sorted line each in `core/CMakeLists.txt`.
2. Derive from `device`; return ports and memory windows from `claims()`
   (spans valid while `machine::attach()` runs).
3. For a moment in time, also derive from `scheduled` and register with
   `machine::schedule()`, a separate call from `attach()` (the two bases
   make one overload ambiguous). The PIT is attached once and scheduled
   twice, one participant per channel.
4. Refuse what you do not implement with `report_fault(at, detail)` (§5).
5. Tests under `tests/core/machine/`, through real ports and addresses.

Anything bounded is a fixed-capacity `std::array` with a documented
capacity constant and a loud failure when exhausted
(`memory_map::max_windows`, `port_map::max_ranges`,
`machine::max_devices`, `scheduler::max_participants`). **`core/` has no
allocator**: no `std::vector`, `std::map`, `<memory>` or exceptions. The
standard headers in use are `<cstdint>`, `<cstddef>`, `<array>`,
`<span>`, `<bit>`, `<compare>`, `<atomic>` and `<new>`.

## 3. Adding a service

1. Write the handler as a `service_handler`, a plain function pointer.
   State lives on `machine` (`dos()`, `input()`, `wall()`, `console()`).
2. Install it into the floor: `install_int10()`, `install_dos_services()`
   and `keyboard_service::install()` are the worked examples.
3. Report flags through the stack image: CF set on failure with the code
   in AX; `service::frame` names the offsets of the pushed IP, CS and
   FLAGS.
4. Refuse the rest: a vector serving many functions by AH refuses an
   unknown AH as loudly as an unbacked vector.
5. A service that closes a boot-log line adds its call to
   `synthetic_boot` in `tests/programs` in the same change; a handler
   that names files adds its `floor.report_file()` call and its line in
   `machine_program::file_trace`.

A handler that must let the machine run and then continue (the timer
handler chains INT 1Ch and still owes an EOI) claims a **continuation
stub**, sets IP to it, and delivers the interrupt.

**A native handler and its stub's `IRET` must not be split by an
interrupt**, or the `IRET` returns to the stub, hits the boundary test
again and runs the handler twice. `machine::step()` dispatches deadlines
*before* the CS compare, and the compare defers to
`cpu::processor::interrupt_due()`. Do not reorder those.

## 4. Virtual time

**Everything machine-visible is counted in ticks of the PIT input clock,
1,193,182 Hz** (`clock.h`); the PIT counts in that unit and the speaker's
square wave is integrated from it. **Nothing under `core/` may read host
time** (`scripts/check-host-time.sh`, in CI); that is what makes a run
replayable (`docs/replay.md`).

A step costs a fixed number of ticks under the speed governor; per-opcode
cycle counting is a non-goal. Devices do not tick, they compute: the PIT
turns a count into a formula evaluated on demand and posts its next edge
as a deadline; the renderer arms a 60 Hz frame boundary. Two properties
of `scheduled`: a handler is called with the tick it armed, not the tick
the machine reached, so re-arming from `due` cannot drift; and ties break
by registration order, fixed by wiring.

## 5. Log, don't fake: what it means here

| you are | you refuse with | the machine does |
|---|---|---|
| a device | `report_fault(at, detail)` | stops with `unimplemented_device` |
| a service handler | `stop_unimplemented_function(at)` | stops with `unimplemented_service` (nothing installed behind that vector) |
| a handler declining one request | `stop_unsupported_request(at)` | stops with `unsupported_request` (a handler ran and said no) |

**Open bus is not faking**: `0xFF` and dropped writes is the true
hardware answer, reported once per page and port. If you find yourself
inventing a private way to say no, fix the shared channel instead.

**A notice, when the honest answer is neither a stop nor a fake.** A
request the machine can honestly *record* but not honestly *perform*:
`INT 10h AH=00h AL=03h` asks for 80x25 text, which this machine has no
path for and whose output nothing will look at. The mode number goes
into the BDA, `AH=0Fh` reports it back, nothing reaches the adapter, and
the machine says so once through `notice_kind::undisplayable_video_mode`.
The test: **can you state, in the log line, precisely what did not
happen?** If yes, a notice; if the line would say "handled it somehow",
a stop.

**The refusal a reader sees** is formatted in core (`machine/report.h`)
so both hosts print the same sentence at the same step:

```
amberfolio: stop reason=unimplemented_service steps=99172 ticks=396688 frames=20 cs=F000 ip=0121 at=0B5D2
amberfolio: stop call=INT21 ah=35 al=00 ax=3500 from=0B58:0052 outcome=handled
amberfolio: stop next=INT 21h AH=35h AL=00h
```

`machine::steps()` is what "at the same step" means across two runs;
`last_service_call()` and `last_device_stop()` are kept unconditionally
(`outcome=unimplemented` is an unbacked vector, `handled` a handler that
said no); `machine::trace()` keeps the last 256 instructions, 64 service
calls and 32 naming file calls, off unless asked (`machine/trace.h`).
`next=` names the one thing to widen.

**The file line.** The DOS layer reports the naming calls (`AH=39h/3Ch/
3Dh/41h`, and `3Eh` because a save has to close) after the outcome, with
the canonical path:

```
amberfolio: file mkdir \SAVE handle=0000 access_denied from=0B58:1823
amberfolio: file create \SAVE\SAVGAMA.DAT handle=0006 none from=0B58:1458
amberfolio: file close \SAVE\SAVGAMA.DAT handle=0006 none from=0B58:14A8
```

Refusals are reported, including a name `canonicalize()` will not
resolve (the event carries the root and the error says why, #121). A
close carries two flags, `read_through` and `written_through`, for a
consumer of the record (the load menu opens every slot to list them and
only one open is a load); they are bookkeeping and not machine state.
The trace ring keeps the last thirty-two file events, and
`format_trace_report` renders them above the service calls.

## 6. Talking to a host

Read `platform.h`'s design essay first.

- **Frames are pulled**, with a monotonic generation counter; a slow host
  drops frames and never slows the machine.
- **`audio_timeline::render()` is the only core function callable off
  the machine thread**, by exactly one thread. The edge list is canonical
  machine state; the float samples are not. The edge list can also be
  read as an opt-in log the host drains between slices (`docs/hosts.md`
  §4).
- **Input is stamped with the machine's own clock**; a host posts only
  between `run()` calls.
- **The wall clock is a seed plus virtual time.**

The C ABI (`abi.h`) mirrors this for the wasm host: an opaque handle, no
structs by value, nothing the other side must free. **A symbol missing
from `-sEXPORTED_FUNCTIONS` in `hosts/web/CMakeLists.txt` silently does
not exist**; the smoke test checks the export list.

## 7. Testing

- **Unit tests** (`tests/core/machine/`) drive one device or service
  through real ports and addresses; the EGA write pipeline is the model
  for table-driven coverage.
- **Machine programs** (`tests/programs/machine_*.cpp`) are self-written
  8086 programs run through the whole machine to exit. **This apparatus
  stays free of GoogleTest** so it builds under Emscripten and
  `ctest --preset wasm` runs the interpreter. `synthetic_boot` is shaped
  like a real boot (self-unpacking, a module loaded off the filesystem
  and far-called through a relocated pointer, every service the real
  boot needed) and is extended in the same change as any new service.
- **Host smoke tests** run a program through a host headlessly.

**A hash tells you something changed; it never tells you the thing was
ever right.** Assert at least one hand-derived expectation (a named
pixel, a run length, a generation counter above zero) beside every hash
you pin. Two M2 bugs hid behind green hashes: a band 648 pixels wide
instead of 2568 (an indexed EGA register write left its value in `AL`),
and an all-black frame never composed (`scheduler::arm()` no-ops on an
unregistered participant).

```sh
cmake --preset windows-msvc          # or linux-gcc, linux-clang, macos, wasm
cmake --build --preset windows-msvc
ctest --preset windows-msvc -L unit
ctest --preset windows-msvc -L smoke
ctest --preset wasm

bash scripts/check-format.sh
bash scripts/check-tidy.sh build/windows-msvc
```

Always rebuild before `ctest`: a stale binary reports "100% tests passed"
from a build that failed.
