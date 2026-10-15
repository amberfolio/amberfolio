// SPDX-License-Identifier: AGPL-3.0-only
//
// Every option the desktop host takes: what each one is for, the struct
// they parse into, and the parser (#472, split from main.cpp).
//
// What follows is the option notes that used to head main.cpp, moved with
// the parser they describe. `--verify` and `--press` are explained in
// window_checks.h and the two volume keys in audio_device.h, beside the
// code they are about.
//
//
// The boot driver
// ---------------
//
// M3 boots the player's own copy, and the method (#94) is a loop: run it,
// read the line it stopped on, widen the one service that line names,
// run it again. Everything in this host that is not M2's host loop is in
// service of making that line worth reading.
//
//   --steps N        stop after N scheduling steps, wherever the program
//   --until TICKS    stop at TICKS of virtual time
//
//     A hang is otherwise the one failure this host cannot report: the
//     machine is running, nothing has refused anything, and the process
//     sits there. A budget turns it into an ending with a CS:IP, a step
//     count and a trace on it — which is a worklist entry, where a
//     hung process is not. The budget is clamped into the run slice
//     rather than checked after it, so the run ends on the step asked
//     for and not somewhere inside the frame after it: a stop you cannot
//     reproduce exactly is not a worklist entry either.
//
//   --dump PREFIX    write PREFIX.ppm, PREFIX.wav and PREFIX.edges
//
//     The frame the machine composed and the sound it made, in two
//     formats every viewer opens (dump.h). docs/machine.md §7's warning
//     about goldens is the argument: "the title renders" is a claim to
//     look at, and no test in this repository will ever run the file
//     that produces it.
//
//     `PREFIX.edges` is the third, and it is a different kind of thing
//     from the other two (M4-A1, #106). The WAV is *a rendering* of the
//     sound at 48 kHz through a box filter; the edges file is the sound
//     as the machine holds it — one line per output transition, `tick
//     level`, in PIT input ticks. platform.h calls the edge list the
//     canonical audio state and says the floats are not it, and #106
//     wants the two questions kept apart: whether the machine made the
//     right edges at the right ticks, and whether the render of them is
//     right. Only the second used to be inspectable at all.
//
//     Written as the run goes, so it survives a run that ends badly, and
//     it ends with a `# edges N dropped M` line saying whether it is all
//     of them.
//
//   --dump-every N   also write PREFIX-NNNNNN.ppm every N frames
//
//     A run is a film, and one frame of it is a still. Everything a
//     player-supplied copy does past the title happens over tens of
//     virtual seconds - a menu answers, a tour walks, a fight resolves -
//     and "what did the screen do" is a question a single final frame
//     cannot answer. Needs `--dump`, whose prefix it shares; the frame
//     number in the name is the same one `--press KEY@FRAME` counts in,
//     so a still and the keystroke that caused it are named in the same
//     units.
//
//     Deliberately every Nth frame rather than every frame: sixty files
//     a virtual second fills a disk before it tells anyone anything, and
//     the caller is the only one who knows how fast the thing they are
//     watching moves.
//
//   --watch OFF[:N]  print a data word every time it changes
//
//     The third instrument of the same kind, and it arrived for the same
//     reason the other two did: a leg of docs/playable.md was impractical
//     without it. `--dump-every` says what the screen did and `--trace`
//     says what the program asked DOS for; neither says *where the party
//     is*, and a city service three streets away is a navigation problem
//     before it is an emulation one (#104).
//
//     `OFF` is a hexadecimal offset in the program's data segment — the
//     anchor its globals are counted in, and the one a seam reads them
//     through (seam_cheats.cpp) — and `N` is 1 or 2 bytes, default 1.
//     Repeatable. A line is printed only when one of the watched values
//     differs from the last line's, so a run that walks twenty squares
//     prints twenty lines:
//
//         amberfolio: watch frame=011300 ds=0CDC 6AAD=05 6AAE=0C 6AAF=06
//
//     The segment is on every line because it is not always the data
//     segment: DS is whatever the frame boundary landed on, and a frame
//     that ends inside an interrupt or an overlay has it pointed
//     somewhere else. What a watch has already said is remembered per
//     segment, so each of those says its piece once and then stays quiet,
//     and the lines that are left are the ones where the watched values
//     actually moved. Filter on the segment the globals live in and the
//     log is the movement and nothing else.
//
//     It reads `memory_map::ram()` and not the bus, so it takes no bus
//     cycle, disturbs no EGA latch and cannot make a notice of its own: a
//     watch has to be able to say a run was clean, which it could not do
//     if watching were itself something the run did.
//
//     What it is *not* is a debugger, and deliberately: no writes, no
//     breakpoints, no expressions. Machine state is the seam engine's to
//     touch (PLAN.md §5) and nothing else's, and this reads.
//
//   --trace          keep the trace ring, and print it with the report
//
//     Off by default, in the machine, at a cost of one branch per step
//     (machine/trace.h). What it answers is the question a bare address
//     cannot: how the program got there.
//
//     The ring's third channel is the naming file calls (#121) — the last
//     thirty-two opens, creates, mkdirs, unlinks and closes, each with
//     the path it resolved to and what DOS answered:
//
//         amberfolio: stop trace file=open \POR\POOL.CFG handle=0000 path_not_found from=0B58:1458
//
//     A failed open is a legitimate DOS answer and stays one, so nothing
//     stops and nothing is refused; the line is the whole of the fix.
//     Without it "the program has spent its startup asking for a file
//     that is not there" is a directory audit rather than a report.
//
//     Since M4 (#97, #99) it also prints every file read the overlay
//     tracker records as it lands — the file, the offset, the length,
//     where it went and the digest of the bytes (machine/overlay.h):
//
//         amberfolio: overlay GAME.OVR offset=38919 length=4735 at=279D:0000 sha256=...
//
//     Those lines are the facts a seam qualified by an overlay is written
//     from, read off the program's own loads rather than inferred from
//     anything, and they are how the cheats seam's module was found.
//
//     And since #268, when the automap seam is on, which evidence made
//     that map's door leaves — the map's own shut faces, and the seam's
//     table of every shut face in the shipped data:
//
//         amberfolio: automap doors frame=011000 disk=8 area=0D geo=0D seen=0200 table=0200 drawn shut=1 kind-seen=1 kind-table=0 no-evidence=0
//
//     `seen` and `table` are the two sources' masks and `drawn` counts
//     the leaves on the panel by which of the four rules put each one
//     there. Printed when any of it moves. All four rules draw the same
//     yellow, so without this a leaf on a screenshot cannot be told from
//     a leaf the table guessed — which is exactly what #268 was about.
//
//   --save-sidecars, --no-save-sidecars
//                    keep this playthrough's progress beside its
//                    saves, or do not
//
//     M5-E2c (#173) and #351. Two enhancements learn something as a
//     party plays — what the automap has explored, and which journal
//     entries the game has cited — and both are observation rather than
//     machine state, so both are gone when the machine stops. This
//     writes them, when the program saves slot `L`, into
//     `\SAVE\AFMAP<L>.DAT` and `\SAVE\AFSEEN<L>.DAT` — files of this
//     project's own, beside the program's saves and never inside one —
//     and reads them back when the program loads that slot, so two
//     playthroughs do not share one map or one list. The wilderness the
//     explored overlay draws is in the first of them already: it keeps
//     no records of its own and reads the automap's.
//     **Neither file appears until there is something to put in it**: a
//     sidecar with no records in it is its header and nothing else, and
//     one of those is written over a file that is already there and
//     never as a new one (`slot_store.h`).
//
//     Off by default, and deliberately: this is a real directory of the
//     player's, and a file appearing in it changes it. Every recorded
//     session in `tests/sessions` pins its disk by name, size and
//     SHA-256, so a sidecar written by a verification run would make the
//     next run's disk a different disk.
//
//     **Which is why a launch with a person in it asks** (#385), once,
//     and keeps the answer in the settings file beside everything else a
//     player chose. `--no-save-sidecars` is the other side of it: an
//     answer for one launch, whatever is remembered. A run that is
//     driven, headless, replayed, recorded, dumped or verified is asked
//     nothing at all and keeps them off — a prompt nobody is watching
//     never comes back, and a sweep that answered its own question would
//     be writing into the disks it replays over.
//     `sidecar_consent.h` has the rule and the reasons.
//
//   --seam ID        turn on one seam, by its config key
//
//     PLAN.md §5's opt-in runtime patches, off unless named here — and
//     refused unless the program that was loaded is the one the seam's
//     addresses are facts about, which is what its fingerprint is for
//     (machine/seam.h). Repeatable. Every enabled seam is printed at
//     startup, because a run that had one on is not the same run as one
//     that did not and the log has to say so.
//
//     `code-wheel` asks **once** (#291): with it on, the first launch
//     shows the challenge exactly as the machine always did, and the
//     seam does nothing but watch; answer it and the seam latches that,
//     and a launch after that is not asked at all. It was gated on a PDF
//     of the wheel until #290, and nothing in this build is gated now.
//
//   --code-wheel-answered
//                    say the code-wheel challenge has already been
//                    answered on this copy
//
//     What the `code-wheel` seam waits for (#291). Without it the seam
//     is on and watching: the game asks the question as it always did,
//     and answering it correctly is what turns this on for the rest of
//     the run. With it, the challenge is never drawn.
//
//     **It says so for this run and writes nothing.** Answering the
//     game's own question is what puts a copy in the store below; this
//     flag is how a driven run states the condition it wants to be in
//     without a person at the keyboard.
//
//   --code-wheel-store PATH
//                    where the copies that have answered are remembered
//
//     M6-C1b (#292). One small text file, one line per copy, keyed by the
//     program's SHA-256 (`host/code_wheel_store.h`). Read before the
//     first instruction — a copy in it starts with the challenge already
//     answered — and written the moment the seam says somebody answered
//     one. Without this flag it is `code-wheel.txt` in this platform's
//     per-user data directory, beside the journal's text.
//
//   --forget-code-wheel
//                    empty that store and be asked again
//
//     The whole of "forget it": the file is emptied before the run, so
//     this launch asks the question and every launch after it does too,
//     until somebody answers. A player can equally delete the file, and
//     the format is one they can read.
//
//   --config PATH    where this player's settings are
//   --no-config      ignore whatever is there
//   --remember       write this run's settings into it
//   --forget-config  empty it and be a first run again
//
//     M6's config (#382), and the thing main.cpp's own comments had
//     been pointing at since #174. `config.txt`, beside the journal's
//     text in this platform's per-user data directory, holding the game
//     directory and program, the seams, the journal engine, the volume,
//     the mute, the speed, the scale and the sidecars answer.
//     `desktop_config.h` has the format and the argument for every part
//     of it; docs/hosts.md 2a is where a player reads about it.
//
//     Three rules, and each is somewhere a host could have been
//     careless:
//
//       * **flag > config > default**, stated once as `prefer()` and
//         checked once. A flag is never overruled, *including* a flag
//         that names the default, which is why `given_on_the_command_line`
//         exists rather than a comparison against a default value.
//       * **only `--remember` writes.** A host that saved its settings
//         at the end of every run would make the next run's seams
//         whatever the last command line happened to say -- so a driving
//         script's `--seam automap` would leave the seam on for a player
//         who never chose it. Every seam is off for somebody who never
//         chose one, and asking is how that stays true.
//       * **a replay reads no config at all**, and says so when there
//         was one to ignore. A recording is verified by exact comparison
//         on whatever machine runs it (docs/replay.md), and a settings
//         file that reached the run would make its answer a property of
//         the desk it ran at.
//
//     A file this build cannot read is a loud line naming the line it
//     stopped on, a clean start on the defaults, and the file left where
//     it is. Never half-read, never repaired, never guessed at.
//
//   --document PATH  present a document the player holds
//
//     A possession gate, which demonstrates the player holds the
//     document and no more (PLAN.md §5). This is the presenting side
//     (#171): the file is read, hashed, and dropped. Nothing is parsed
//     and nothing is kept. **No seam is gated today** — the code wheel's
//     was the one, and #290 took it off — so what this does now is name
//     what it was handed.
//
//     `PATH` is a path on *this* machine, not on the emulated one: a
//     code wheel lives wherever a person keeps their PDFs, which is very
//     often not inside the game directory. Repeatable, because a player
//     may hold both documents.
//
//     A document this build does not know is **reported, not guessed**:
//     the line says so and prints the fingerprint of the file, which is
//     what turns "this does not work" into something somebody can add to
//     `machine/document.h`'s table. A gate that armed on an unrecognized
//     document would be a gate that armed on anything.
//
//     Configuration, like `--seam`: it is applied before the first step,
//     it is not machine state, and a run with a document presented and
//     every seam off is byte-for-byte the run without one.
//
//     **The flag is not the control.** A file dropped on the window goes
//     through the same path and says the same two lines, and says them
//     in the toggle panel as well as on stderr (#384, `document_control.h`
//     for the words). A player does not have to know a flag exists to
//     show this build a document, which is what M6 is about; the flag
//     stays because a driving script has no hands.
//
//   --journal PATH   ingest the player's own Adventurer's Journal
//
//     M5-E3 (#174), and the one place this host reads *inside* a
//     document rather than only hashing it. The file is presented as
//     `--document` presents one — so a journal-gated seam arms — and then
//     each entry's scan is followed to its offset, decoded, read by an
//     OCR engine and kept as text (`host/journal_ingest.h`).
//
//     An edition this build does not know the insides of is reported
//     with its fingerprint and nothing is read: the offsets are only
//     true of one file, and following them into another produces
//     twenty failures rather than one sentence. `known_journals()` has
//     one row — the GOG release's own journal (#214) — so that is
//     what every *other* journal gets, and `docs/journal.md` §3 is how
//     an edition is added.
//
//   --journal-store PATH  where the text a journal ingested lives
//
//     Defaults to `journal.txt` under this platform's per-user data
//     directory, beside where M6's configuration will live; the line
//     this host prints after an ingestion says which file it used. The
//     store is read before an ingestion and written after, which is what
//     makes a correction survive one (`host/journal_store.h`).
//
//     It is read at the start of **every** run, `--journal` or not: an
//     ingestion happens once and the reading happens for ever after
//     (M5-E4, #175), so a player who ingested their journal last week
//     starts today's run with `--seam journal` able to answer.
//
//   --journal-ocr PATH|none  which OCR engine to read with
//
//     The player's own Tesseract (`tesseract_ocr.h` says why it is run
//     rather than linked), and since #382 it is **discovered** rather
//     than named: beside the binary first, then each directory of
//     `PATH`, then a report of the filename and every place it was
//     looked for. `ocr_discovery.h` has the order and the argument.
//
//     `none` ingests every image and stores no text, which is also what
//     happens when no engine is found — said in as many words, with the
//     places listed, rather than quietly recognizing nothing.
//
//   --journal-probe  add the synthetic probe edition, for checks
//
//     Test apparatus, and named as such: `host/journal_probe.h` builds a
//     document this project generates, so the whole ingestion can be
//     driven in CI without a document it did not make. It adds the probe
//     edition to *this run's* table and installs the fixture engine that
//     answers for exactly the probe's pixels; a player's build knows
//     nothing about either, the same way it knows nothing about the web
//     host's probe seam.
//
//   --cite-all-journal
//                    a debug cheat: every entry onto the Notes log
//
//     Puts every entry, tavern tale and proclamation the journal store
//     holds onto the journal's own log, so `Notes` (with `--seam
//     journal`) lists all of them with a `*` on each until it is opened
//     — a proof-reading surface for the text an OCR engine produced,
//     ninety-odd entries a person would otherwise reach one number at a
//     time (#301). Entry 1 is at the top; every row carries this run's
//     date. It is a *cheat*, a switch that breaks the journal's own
//     rule that the log fills as the game is played (PLAN.md §5 item
//     6), and it is off unless asked for.
//
//     **The log stays filled until you empty it yourself.** It is
//     written back into the store above the same way a real citation is,
//     so it survives every later run: to have the game start citing
//     afresh, delete the `seen` lines from the store file, or the file.
//     With no journal ingested there is nothing to cite, and this says
//     so and leaves the log alone.
//
//   The journal reader itself is a seam, not a flag: `--seam journal`
//   turns it on, and then the entry the game cites opens on the game's
//   own screen and F1 opens any other (M5-E4, #175, `machine/journal.h`).
//   It reads the store above and never writes it.
//
//   --seams          list every seam this build carries, and exit
//
//     The toggle surface M4-F4 (#98) asks for: each seam's id, its
//     description, and where it stands against the program that was
//     loaded — off, on, or unavailable with the reason. Printed after
//     the load and after any `--seam` flags have been applied, so the
//     listing is the state the run would have started in, and then the
//     process exits 0 without running anything. The edition line it
//     prints beside the fingerprint is the other half of #95: which
//     known edition the file is, or that it is not one, in which case no
//     seam is available (machine/edition.h).
//
//   --seam-panel     open with the toggle panel up (#383)
//
//     The same five facts, painted over the window instead of printed:
//     each seam's name, its state, `fired` as a **number**, the refusal
//     reason and the document it waits for. `--seams` answers a
//     question and exits; this is the panel a player works, and the
//     right mouse button opens and closes it whether or not this flag
//     was given. Up and down pick a row and Return toggles it, through
//     the same `enable()`/`disable()` a `--seam` flag takes.
//
//     A toggle made in the panel is **written to the config file**, and
//     that is the difference between a person and a script: `--seam` is
//     remembered only by `--remember` (a driving script's flag is not a
//     player's choice), where a click in a panel is nobody but a
//     player. A run with `--no-config` says so and writes nothing.
//     Absence of a remembered choice is *off*, never "unset means
//     inherit" -- a player who has never opened the panel gets every
//     seam off, which is the fidelity invariant and not a preference.
//
//     The panel needs a window, and a replay's seams are the
//     recording's, so it is refused with `--headless` and `--replay`
//     for the reasons `--keyboard` and `--pull` are.
//
//   --save-layer     which of the disk's files are the player's (#208)
//
//     Two things, either side of the run. Before it, with the edition
//     line, the table itself: one row per path pattern, what kind of
//     file it is, whether a slot is incomplete without it, and a line
//     saying what it holds. After it, the disk read against that table
//     — the files the layer claims, each with the slot letter and the
//     party-member index its name carries.
//
//     It is the same question a browser has to answer before it writes
//     `\SAVE\` into its own storage, and the reason it is a table
//     rather than a heuristic is that the publisher's bytes are on one
//     side of the line and the player's on the other. A program this
//     build has no table for says so and lists nothing, which is the
//     honest answer and not a failure (machine/save_layer.h).
//
//   --vfs-list       list every file on the disk, after the run
//   --vfs-get PATH   read one file back through the door, after the run
//   --vfs-remove PATH  delete one file, after the run
//
//     M5-D2's door (#170), driven against a real directory. The wasm
//     host reaches these operations through `af_machine_vfs_*`, over an
//     in-memory filesystem a browser handed it a file at a time; this is
//     the same three operations over `directory_vfs`, so the pair can be
//     compared and #173's exploration sidecar can be checked on either
//     host. The listing walks the whole tree and is **files** - a path
//     and a size per line, in the pinned walk order core decides
//     (machine/vfs.h), which is why an empty directory does not appear.
//
//     They run **after** the program has exited, because the question
//     they exist to answer is what the run left behind: what is in
//     `\\SAVE\\` once the game has saved.
//
//     `--vfs-get` prints the file's size and the SHA-256 of the bytes
//     that came back, and not the bytes. Every byte goes through the
//     read, which is what is being checked; putting a player's file into
//     a log would be putting it somewhere it does not belong, and a
//     digest of what was read is a stronger claim than a hexdump anybody
//     would actually check by eye.
//
//     `--vfs-remove` deletes a real file on the player's disk. It says
//     so on the line before it does it, because this host's filesystem
//     is not a sandbox and a flag that reads like a test fixture is
//     exactly the one somebody runs on a real installation.
//
//   --speed NAME     which machine to be: xt, turbo, at or 386
//
//     The virtual clock's step cost (machine/clock.h), by the names the
//     presets already have. `xt` is the default and is the machine the
//     game was written for — a 4.77 MHz 8088 at about 298,000
//     instructions a second, which is slow enough to watch a title
//     screen paint itself line by line, because that is what an XT did.
//
//     The other three are not a fast-forward and not a hack: they are
//     the faster machines the same software ran on, and they change nothing
//     about what the emulator computes — virtual time still governs
//     every deadline, every tone and every tick, so a run at `at` is as
//     deterministic and as replayable as one at `xt`. What changes is
//     how much of it fits in a second of yours.
//
//     Which of them is *right* is a playtest question and not settled
//     here (#107, PLAN.md §9's note on pacing feel). This flag exists so
//     that the question can be asked by eye.
//
//   --fast N|max     run virtual time N times faster than the wall
//
//     The other way of going faster, and it is not the same way.
//     `--speed` changes *which machine this is*; this changes *how fast
//     you watch it*, and the difference is measurable rather than
//     philosophical. Booting the maintainer's copy splits into about 104
//     seconds of computation and about 21 seconds of pause the program
//     times against the BIOS tick. A faster processor divides the first
//     number and leaves the second alone, so twenty times the CPU is
//     still twenty-six seconds; fast-forward divides both, and twenty
//     times the wall rate is six.
//
//     Nothing inside the machine can tell. The step count, the tick
//     count, the frames composed and every byte of the framebuffer are
//     identical to a run at `--fast 1` — the only thing that changes is
//     how long this host sleeps at the bottom of the loop, which
//     platform.h's design essay is careful to keep outside machine state
//     for exactly this reason. That is what makes it safe to hand a
//     player a fast-forward before the replay harness exists (#100).
//
//     `max` does not sleep at all, and is what `--headless` has always
//     done — so the flag means nothing there and says so rather than
//     being quietly ignored. How fast `max` actually is depends on the
//     host: this interpreter runs about 22 million steps a second, which
//     at the default speed is roughly seventy times real time.
//
//     Audio is the one thing fast-forward spoils, unavoidably: the
//     speaker is pulled by a real 48 kHz device that cannot be hurried,
//     so anything past about 1x is producing sound faster than anything
//     can consume it. `--verify` counts the resyncs.
//
//   --record FILE    write this run down as a recording
//   --record-every N take a checkpoint every N frames, not every one
//   --replay FILE    be the run a recording describes, and check it
//
//     The two halves of machine/replay.h, and the reason that file says
//     the player never runs the machine: this loop does. Recording adds
//     three things to it — a key line where a key is posted, a checkpoint
//     where a frame ends, an `end` line where the run does — and the
//     preamble, written before SDL is even up.
//
//     `--record-every` spaces the second of those. A checkpoint hashes
//     every byte of RAM, so one a frame is about a megabyte of SHA-256
//     sixty times a virtual second: right for a run somebody is pointing
//     at a problem, and the reason a game-length recording at that
//     cadence is both slow to make and far too large to commit. The
//     session library picks its own and says why
//     (tests/sessions/README.md).
//
//     What a sparse cadence costs is *where* a divergence is localized,
//     never whether one is found: every key still lands on the tick it
//     was recorded at, and the run still has to reach `end`. What it must
//     not cost is the moments worth pinning, so a frame that posted a key
//     is checkpointed whatever N says, and so is the frame the run ends
//     on — that last one carries `stopped`, which a replaying host needs
//     before it can arrive at the tick at all.
//
//     Replaying adds one thing and takes one away. It adds a clamp: the
//     slice stops at the recording's next event as readily as at a frame
//     boundary, because an event the machine *consumes* has to land on
//     the exact tick it was recorded at, and a loop that ran through the
//     tick first would be checking a machine that had already gone
//     somewhere else. The exception is a checkpoint of a stopped
//     machine, which this loop has to be allowed to run *past* to
//     arrive at, because stopping happens inside a step and spends
//     neither the step nor its ticks — machine/replay.h has that story.
//
//     It takes away the keyboard: the recording's keys are the run's
//     keys, and a key struck at the window during a replay is an input
//     the recorded run never had. The window still closes.
//
//     A replay does not take its speed or its seams from the command
//     line either — the recording named them, the player applies them
//     before it checks them, and `--speed`, `--seam` and `--press` are
//     refused alongside `--replay` rather than silently agreed with.
//
//     The verdict is the run's exit code, ahead of the program's own, on
//     the same reasoning as `--verify`: a run asked to check itself
//     against a recording is answering that question and not the
//     program's. Reaching the recording's `end` is part of passing.
//
//   --wall now|none|YYYY-MM-DD[THH:MM[:SS[.CC]]]
//                    what date to tell the machine it is
//
//     The host's own clock unless you say otherwise, which is what a
//     player wants and what nothing here did until #320: the machine's
//     date is a seed plus virtual time (`machine/platform.h`), no host
//     ever seeded it, and so the game — and every stamp in the journal's
//     listing — read 1 January 1980 plus the run's own uptime.
//
//     `none` is the machine that was: unseeded, counting from the DOS
//     epoch, which is what a PC with no clock card gave you and what
//     every recording in `tests/sessions/` was made on.
//
//     A stated date is for a run that has to be **reproducible**, and
//     that is more than a hash. The seed is machine state, so seeding
//     from the clock puts the moment the run started into every
//     checkpoint — and the program *reads* the date: 400 million steps
//     of a real boot, dumped at two instants a minute apart, differ in
//     73 pixels at the same step, the same tick and the same frame count
//     (`docs/replay.md` §6 has the measurement, and the era's usual
//     reason: a generator seeded off the clock). So **two runs compared
//     with each other, by hash or by pixel — a seam on against the same
//     script with it off — have to be told the same instant**, and this
//     is how they are told. `scripts/visual-legs.py` passes `--wall
//     none` on both of its sides for that reason, and the same leg
//     without it fails on 178 pixels the seam does not own. Recorded either way, as a `wall` line at the
//     tick it was seeded at, so a recording replays as the run it was;
//     refused alongside `--replay`, which takes its date from the
//     recording like everything else.
//
//   -- ARGUMENTS     everything after `--` becomes the command tail
//
//     Passed to the loader verbatim, with the single leading space DOS's
//     own command-line parsing leaves in front of a tail. The PSP half of
//     this — what a program that parses its tail actually finds — is #89.
//
// The report itself is formatted in core, not here
// (machine/report.h), because M3's exit criterion is desktop *and* web
// and the two hosts have to print the same sentence at the same step for
// that comparison to mean anything (#84).

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/platform.h"
#include "press_spec.h"

