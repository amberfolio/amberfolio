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

## What is here

| Session | Program | What it pins |
| --- | --- | --- |
| `spin.rec` | a 34-byte `JMP $` MZ file, built in the tests | four frames of a machine doing nothing but keeping time — the PIT, the scheduler, the renderer's frame deadline and the clock, which is the smallest run in which every target could still disagree |

`spin.rec` is deliberately the least interesting run that could fail. The
program executes one instruction forever, so anything that differs
between two targets differs because the *machine* does — a device's
arithmetic, the scheduler's tie-break, the order a state section is
written in — and not because a program went two ways.

Larger sessions are #101's, together with the cadence question: a
checkpoint line carrying its section hashes is about four hundred bytes,
and the content guard refuses a file over 256 KiB, so a session of any
length checkpoints less often than every frame. `spin.rec` is four
frames and carries no section hashes, which is why it is 566 bytes.

## Who checks them

Both, on every push, from the same file:

- **native** — `SessionLibrary.*` in `tests/core/machine/session_test.cpp`,
  through `af_machine_verify_recording`.
- **wasm** — `hosts/web/tests/smoke.mjs`, through the same ABI call, in
  a build that shares no compiler, no standard library and no SHA-256
  implementation with the native one.

Each also tampers with a checkpoint hash and requires the refusal. A
golden that cannot fail is not one.

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
  order, and all three have to move together.

Re-recording is not a way to make a red test green. If a session stops
verifying and none of the above changed, the machine changed and the
finding is real — `docs/replay.md` §5 is how to read the report, which
names the first section that disagreed.
