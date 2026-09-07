# Sessions

Committed recordings, and the cross-target proof they exist to be.
[`docs/replay.md`](../../docs/replay.md) is what a recording is; this
directory is the goldens every target must reproduce. A recording is
keys, ticks and hashes, so a target that reproduces one has reproduced
every byte of RAM, every device's registers, the scheduler's deadlines,
the DOS handle table and the framebuffer at every checkpoint. Nothing in
one is content (PLAN.md §6).

**Every game session here is stale pending #293.** All 23 were recorded
with the code-wheel seam answering the challenge; since #291 it only
watches, so a replay with a disk arrives at a screen still waiting. The
`contrast`/`identical` checks CI makes read the recordings and are
unaffected.

## What a session is

    tests/sessions/spin.rec       the recording
    tests/sessions/spin.session   the descriptor: what it is, which disk
    tests/sessions/spin/          the disk, when it can be committed

A `.rec` with no descriptor is a failure, not a skip.

**The descriptor grammar** (parsed by `scripts/sweep.py`; `#` lines are
comments):

| Line | Meaning |
| --- | --- |
| `about TEXT` | free text, repeatable |
| `disk NAME` | the disk is `tests/sessions/NAME/`, in the tree |
| `disk external` | the disk is the player's own; matched from `--game-disk` candidates by the `file`/`dir` lines |
| `file PATH SIZE SHA256` / `dir PATH` | every entry of the disk, pinned exactly in both directions |
| `document SHA256` | a document the run presented (a code-wheel PDF); replaced under #293 by a line saying the challenge was already answered |
| `journal-store PATH` / `journal-store external SHA256` | the reader's store the run was made over; the runner copies it before running, since a run writes its log back |
| `contrast BASELINE` | assertion: agrees with BASELINE checkpoint for checkpoint until the seam first matters, then differs to the end |
| `identical BASELINE` | assertion: every checkpoint equal to BASELINE |

Both relations are checked on the files, with no disk, and fail on a
pair that checkpoints at different ticks (a comparison that would
otherwise pass by comparing nothing). `scripts/test-sweep.sh` asserts
each failure mode: identical throughout, divergent from the first
checkpoint, divergent then rejoined, different ticks.

**A pair puts its inputs at the same ticks in both halves**, because a
frame that carries an input is checkpointed whatever the cadence says.
**A pair is told the same date**: `--wall` on both halves, or
`--wall none` (what every recording here was made on).

## The sessions