namespace amberfolio::sdl {

constexpr unsigned default_scale = 3;

/// A seam trigger the host pulls for itself, at a frame of the loop
/// (#161) — `scripted_press`'s (press_spec.h) sibling, and the reason it
/// is a separate type rather than a key: a pull does not go through SDL
/// at all, so it needs no window and works under `--headless`, which is
/// where the scripted runs that would want one live.
struct scripted_pull {
  std::string id;
  std::uint64_t frame{};
  bool done{false};
};

/// Where the date this machine believes it is comes from (`--wall`,
/// #320).
///
/// Three answers and not two, because the third is a real one. The clock
/// inside is a **seed** — "at this tick the wall read this" — plus
/// virtual time (`machine/platform.h`), so a machine no host ever seeds
/// is not a machine with a wrong date: it is a PC with no clock card,
/// counting from 1 January 1980, which is exactly what an XT without one
/// gave you and exactly what every run of this host gave until #320.
/// `none` keeps that machine available, because it is the machine every
/// recording in `tests/sessions/` was made on.
enum class wall_source : std::uint8_t {
  /// This host asks the operating system, once, before the first
  /// instruction. The default, and the only one a player will ever want.
  host_clock,
  /// A date somebody stated. What a run that has to be *reproducible*
  /// wants: seeding from the clock puts the moment the run started into
  /// every state hash it produces, so two runs meant to be compared hash
  /// for hash have to be told the same instant.
  stated,
  /// Nobody seeds it. The machine this host has always been.
  unseeded,
};

/// One `--watch` subject: an offset in the program's data segment, and
/// how wide the value there is.
struct watch_point {
  std::uint16_t offset{};
  unsigned width{1};
};

/// Which settings the **command line** named (#382).
///
/// One bool per setting the config file can also carry, and the whole of
/// what `prefer()` needs to keep "flag > config > default" true: a
/// setting's value cannot say whether somebody chose it, because a
/// player is allowed to choose the default.
///
/// It is also what the refusals below now test. `--headless` refuses
/// `--volume` because a flag that does nothing is a mis-invocation; a
/// *config* naming a volume on a run that opens no audio device is not
/// one, and a host that refused to start over it would be refusing a
/// player their own settings file.
struct given_on_the_command_line {
  bool root{false};
  bool program{false};
  bool seams{false};
  bool journal_ocr{false};
  bool volume{false};
  bool muted{false};
  bool speed{false};
  bool scale{false};
  bool save_sidecars{false};
};

struct options {
  std::filesystem::path root;
  std::string program;
  /// `--install DIR` (#397): the DOS directory `root` appears at, which
  /// is made current before the load. Empty when not given, and then the
  /// edition row's own install directory decides, or the root.
  std::string install;
  bool headless{false};
  unsigned scale{default_scale};
  bool verify{false};
  std::vector<scripted_press> presses;
  std::vector<scripted_pull> pulls;
  std::vector<watch_point> watches;
  std::vector<std::string> seams;
  bool list_seams{false};

