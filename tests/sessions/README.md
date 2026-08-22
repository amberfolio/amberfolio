# Sessions

Committed recordings, and the cross-target proof they exist to be.

[`docs/replay.md`](../../docs/replay.md) is what a recording is and how
one is made. This directory is the part that gets committed: a small
number of goldens that every target must reproduce exactly.

The claim is PLAN.md §4's, and it is not a claim about a run — it is a
claim about the emulator. A recording is *keys, ticks and hashes*, so a
target that reproduces one has reproduced every byte of RAM, every
device's registers, the scheduler's deadlines, the DOS handle table and
the framebuffer, at every checkpoint, from the same starting conditions.
Two builds that agreed about the answer and disagreed about the machine
would fail here and nowhere else.

Nothing in a recording is content. A checkpoint is a SHA-256 and a tick;
the manifest is names, sizes and digests. That is what makes these
committable at all (PLAN.md §6: "hashes are committable; screen content
never is"), and it is why the programs recorded here are the
repository's own.

## What a session is

Two things, named alike:

    tests/sessions/spin.rec      the recording
    tests/sessions/spin/         the disk it was recorded against

The recording's manifest names every file in the disk and pins it by
SHA-256, so the pair is self-checking: a disk that drifted from its
recording is refused before a step is taken. Keeping the disk beside the
recording is what lets the desktop host and `scripts/sweep.py` verify any
session with no special case for which one it is.

| Session | Program | What it pins |
| --- | --- | --- |
| `spin.rec` | `spin/SPIN.EXE` — 34 bytes, ten of them `JMP $` behind an MZ header | four frames of a machine doing nothing but keeping time: the PIT, the 8259, the scheduler, the renderer's frame deadline and the clock |

`spin.rec` is deliberately the least interesting run that could fail. The
program executes one instruction forever, so anything that differs
between two targets differs because the *machine* does — a device's
arithmetic, the scheduler's tie-break, the order a state section is
written in — and not because a program went two ways. It caught two such
things while it was being written: an attach order that differed between
the ABI and the hosts, and a machine that had never had its RESET line
pulled.

Larger sessions are #101's, together with the cadence question: a
checkpoint line carrying its section hashes is about four hundred bytes,
and the content guard refuses a file over 256 KiB, so a session of any
length checkpoints less often than every frame. `spin.rec` is four
frames and carries no section hashes, which is why it is 566 bytes.

## Who checks them

Three, on every push, from the same file:

- **native** — `SessionLibrary.*` in `tests/core/machine/session_test.cpp`,
  through `af_machine_verify_recording`.
- **wasm** — `hosts/web/tests/smoke.mjs`, through the same ABI call, in a
  build that shares no compiler, no standard library and no SHA-256
  implementation with the native one.
- **desktop** — `sdl-host-verifies-a-session`, through the host's own
  `--replay`. Worth being a third and not a repetition: that host builds
  its device set with its own code rather than through
  `af_machine_attach_reference_devices()`, so a wiring that drifted from
  the ABI's would pass the other two and fail here.

The first two also tamper with a checkpoint hash and require the refusal.
A golden that cannot fail is not one.

    python3 scripts/sweep.py

runs all three over every session and prints one table. A target that is
not built is skipped and said so, never counted as a pass.

## When one of these has to change

A session is re-recorded when — and only when — the machine it describes
legitimately changes:

- `state_format_version` is bumped (a device grows a register, a section
  is added or reordered);
- `recording_format_version` is bumped (the line grammar changes);
- the reference device set's **attach order** changes. The canonical
  state hashes devices in attach order, so this is machine state.
  `hosts/sdl/src/main.cpp`, `core/src/abi.cpp`'s `reference_devices` and
  `tests/programs/machine_harness.cpp` all wire the same list in the same
  order, and all three have to move together;
- what `machine::reset()` leaves behind changes — the self test programs
  the PIT and the 8259 through real bus cycles, and that is where a
  session's device state starts.

Re-recording is not a way to make a red test green. If a session stops
verifying and none of the above changed, the machine changed and the
finding is real — `docs/replay.md` §5 is how to read the report, which
names the first section that disagreed.
