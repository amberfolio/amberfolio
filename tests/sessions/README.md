# Sessions

Committed recordings, and the cross-target proof they exist to be.
[`docs/replay.md`](../../docs/replay.md) is what a recording is; this
directory is the goldens every target must reproduce. A recording is
keys, ticks and hashes, so a target that reproduces one has reproduced
every byte of RAM, every device's registers, the scheduler's deadlines,
the DOS handle table and the framebuffer at every checkpoint. Nothing in
one is content (PLAN.md §6).

**`cite` is the one session still owed a re-record (#293).** Every other
game session here was made on the boot the challenge never stops, and
each says so in one line. `cite` needs a store that is a real ingestion
of a real journal, pinned by digest, and it needs a new script as well:
since #346 a citation writes a line on the `Notes` log and draws
nothing, so the panel that is in every hash after its checkpoint is a
panel the seam no longer paints. Reaching the log with `Notes` and
opening the first proclamation from it is what it should do instead.

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
| `document SHA256` | a document the run presented, by digest, for a seam with a possession gate. No seam in this build has one (`docs/seams.md` §5), so nothing here says it |
| `code-wheel-answered` | the code-wheel challenge had already been answered when this run was made (#291). Not a file and not a digest: since #290 the possession proof is the act, so what a replay is told is a condition. Every game session but `boot` and `boot-wheel` says it |
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
| `boot` | shipped slots | none | | the boot itself, nothing pressed, ending on the screen that asks for the code word. 72 checkpoints. With `boot-wheel`, the only pair here whose challenge is **not** answered |
| `boot-wheel` | same | code-wheel, unanswered | `identical boot` | the seam watching: `fired=144`, once per call of the program's string compare that lands in the word table, and not one of them moves the machine |
| `party` | external, pristine | code-wheel | | `docs/playable.md` leg 0: a character generated and added, `BEGIN ADVENTURING` into the opening event at 15,1 W. 131 checkpoints |
| `save` | external, pristine | code-wheel | | legs 0, 1, 3: the tour to 0,4 W, the game saved to slot A from camp. 241 checkpoints |
| `load` | external, **the disk `save` wrote** | code-wheel | | slot A loaded, the party back at 0,4 W. 87 checkpoints |
| `fight` | the disk `save` wrote | code-wheel | | leg 2: twelve steps north into orcs, `QUICK`, `THE END`. 164 checkpoints |
| `fight-cheat` | same | + cheat-invulnerable | `contrast fight` | 113 of 164 identical, divergent from tick 246,949,296; the seam fires nine times and the character is still standing at the end |
| `temple` | external, **the shipped save slots** | code-wheel | | leg 5: slot A, the temple at 3,1, `CURE BLINDNESS` bought for a thousand gold — `PLATINUM 1386` on the sheet at the end. 167 checkpoints |
| `camp` | shipped slots | code-wheel, cheat-wound-party (pulled) | | leg 7 without the Fix: slot B, `ENCAMP`, the party wounded to one hit point each, `REST`. 100 checkpoints |
| `camp-fix` | same | + encamp-fix | `contrast camp` | the same run pressing `FIX` instead of Rest at the same tick: 79 of 100 identical, divergent from tick 188,955,888; `fired=11`. Both halves pull the wound cheat at the same tick, so the only difference is one keystroke |
| `walk` | shipped slots | code-wheel | | leg 8 without the map: slot A, forty-eight moves to the armourer at 8,11, a `Tab` nothing claims. 190 checkpoints |
| `walk-map` | same | + automap | `contrast walk` | 77 of 190 identical, divergent from tick 190,944,688 (three frames after the `Tab`); the panel is in every hash after it |
| `wild` | shipped slots | code-wheel | | slot J, already on a wilderness area, eight steps north to 3,24 N. 103 checkpoints |
| `wild-trail` | same | + explored | `contrast wild` | 72 of 103 identical, divergent at the **arrival** (tick 178,216,368), because fog marks the unknown |
| `reader` | shipped slots, `journal-store tests/visual/reader-store.txt` | code-wheel, journal | | `Notes`, the row the cursor is on opened full-screen, `NEXT` to page 2/2, and out. 115 checkpoints. It was F1 and a typed number until #346 |
| `notes` | same | code-wheel, journal | | `Notes` opens the log the store carries — two rows, `*` on the unread — and six adventuring keys reach nothing while it is up. 111 checkpoints |
| `cite` | external, **pristine**, `journal-store tests/visual/cite-store.txt` | code-wheel, journal | | the program's own citation (#232): a new party east to the city hall at 3,4, whose event names proclamations LXIV, LXXVIII, CIX and LIX, then `Notes` and the first of them opened off the log. 261 checkpoints. Its store was somebody's own journal behind an external digest until #290; four proclamations written here do the same job and let anybody verify it |
| `subset-map-reader` | shipped slots | code-wheel, automap, journal | | the panel up, an entry opened over it, the map given back and put away. 108 checkpoints |
| `quiet` | shipped slots | code-wheel | | the baseline: slot A, four movement keys — one step, a wall, a turn, a wall. 90 checkpoints |
| `quiet-automap` | same | + automap | `identical quiet` | Tab never pressed |
| `quiet-encamp` | same | + encamp-fix | `identical quiet` | the camp screen never opened |
| `quiet-cheats` | same | + all three cheats | `identical quiet` | none pulled |
| `quiet-explored` | same | + explored | `identical quiet` | the overworld never shown; the points are reached half a million times |
| `quiet-journal` | same | + journal | **`contrast quiet`** | 76 of 90 identical: `Notes` goes on the party's bar the moment the bar is drawn, in `cpu`, `ram`, `devices`, `display`, `audio`. The enhancement, not a leak; `identical` is not loosened to fit |
| `quiet-all` | same | every seam | `identical quiet-journal` | eight seams armed, none triggered, no more machine than the journal alone |

## The matrix, by seam

| Seam | On and exercised | On and never triggered |
| --- | --- | --- |
| `code-wheel` | every game session but the boot pair: the challenge is answered before the run and the seam steps over the boot's call to the check | `boot-wheel`, on and unanswered: it watches 144 times and triggers nothing |
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
python3 scripts/sweep.py --document FILE_OR_DIR  # a gated seam's document; none today
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
  --code-wheel-answered --journal-store tests/visual/reader-store.txt --quiet
```

`drive.mjs` puts an empty directory into the module by putting and
removing a placeholder, since the ABI has no `mkdir` (#273). It takes
`--code-wheel-answered` in the same spelling the desktop host does, and
every game session but the boot pair needs it: pass it to `boot-wheel`
and the replay diverges in the boot's tail, which is the descriptor's
line saying something.

Every re-recorded session but `cite` was replayed on the module this
way as well as on the desktop host. Doing it found one thing: this
driver never restored the store's read log into the machine, which the
dev page and the desktop host both do, so every session that *opens*
the `Notes` listing diverged here at the frame the listing is drawn.
`quiet-journal`, which only splices `Notes` onto the bar, verified
throughout, which is why nothing had said so before.

## Checkpoint cadence

Game sessions use `--record-every 128`, all of them since #293 — half of
them were on 100 before, which is why the counts in the table above are
smaller than the ones that used to be there. A frame that posted a key
and the frame the run ends on are always checkpointed.
`docs/replay.md` §3.

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