  /// Open with the toggle panel up (#383). The panel is the window's
  /// own -- the right mouse button opens and closes it either way -- and
  /// this is how a driven run puts it in front of a camera, the way
  /// `--keyboard` does for the on-screen keyboard.
  bool seam_panel{false};

  /// Whether `opts.seams` came out of the config file rather than off
  /// the command line (#383).
  ///
  /// It decides what a refusal costs. A `--seam` this host cannot honour
  /// is a command line to fix and stops the run; a *remembered* choice
  /// that no longer fits -- a player who turned a seam on last week and
  /// has loaded another program today -- is a row in the panel with a
  /// reason on it, and stopping the launch over it would be persistence
  /// taking the game away.
  bool seams_from_config{false};

  /// The VFS door (M5-D2, #170), against the directory this host was
  /// pointed at. Applied *after* the run, so `--vfs-list` says what is
  /// in `\\SAVE\\` once the game has saved rather than before it started.
  bool list_vfs{false};
  std::vector<std::string> vfs_gets;
  std::vector<std::string> vfs_removes;

  /// The save layer (#208): which of the files on the disk are the
  /// player's, and which of those make up a slot. The table comes off
  /// the loaded program and is printed with the edition line; the files
  /// it matches are printed after the run, for the same reason
  /// `--vfs-list` is.
  bool save_layer{false};

