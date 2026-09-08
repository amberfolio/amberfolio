# Replay

How a run is recorded, how a recording is verified, and what a golden is.

The machine's only clock is virtual (`machine/clock.h`), and everything
nondeterministic it consumes arrives through the platform interface,
stamped with that clock. So a run is **keys, ticks and hashes**: the
initial conditions, the keys with their ticks, the wall-clock seed, and a
hash of the machine's state at checkpoints. A replay that delivers the
same keys at the same ticks reproduces the run exactly, on every target.
Nothing in a recording is content, which is what lets the session
library (`tests/sessions/README.md`) be committed.

## 1. What a recording is

Plain text (`machine/replay.h`), one line per fact:

```
amberfolio-recording 3 state=1
program BOOT.EXE 3f1c…
tail 202d4649525354 4c49474854
speed 256
seam probe
dir SAVE
file SAVE\CHARLIST.TXT 285 b677…
file SAVE\SAVGAMA.DAT 1024 4c9e…
file BOOT.EXE 2144 3f1c…
file OVL.BIN 9 a7e0…
wall 0 2026-09-06T08:30:00.00
key 5000 1e down
pull 120000 probe
checkpoint 1193182 1193182 9b2d… clock=… cpu=… ram=… devices=… …
end 2000000 2000000
```

The **preamble** is the initial conditions: the program and its
fingerprint, the command tail as hex, the speed (step cost in 1/256ths of
a tick; a replay runs at the recorded speed), every seam that was on, and
the **manifest**. Then the **stream** in tick order: wall seeds, key
events, seam pulls, checkpoints, and `end`.

