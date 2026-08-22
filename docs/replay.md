# Replay

How a run is recorded, how a recording is verified, and what a golden is.

This is the sibling of [`seams.md`](seams.md). M4-R1 (#100) of the plan
of record (#110) built the harness; M4-R2 (#101) is the session library
that uses it. Both began here and are finished at the closeout (#109).

The short version:

> The machine's only clock is virtual (`machine/clock.h`), and everything
> nondeterministic it consumes arrives through the platform interface,
> stamped with that clock. So a run is **keys, ticks and hashes**: record
> the initial conditions, the keys with their ticks, the wall-clock seeds,
> and a hash of the machine's state at checkpoints — and a replay that
> delivers the same keys at the same ticks reproduces the run exactly, on
> every target. Nothing in a recording is content; that is what lets the
> session library be committed.

- [1. What a recording is](#1-what-a-recording-is)
- [2. The canonical state](#2-the-canonical-state)
- [3. Recording](#3-recording)
- [4. Verifying](#4-verifying)
- [5. What can go wrong, and what the report says](#5-what-can-go-wrong-and-what-the-report-says)
- [6. The guard that makes it possible](#6-the-guard-that-makes-it-possible)
- [7. Versions](#7-versions)

---

## 1. What a recording is

Plain text (`machine/replay.h`), one line per fact:

```
amberfolio-recording 1 state=1
program BOOT.EXE 3f1c…
tail 202d4649525354 4c49474854
speed 256
seam probe
file BOOT.EXE 2144 3f1c…
file OVL.BIN 9 a7e0…
key 5000 1e down
checkpoint 1193182 1193182 9b2d… clock=… cpu=… ram=… devices=… …
end 2000000 2000000
```

The **initial conditions** come first: the program and its fingerprint,
the command tail (as hex, because it usually begins with a space), the
speed (the step cost in 1/256ths of a tick — a replay is made at the
speed it was recorded at), every seam that was on, and the **manifest** —
every file in the root directory in the VFS's pinned order, with its size
and SHA-256. Then the **stream**, in tick order: wall-clock seeds, key
events, checkpoints, and the end.

A checkpoint is the tick, the step count, the whole-state hash, and
optionally the first eight bytes of each section's hash. `end` is where
the recording stopped; reaching it means everything before it held.

Everything here is a fact about a run, none of it a byte of a program or
a pixel of a screen (PLAN.md §6: "hashes are committable; screen content
never is").

---

## 2. The canonical state

`machine/state.h` is the versioned, sectioned serialization a checkpoint
hashes. Thirteen sections, in order: `clock`, `cpu`, `ram`, `devices`,
`scheduler`, `keyboard`, `dos`, `wall`, `input`, `console`, `audio`,
`display`, `stop`. Every device writes its own architectural state
through `device::save_state()`; the platform classes write theirs; the
machine writes the order.

**In**: everything the program can observe or that decides what it
observes next — registers and interrupt latches, the clock and step
count, every byte of RAM, every device's architectural state in attach
order, the armed deadlines, the DOS handle table and the position of
every open file, the wall seed, the input still queued, the console bytes
not yet drained, the speaker's edge list as a count and a running digest,
the framebuffer and its generation, and the stop record.

**Out**, by decision, and the reasons matter as much as the list:

| Out | Because |
| --- | --- |
| the speed governor | configuration; the recording names it as an initial condition |
| the seam engine and its toggles | configuration (`seam.h`); the recording names each seam that was on |
| the overlay tracker | an observation of machine events, rebuilt by replaying them |
| the trace ring, the first-touch notices | diagnostic bookkeeping — a run with `--trace` and one without must hash alike |
| every float audio sample | output, not state; the edge list is the canonical thing (`platform.h`) |
| the filesystem's contents | the host's; captured as a manifest of names, sizes and SHA-256s in the preamble |

`state_format_version` is bumped when the bytes change — a device grows a
register, a section is added — and a bump invalidates every golden. A
recording names the version it was hashed under and a player refuses to
compare across versions, so a format change reports itself as a format
change rather than as a thousand divergences. Bumping it means
re-recording the session library (#101); it is a decision, never a side
effect.

---

## 3. Recording

The desktop host records with `--record FILE`:

```sh
amberfolio ./disk GAME.EXE --record session.rec
```

The preamble is written before SDL is even up, so that a run which cannot
be described is refused before a window opens. Then the loop adds three
things and nothing else:

- a `key` line **where a key is posted**, stamped with `machine::time()`
  — the machine's clock is the only stamp a key has, and the post is the
  only moment the machine can see one;
- a `checkpoint` **where a frame ends**, taken after the slice and before
  the frame is presented;
- an `end` line where the run does.

The frame boundary is the machine's own (`renderer::frame_period`, ticks
off the virtual clock and never the wall), which is what makes a
recording made on one target name ticks a replay reaches on another.

**Recording is not free.** A checkpoint hashes every byte of RAM, so the
default cadence — one a frame — costs about a megabyte of SHA-256 sixty
times a virtual second, and makes a debug-build run roughly thirty times
slower than the same run without it. Each line is about four hundred
bytes. That is the right trade for a tool a person points at a problem;
it is *not* the right trade for the committed session library, whose
files also have to stay under the content guard's 256 KiB ceiling. #101
picks the cadence a committed session uses; this file's grammar already
allows any of them, because nothing requires a checkpoint every frame or
requires one to carry its sections.

---

## 4. Verifying

```sh
amberfolio ./disk GAME.EXE --replay session.rec
```

The recording decides the speed, the seams and every key, and the player
applies all three before it checks them. `--speed`, `--seam` and
`--press` are therefore refused alongside `--replay` rather than silently
agreed with, and `--record` and `--replay` together are refused as the
two halves of one thing.

The player (`machine::replay_player`) lives in core because two hosts and
a test harness all have to verify the *same* recording the *same* way,
or "verified on all four targets" means four readings of it. It parses,
checks the initial conditions, delivers the events and compares the
hashes. It never runs the machine — that is the host's loop:

```
run to min(next_tick(), own frame boundary) → apply() → repeat
```

### Why `next_tick()` is not "the next line"

A machine stops **inside** a step. `machine::run()` is

```cpp
while (now_ < until) {
  if (step() == cpu::step_status::stopped) break;
  ++result.steps;
}
```

so the step that exits or refuses is not counted and its ticks are not
spent. A stopped machine and the machine one step short of stopping
therefore stand at **the same tick and the same step count**, differing
only in having stopped — and the only way to get from the second to the
first is to ask the machine to run *past* that tick.

A recording's last checkpoint is almost always the stopped one. A host
held at its tick would run to it, find nothing left to do, and compare a
machine that was never given the chance to stop. So a checkpoint of a
stopped machine is marked `stopped` on its line, and `next_tick()`
answers `never` for it: the host runs on to its own frame boundary, the
machine stops where it stopped before, and the ticks agree because
stopping costs none.

Everything else does hold the host to its exact tick. A key or a wall
seed consumed a tick late is a different run, and `end` is where the
recording stops being one.

This was found the way such things are: the first round trip of a program
that exits diverged in one section, `dos`, at the last checkpoint of
sixty-one, with the step and tick counts identical — the recorded machine
had closed its files on the way out and the replayed one had not yet been
allowed to. `Replay.ARecordingThatEndsInAStopIsReachedByRunningPastIt`
and its companion are the regression tests.

---

## 5. What can go wrong, and what the report says

One line, `amberfolio: replay …`, naming the first thing that differed
and where. The exit code is the run's answer, ahead of the program's own,
on the same reasoning as `--verify`: a run asked to check itself against
a recording is answering the check's question.

| Report | What happened |
| --- | --- |
| `verified checkpoints=N keys=K` | every condition matched and `end` was reached; the process then returns the program's own exit code |
| `refused line=L why=…` | not a recording this player reads, or the initial conditions do not match — the wrong program, the wrong speed, the wrong seams, a file that is not the file |
| `diverged line=L tick=T section=S expected=… actual=…` | a checkpoint's hash was not the machine's; `section` is the first of the thirteen to disagree |
| `diverged … why=the machine ran past an event's tick` | the host overran an event — a frame period that is not the recorder's, or a `stopped` marker that is not true |
| `refused … why=the recording has no end line` | the recording was cut short; incomplete, not diverged, and told apart on purpose |
| `in progress checkpoints=N` | a budget ended the run before `end`; a prefix verified, and a prefix is not the run |

`ctest -L smoke -R records-and-replays` is the round trip as one case:
record the composite with a scripted keystroke, replay it with nothing
but the recording, then tamper with a checkpoint hash and require the
refusal. A check that cannot fail is not one.

And `scripts/sweep.py` runs every committed session
([`tests/sessions/`](../tests/sessions/README.md)) against every target
that can verify one — the desktop host, the native suite and the wasm
module under node — and prints one table. A target that is not built is
skipped and said so, never counted as a pass.

---

## 6. The guard that makes it possible

All of this rests on one property: **virtual time is the only clock.**
Nothing under `core/` may read the host's. That was prose in
`machine/clock.h` from M2 and is now `scripts/check-host-time.sh`, run in
CI's guards job on every push (#78). It refuses the standard library's
clocks and the libc time calls under `core/`, and leaves the hosts —
which are *supposed* to know what time it is — alone.

`scripts/test-guards.sh` is its self-test, including the cases that must
*not* trip: `machine::time()` and `wall_time` are names core uses
legitimately, and a guard that caught them would be one people learn to
work around.

The wall clock a program reads through INT 21h is not an exception to
this. It is a **seed**, set once by the host and read as
`wall().at(time())` — a fixed origin plus virtual elapsed time — so it
advances with the machine and is recordable as a `wall` line at a tick.
The desktop host does not seed it today, which is why no recording here
carries one.

---

## 7. Versions

Two, and they are independent:

- **`recording_format_version`** (`machine/replay.h`) — the line grammar.
  A player refuses another version rather than misreading it.
- **`state_format_version`** (`machine/state.h`) — the bytes a checkpoint
  hashes. A player refuses to compare across versions, because a
  divergence that is really a format change is a false finding.

Both are `1`. A recording names both on its first line
(`amberfolio-recording 1 state=1`). Changing either invalidates every
golden recorded under it; the session library is re-recorded in the same
change, and the reason is written down here.