  /// Which on-screen keyboard layout the run opens with, or empty for one
  /// that opens with none (#377). The names are
  /// `machine::screen_keyboard`'s own — `prompt`, `name`, `full` — and
  /// the window's middle mouse button steps it on through the layouts and
  /// off again either way.
  std::string keyboard;

  /// Documents the player presents (M5-D3, #171), as paths on this
  /// machine's own filesystem — not on the emulated one. A code wheel
  /// lives wherever a person keeps their PDFs, which is very often not
  /// inside the game directory.
  std::vector<std::string> documents;

  /// The code wheel's challenge, already answered (M6-C1a, #291). The
  /// seam watches for a person answering it and remembers it for the
  /// rest of the run; this is how a run *states* that condition without
  /// a person at the keyboard, and it writes nothing down.
  bool code_wheel_answered{false};

  /// Where the copies that have answered are remembered (M6-C1b, #292),
  /// or empty for this platform's per-user data directory; and the
  /// player asking to be asked again.
  std::string code_wheel_store;
  bool forget_code_wheel{false};

  /// The journal's ingestion (M5-E3, #174). `journal` is the document to
  /// read the entries out of; `journal_store` is where the text goes, or
  /// empty for this platform's default; `journal_ocr` is the engine, or
  /// `none`; `journal_probe` adds the synthetic edition and its fixture
  /// engine for a check that has no real document to use.
  ///
  /// `journal_ocr` empty means **nobody said**, and this host goes and
  /// looks (`ocr_discovery.h`, #382): beside the binary, then the path,
  /// then a report of what it looked for. A build that carries its own
  /// engine reads the same emptiness as "use the one you carry", because
  /// a player who typed nothing did not ask for a program
  /// (`tesseract_linked_ocr.h`). Before #382 the default was the bare
  /// word `tesseract` and the shell resolved it, which worked for a
  /// player whose engine was on the path and told everybody else nothing.
  std::string journal;
  std::string journal_store;
  std::string journal_ocr;
  bool journal_probe{false};
  /// The debug cheat that puts everything the store holds onto the
  /// journal's log (#301). A flag rather than a seam, on purpose: the
  /// usage block above says why.
  bool cite_all_journal{false};
  machine::speed_preset speed{machine::default_speed};