| Session | Disk | Seams | Relation | What it pins |
| --- | --- | --- | --- | --- |
| `spin` | `spin/SPIN.EXE`, 34 bytes, `JMP $` behind an MZ header | none | | four frames of a machine keeping time: PIT, 8259, scheduler, renderer deadline, clock. The only committed binary; the content guard names its path |
| `party` | external, pristine | code-wheel | | `docs/playable.md` leg 0: a character generated and added, `BEGIN ADVENTURING` into the opening event at 15,1 W. 144 checkpoints |
| `save` | external, pristine | code-wheel | | legs 0, 1, 3: the tour to 0,4 W, the game saved to slot A from camp. 254 checkpoints |
| `load` | external, **the disk `save` wrote** | code-wheel | | slot A loaded, the party back at 0,4 W. 100 checkpoints |
| `fight` | the disk `save` wrote | code-wheel | | leg 2: twelve steps north into orcs, `QUICK`, `THE END`. 177 checkpoints |
| `fight-cheat` | same | + cheat-invulnerable | `contrast fight` | 126 of 177 identical, divergent from tick 274,951,600; the seam fires nine times |
| `temple` | external, **the shipped save slots** | code-wheel | | leg 5: slot A, the temple at 3,1, `CURE BLINDNESS` bought for a thousand gold. 181 checkpoints |
| `camp` | shipped slots | code-wheel, cheat-wound-party (pulled) | | leg 7 without the Fix: slot B, `ENCAMP`, the party wounded to one hit point each, `REST`. 112 checkpoints |
| `camp-fix` | same | + encamp-fix | `contrast camp` | the same run pressing `FIX` instead of Rest at the same tick: 91 of 112 identical, divergent from tick 216,799,088; `fired=11`. Both halves pull the wound cheat at the same tick, so the only difference is one keystroke |
| `walk` | shipped slots | code-wheel | | leg 8 without the map: slot A, forty-eight moves to the armourer at 8,11, a `Tab` nothing claims. 203 checkpoints |
| `walk-map` | same | + automap | `contrast walk` | 90 of 203 identical, divergent from tick 218,787,888 (three frames after the `Tab`); the panel is in every hash after it |
| `wild` | shipped slots | code-wheel | | slot J, already on a wilderness area, eight steps north; the mode byte becomes 3 at frame 9,552. 140 checkpoints |
| `wild-trail` | same | + explored | `contrast wild` | 107 of 140 identical, divergent at the **arrival** (tick 204,866,288), because fog marks the unknown. Recorded at the grey checker, radius one; #293 re-records it |
| `reader` | shipped slots, `journal-store tests/visual/reader-store.txt` | code-wheel, journal | | F1, the section cycled, entry three opened by number, paged, closed. 156 checkpoints. Draws a page #305 now draws full-screen; re-recorded under #293 |
| `notes` | same | code-wheel, journal | | `Notes` opens an empty log; six adventuring keys reach nothing while it is up. 146 checkpoints |
| `cite` | external, pristine; `journal-store external SHA256` | code-wheel, journal | | a real citation (#232): a new party to the city hall at 3,4 E, whose event names four proclamations, the first opened with no key pressed. 291 checkpoints |
| `subset-map-reader` | shipped slots | code-wheel, automap, journal | | the panel up, an entry opened over it, the map given back and put away. 146 checkpoints. Re-recorded under #293 |
| `quiet` | shipped slots | code-wheel | | the baseline: slot A, four steps walked. 126 checkpoints |
| `quiet-automap` | same | + automap | `identical quiet` | Tab never pressed |
| `quiet-encamp` | same | + encamp-fix | `identical quiet` | the camp screen never opened |
| `quiet-cheats` | same | + all three cheats | `identical quiet` | none pulled |
| `quiet-explored` | same | + explored | `identical quiet` | the overworld never shown; the points are reached half a million times |
| `quiet-journal` | same | + journal | **`contrast quiet`** | 111 of 126 identical: `Notes` goes on the party's bar the moment the bar is drawn, in `cpu`, `ram`, `devices`, `display`, `audio`. The enhancement, not a leak; `identical` is not loosened to fit |
| `quiet-all` | same | every seam | `identical quiet-journal` | eight seams armed, none triggered, no more machine than the journal alone |

## The matrix, by seam

| Seam | On and exercised | On and never triggered |
| --- | --- | --- |
| `code-wheel` | every game session, at the challenge | none yet; #293 adds `identical quiet` with the challenge unanswered |
| `encamp-fix` | `camp-fix` | `quiet-encamp` |
| `automap` | `walk-map` | `quiet-automap` |
| `journal` | `reader`, `notes`, `cite` | `quiet-journal` (a `contrast`) |
| `explored` | `wild-trail` | `quiet-explored` |
| the cheats | `fight-cheat`; `camp-fix` pulls `cheat-wound-party` | `quiet-cheats` |

Subsets: `quiet-all` (all on), `subset-map-reader` (two seams wanting the
same pixels), `camp-fix` (the Fix with a cheat), `wild-trail` (explored
without automap; they share a store).

## Running them

```sh
python3 scripts/sweep.py                         # every session, every built target
python3 scripts/sweep.py --targets contrast      # the relations only; no disk, runs in CI
python3 scripts/sweep.py --game-disk DIR ...     # repeatable; each session picks the disk its pins match
python3 scripts/sweep.py --document FILE_OR_DIR  # or $AMBERFOLIO_DOCUMENT
python3 scripts/sweep.py --journal-store FILE_OR_DIR  # or $AMBERFOLIO_JOURNAL_STORE
python3 scripts/sweep.py --pin NAME --game-disk DIR   # write a descriptor's file/dir lines from a pristine snapshot
```

Outcomes are `ok`, `FAIL`, and `SKIP` with what was missing. A sweep
that verified nothing exits non-zero; `scripts/test-sweep.sh` asserts
that. The disk handed to a game session must be a **pristine snapshot**
(the sweep copies it before each run); a file the pin does not name is
"not that disk" as much as a missing one.

On the wasm module:

```sh
node build/wasm/hosts/web/<config>/drive.mjs <disk> START.EXE --replay tests/sessions/reader.rec \
  --journal-store tests/visual/reader-store.txt --quiet
```

`drive.mjs` puts an empty directory into the module by putting and
removing a placeholder, since the ABI has no `mkdir` (#273). All 23 game
sessions have replayed on both hosts with identical seam and
host-service lines.

## Checkpoint cadence

Game sessions use `--record-every 128`. A frame that posted a key and
the frame the run ends on are always checkpointed. `docs/replay.md` §3.

## Who checks them

On every push, from the same files: **native** (`SessionLibrary.*` in
`tests/core/machine/session_test.cpp`, through
`af_machine_verify_recording`), **wasm** (`hosts/web/tests/smoke.mjs`,
same ABI call, different compiler and SHA-256), **desktop**
(`sdl-host-verifies-a-session`, through `--replay`, with its own device
wiring). The first two also tamper with a hash and require the refusal.
Only sessions whose disk is in the tree run in CI; the rest are `SKIP`
there and are checked on the maintainer's machine.

## When one of these may be re-recorded

Only when the machine it describes legitimately changes:

- `state_format_version` is bumped;
- `recording_format_oldest_read` is bumped (a format retired; bumping
  `recording_format_version` alone does not, see `docs/replay.md` §7);
- the reference device set's **attach order** changes
  (`hosts/sdl/src/main.cpp`, `core/src/abi.cpp`'s `reference_devices`,
  `tests/programs/machine_harness.cpp` move together);
- what `machine::reset()` leaves behind changes;
- a seam is added after the session was made and the session claims to
  carry them all (`quiet-all`);
- a seam the session turns on legitimately changes what it does. The
  test: the change was chosen, argued on an issue, and visible in the
  seam's source, never that a red line went green.

If a session stops verifying and none of the above changed, the machine
changed and the finding is real.