- A `seam` line says the seam was on. A `pull` line says a person pulled
  its trigger at a tick (#161); it is an input event like a key, and a
  pull the engine refuses on replay is a divergence.
- A checkpoint is the tick, the step count, the whole-state hash, and
  optionally the first eight bytes of each section's hash. `end` is where
  the recording stopped; reaching it means everything before it held.

**The manifest** names the whole disk: every directory and every file at
every depth, `\`-joined and relative to the root with no leading `\`
(the spelling `tests/sessions/*.session` uses).

- Order is depth first, each directory's entries in the VFS's pinned name
  order (`dos_name_less`), a directory's own line before its contents.
  That is lexicographic over component sequences, so a mismatch reads as
  "one side has an entry the other has not".
- A directory keeps its own `dir PATH` line, no size, no digest. An empty
  directory is a fact about a disk.
- Depth is bounded by `dos_path::max_depth` (8) and entries by
  `replay_max_manifest_entries` (512); a disk past either is refused,
  never half-described. The largest disk the library pins is 193 entries.
- The manifest is tens of kilobytes of fixed storage, which is why
  `af_machine_verify_recording` keeps its player in static storage
  (`core/src/abi.cpp`).
- Version 1 recordings (seven of the library's) name only the root and
  are read that way (§7).

## 2. The canonical state

`machine/state.h` is the versioned, sectioned serialization a checkpoint
hashes. Thirteen sections, in order: `clock`, `cpu`, `ram`, `devices`,
`scheduler`, `keyboard`, `dos`, `wall`, `input`, `console`, `audio`,
`display`, `stop`. Every device writes its own architectural state
through `device::save_state()`; the platform classes write theirs; the
machine writes the order.

**In**: everything the program can observe or that decides what it
observes next: registers and interrupt latches, clock and step count,
every byte of RAM, every device's architectural state in attach order,
the armed deadlines, the DOS handle table and every open file's position,
the wall seed, queued input, undrained console bytes, the speaker's edge
list as a count and a running digest, the framebuffer and its generation,
and the stop record.

**Out**, by decision:

| Out | Because |
| --- | --- |
| the speed governor | configuration; the preamble names it |
| the seam engine and its toggles | configuration (`seam.h`); the preamble names each seam |
| the overlay tracker | an observation, rebuilt by replaying |
| the trace ring, first-touch notices | diagnostics; a run with `--trace` and one without must hash alike |
| every float audio sample | output; the edge list is canonical (`platform.h`) |
| the filesystem's contents | the host's; captured as the manifest |

`state_format_version` is bumped when the bytes change, and a bump
invalidates every golden: a player refuses to compare across versions,
and the session library is re-recorded in the same change.

## 3. Recording

```sh
amberfolio ./disk GAME.EXE --record session.rec
```

The preamble is written before SDL is up, so a run that cannot be
described is refused before a window opens. The loop then adds:

- a `key` line where a key is posted, stamped with `machine::time()`;
- a `pull` line where a seam's trigger is pulled (Pause/Break, or
  `--pull ID@FRAME`);
- a `checkpoint` where a frame ends (`renderer::frame_period`, off the
  virtual clock), taken after the slice and before the frame is presented;
- an `end` line.

A checkpoint hashes every byte of RAM; at one per frame a debug build
runs about thirty times slower and each line is about 400 bytes.
`--record-every N` spaces them; the session library uses **128** (a
little over two virtual seconds), which keeps a twenty-thousand-frame
leg under the content guard's 256 KiB. Two frames are checkpointed
whatever N says: **one that carried an input**, and **the one the run
ends on** (where a `stopped` marker lives). `sdl-host-records-and-replays`
asserts that the cadence changes what is written down and not what
happened.

## 4. Verifying

```sh
amberfolio ./disk GAME.EXE --replay session.rec
```

The recording decides the speed, the seams, the wall seed and every key,
so `--speed`, `--seam`, `--wall` and `--press` are refused alongside
`--replay`, and `--record` with `--replay` is refused.

The player (`machine::replay_player`) lives in core so both hosts and the
test harness verify a recording the same way. It parses, checks the
initial conditions, delivers the events and compares hashes; the host
runs the machine:

```
run to min(next_tick(), own frame boundary) → apply() → repeat
```

**A checkpoint of a stopped machine is marked `stopped`, and
`next_tick()` answers `never` for it.** `machine::run()` does not count
the step that exits or refuses, so a stopped machine and the machine one
step short of stopping stand at the same tick and step count. The host
must run *past* that tick for the machine to stop; held at the tick, it
would compare a machine never given the chance.
`Replay.ARecordingThatEndsInAStopIsReachedByRunningPastIt` is the test.
Every other event holds the host to its exact tick.

## 5. What can go wrong, and what the report says

One line, `amberfolio: replay …`. The exit code is the run's answer,
ahead of the program's own.

| Report | What happened |
| --- | --- |
| `verified checkpoints=N keys=K pulls=P` | every condition matched and `end` was reached; the process then returns the program's own exit code |
| `refused line=L why=…` | not a recording this player reads, or the initial conditions do not match (program, speed, seams, a file) |
| `refused … why=… path=SAVE\CHARLIST.TXT` | the manifest did not match, and that is the entry: a file whose digest or size differs, one the disk has and the recording does not, one the recording names and the disk lacks, or a directory where a file was named |
| `diverged line=L tick=T section=S expected=… actual=…` | a checkpoint hash was not the machine's; `section` is the first of the thirteen to disagree |
| `diverged … why=the machine ran past an event's tick` | the host overran an event: a frame period that is not the recorder's, or a `stopped` marker that is not true |
| `refused … why=the recording has no end line` | cut short; incomplete, not diverged |
| `in progress checkpoints=N` | a budget ended the run before `end`; a prefix verified |

`ctest -L smoke -R records-and-replays` is the round trip: record, replay,
then tamper with a hash and require the refusal. `scripts/sweep.py` runs
every committed session against every target that can verify one and
prints one table; a target not built, or a session whose disk is not
present, is a `SKIP` and never a pass.

## 6. The guard that makes it possible

**Virtual time is the only clock.** `scripts/check-host-time.sh` runs in
CI's guards job and refuses the standard library's clocks and libc time
calls under `core/`; `scripts/test-guards.sh` is its self-test, including
the names core uses legitimately (`machine::time()`, `wall_time`).

The wall clock a program reads through INT 21h is a **seed**, set once by
the host and read as `wall().at(time())`, so it advances with the machine
and is recorded as a `wall` line at a tick. Both hosts seed it (#320): the
desktop from its own clock before the first instruction, the page from
`new Date()`.

- **The seed is machine state, so it is in every checkpoint hash.** Two
  runs to be compared by hash or by pixel must be told the same date:
  `--wall YYYY-MM-DD[THH:MM[:SS[.CC]]]`. The program consumes the DOS
  clock (a minute's difference moves 73 pixels of the boot), so this is
  not only about hashes.
- `--wall none` is the unseeded machine every recording before #320 was
  made on; all committed recordings carry no `wall` line and verify
  unchanged. `scripts/visual-legs.py` states `--wall none` on both sides.
- The library's pairs were re-recorded on `--wall none` under #293, both
  halves of each with the same script and the same tick budget.

## 7. Versions

Two, independent, both on a recording's first line
(`amberfolio-recording 3 state=1`):

- **`recording_format_version`** (`machine/replay.h`), the line grammar.
  It is **3** (#161's `pull` line); 2 was #155's recursing manifest.
- **`state_format_version`** (`machine/state.h`), the bytes a checkpoint
  hashes. It is **1**. A player refuses to compare across versions.

**A recording format is read for as long as a recording of it may
exist.** `recording_format_oldest_read` says which versions a player
accepts, and each is read the way it was written: a version-1 manifest
names the root and goes on meaning exactly that. Bumping
`recording_format_version` does not invalidate a golden; only retiring a
version does, and that is a deliberate act. Seven of the library's
recordings are version 1, six of them over a disk nobody in this tree can
re-record;
`SessionLibrary.EveryCommittedRecordingIsAFormatThisBuildStillReads`
asserts the version on the files. A recording that carries a line its
version does not allow is refused.

`state_format_version` has no such escape: bumping it invalidates every
golden and the library is re-recorded in the same change.