  /// Where the speaker's level starts, and whether it starts latched to
  /// silence (#148). Two things and not one, for the reason every mixer
  /// ever built has them as two: mute is a latch that can be lifted, and
  /// lifting it should give back the level that was there rather than
  /// some level the player has to find again. The gain the audio thread
  /// applies is `muted ? 0 : volume`.
  ///
  /// One is "what the machine made" and this host does not go above it;
  /// `audio_gain.h` says why.
  float volume{1.0F};
  bool muted{false};

  /// How many seconds of virtual time to run per second of wall time.
  /// Zero means "do not pace at all" — `--fast max`, and what
  /// `--headless` does regardless.
  double fast{1.0};

  /// Zero means "no budget" for both. Zero is not a budget anyone can
  /// want — a run of no steps observes nothing — so it is free to be the
  /// sentinel, and a caller does not have to say `--steps 0` to mean
  /// "unlimited".
  std::uint64_t step_budget{0};
  machine::ticks tick_budget{0};

  /// `--dump-every`: write a still every this many frames, on top of
  /// the one `--dump` writes at the end. Zero means "only the last
  /// one", which is what `--dump` alone has always meant.
  std::uint64_t dump_every{0};

  /// Where `--record` writes the recording, and where `--replay` reads
  /// one. Empty when the option was not given, and never both at once:
  /// a run is either the one being recorded or the one being checked
  /// against a recording, and one that tried to be both would be
  /// recording its own checks.
  std::string record_path;
  std::string replay_path;

  /// `--rehash FILE`, with `--replay` (rehash.h, #404).
  std::string rehash_path;

  /// `--record-every`: take a checkpoint every this many frames rather
  /// than every one. One — a checkpoint a frame — is what `--record`
  /// alone has always done and is right for a run a person is pointing
  /// at a problem; the committed session library uses a sparser one
  /// (tests/sessions/README.md). A checkpoint hashes every byte of RAM,
  /// so the cadence is most of what a recording *costs* to make as well
  /// as most of what it costs to store, and a game session at one a
  /// frame is neither affordable to record nor small enough to commit.
  ///
  /// Sparse never means "and not the interesting moments": a frame in
  /// which a key was posted is checkpointed whatever the cadence says,
  /// and so is the frame the run ends on.
  std::uint64_t record_every{1};

  /// Where `--dump` writes. Empty when it was not asked for; the two
  /// files are this plus `.ppm` and `.wav`.
  std::string dump_prefix;

  bool trace{false};

  /// `--save-sidecars`: keep what this playthrough has accumulated
  /// beside its saves (M5-E2c #173, #351). Off by default — see the usage
  /// block at the top of this file for why writing into a player's game
  /// directory is asked for rather than assumed.
  bool save_sidecars{false};

  /// `--wall`: where the date this machine is told it is comes from
  /// (#320). The host's own clock unless somebody says otherwise, which
  /// is the answer a player wants and the one nobody was giving.
  wall_source wall{wall_source::host_clock};

  /// The instant `--wall YYYY-MM-DD[THH:MM[:SS[.CC]]]` named, already
  /// checked against `wall_clock`'s own rules at parse time. Meaningless
  /// unless `wall` is `stated`.
  machine::wall_time wall_stated{};

  /// Everything after `--`, joined with single spaces and with the one
  /// leading space DOS leaves in front of a command tail. Empty when
  /// there was no `--`, which is a program invoked with no arguments
  /// rather than one invoked with an empty argument.
  std::string command_tail;

  /// The config file (#382). `config_path` is where it is, or empty for
  /// this platform's per-user data directory; `no_config` ignores
  /// whatever is there; `remember` writes this run's settings back;
  /// `forget_config` empties it.
  ///
  /// None of these can themselves be remembered, which is the point of
  /// them: a flag that turned the config off would be useless if the
  /// config could turn it back on.
  std::string config_path;
  bool no_config{false};
  bool remember{false};
  bool forget_config{false};

  /// Which of the settings above the command line named, for the
  /// precedence rule (`desktop_config.h`).
  given_on_the_command_line given;

  /// A launch with nothing to run and nowhere to run it from: a first
  /// run, which is not an error (#382). `valid` is false because there is
  /// no run to have, and this says why — `main` prints what it needs and
  /// where to point it, and exits successfully.
  bool first_run{false};

  bool valid{false};
};

/// A speed preset in words, for the line a non-default run prints.
[[nodiscard]] const char* speed_name(machine::speed_preset preset) noexcept;

/// A speed preset as the word `--speed` takes, which is not the same
/// thing as the sentence above: that one is for a person reading a log,
/// this one goes into a config file and comes back out of it (#382).
[[nodiscard]] const char* speed_word(machine::speed_preset preset) noexcept;

/// And back again. False for a word that is not one of the four, which
/// is what `--speed` refuses on and what a config file's reading refuses
/// on — one table, so the two cannot drift apart.
[[nodiscard]] bool speed_named(std::string_view word,
                               machine::speed_preset& out) noexcept;

/// A non-negative integer argument, or false for anything that is not
/// one. `strtoull` rather than `atoi` for the reason `--scale` already
/// gives: it can tell "0" from "not a number", and here the difference
/// decides whether a budget exists at all.
[[nodiscard]] bool parse_count(const char* text, std::uint64_t& out);

/// `ID@FRAME`, into a scripted pull (#161). The same shape as
/// `parse_press` (press_spec.h), and split on the last `@` for the same
/// reason — a seam id is kebab-case and cannot contain one, but making
/// that a fact this parser depends on would be free only until it was
/// not.
[[nodiscard]] bool parse_pull(std::string_view spec, scripted_pull& out);

/// `OFF[:WIDTH]`, hexadecimal offset, into a watch point. False on
/// anything that is not that.
///
/// Hexadecimal without a `0x`, because every offset a watch is pointed at
/// comes off the same fact table the seams are written from
/// (machine/seam.h) and those are written in hex. A width is 1 or 2 —
/// a byte or a word — and nothing else, because there is no third thing
/// a data offset in a 16-bit program means.
[[nodiscard]] bool parse_watch(std::string_view spec, watch_point& out);

/// The command line, parsed (`argc`/`argv` as `main` was given them).
///
/// `valid` is false when the line was refused, and a sentence on stderr has
/// said why. Nothing is read from the config file here: that is
/// `settle_config()`'s (launch_config.h), which runs on what this returned.
[[nodiscard]] options parse(int argc, char** argv);

}  // namespace amberfolio::sdl
