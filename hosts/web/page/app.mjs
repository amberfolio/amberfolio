// SPDX-License-Identifier: AGPL-3.0-only
//
// Browser-only glue for the dev page: canvas presentation, keyboard
// input, the AudioWorklet wiring, the console <pre> sink, and — since
// M3-F2 (#84) — the player's own directory.
//
// Deliberately separate from host.mjs, which has to stay DOM-free so
// tests/smoke.mjs can import it under node (host.mjs's own top comment).
// The directory picker is separate again, in picker.mjs, for the same
// reason one layer down: reading a `File` is browser work and putting the
// bytes in the machine is not.
//
// This is still the bare dev page PLAN.md §7 asks for, not the M6
// reference shell: no onboarding, no persistence, no touch, no styling
// worth the name. What #84 added is one affordance — getting a directory
// into the machine — because M3's exit criterion is "verified locally on
// desktop **and** web" and there was previously no way to put a player's
// files in front of the browser at all.
//
//
// Two things it can run
// ---------------------
//
// The embedded demo program (hosts/web/src/demo_program.cpp), which is
// what M2-H2 built this page to prove, and a program out of a directory
// the player chose. One run per page load either way: this page has no
// business tearing a machine down and standing another one up, and the
// ABI has one machine per module anyway (abi.h).
//
//
// Why everything waits for a gesture
// -----------------------------------
//
// Browsers refuse to start an AudioContext without a user gesture, and a
// page that ran the machine before the button existed to explain why
// nothing is audible yet would be confusing about the one thing this page
// exists to demonstrate. Choosing a directory is a gesture too, which is
// why the module is instantiated there: by the time Boot is pressed, the
// files are already in the machine and the audio context may start.

import {
  loadAmberfolio,
  Machine,
  loadDemoProgram,
  scancodeFor,
  decodeConsoleBytes,
  SPEED_PRESETS,
  AF_OK,
  AF_INVALID,
  AF_NO_ROOM,
  describeSkip,
  AF_SEAM_ON,
  AF_SEAM_UNAVAILABLE,
  formatSeamFired,
  AF_RUN_END_STOPPED,
  AF_RUN_END_STEP_BUDGET,
  AF_RUN_END_HOST_QUIT,
} from './host.mjs';
import { wireDirectoryPicker } from './picker.mjs';

const CANVAS_ID = 'screen';
const START_BUTTON_ID = 'start';
const BOOT_BUTTON_ID = 'boot';
const STATUS_ID = 'status';
const CONSOLE_ID = 'console';
const DIRECTORY_INPUT_ID = 'directory';
const DROP_ZONE_ID = 'drop';
const PROGRAM_SELECT_ID = 'program';
const TAIL_INPUT_ID = 'tail';
const STEPS_INPUT_ID = 'steps';
const TRACE_CHECKBOX_ID = 'trace';
const EDITION_ID = 'edition';
const SEAMS_ID = 'seams';
const SPEED_SELECT_ID = 'speed';
const VOLUME_INPUT_ID = 'volume';
const MUTE_CHECKBOX_ID = 'mute';
const HEALTH_ID = 'health';

/// A frame's worth of audio, in samples, at this rate — matched to the
/// video frame rate so one rAF callback pulls roughly one frame of both
/// (the run loop below advances virtual time by one frame per callback
/// too; see its own comment).
const AUDIO_SAMPLE_RATE = 44100;

/// Wires the page up. Called once, from index.html's own inline module
/// script.
export function runDevPage() {
  const el = (id) => document.getElementById(id);

  const canvas = el(CANVAS_ID);
  const startButton = el(START_BUTTON_ID);
  const bootButton = el(BOOT_BUTTON_ID);
  const statusEl = el(STATUS_ID);
  const consoleEl = el(CONSOLE_ID);
  const programSelect = el(PROGRAM_SELECT_ID);
  const speedSelect = el(SPEED_SELECT_ID);
  const volumeInput = el(VOLUME_INPUT_ID);
  const muteCheckbox = el(MUTE_CHECKBOX_ID);
  const healthEl = el(HEALTH_ID);

  const setStatus = (text) => {
    if (statusEl) statusEl.textContent = text;
  };

  const appendConsole = (text) => {
    if (consoleEl) {
      consoleEl.textContent += text;
      consoleEl.scrollTop = consoleEl.scrollHeight;
    }
    if (text.length > 0) console.log('[amberfolio]', text);
  };

  // The one machine, made on whichever gesture comes first. `Machine`
  // throws if a second one is asked for (abi.h: one machine per module),
  // so this is also what keeps the two entry points from colliding.
  let loaded = null;
  let machine = null;
  let started = false;

  const ensureMachine = async () => {
    if (machine) return machine;
    setStatus('loading the wasm module...');
    loaded = await loadAmberfolio({ print: appendConsole, printErr: appendConsole });
    for (const line of loaded.output) appendConsole(`${line}\n`);

    machine = new Machine(loaded.module);
    const attached = machine.attachReferenceDevices();
    if (attached !== AF_OK) {
      throw new Error(`af_machine_attach_reference_devices() answered ${attached}`);
    }
    // The RESET line, once the devices are on the bus — the same thing
    // the SDL host's own wiring does before it loads anything
    // (hosts/sdl/src/main.cpp's `wired_machine`).
    //
    // It is not decoration. `reset()` blanks the frame and republishes
    // it, which advances the generation counter, so a machine that was
    // reset and one that was not are one frame apart forever after. That
    // difference shows up in the `frames=` field of the stop report, and
    // M3's exit criterion is that this host and the desktop one print the
    // same line at the same step (#84) — so the two have to power on the
    // same way, not merely run the same way.
    machine.reset();
    const { major, minor, patch } = loaded.version;
    appendConsole(`[host] amberfolio ${major}.${minor}.${patch}\n`);
    if (speedSelect && speedSelect.value !== 'xt') applySpeed();
    return machine;
  };

  // --- Speed: #107's presets, this page's half (#108) --------------------
  //
  // The same four names the SDL host's `--speed` takes and the web
  // driver's `--speed` takes; host.mjs owns the mapping onto abi.h's
  // numbers so that no host spells a preset its own way.
  //
  // A governor and not a fast-forward: virtual time still decides every
  // deadline, tone and tick, so a run at `at` is exactly as deterministic
  // as one at `xt` — what changes is how much of it fits into a second of
  // yours, which on this host is the question #108 exists to ask. It is
  // applied whenever the control changes and not only at boot, because
  // the useful thing to do with it here is to turn it up while watching
  // the readout below and find where the browser stops keeping up.
  function applySpeed() {
    if (!machine || !speedSelect) return;
    const preset = SPEED_PRESETS[speedSelect.value];
    if (preset === undefined) return;
    machine.setSpeed(preset);
    appendConsole(`[host] speed ${speedSelect.value}\n`);
  }
  if (speedSelect) speedSelect.addEventListener('change', applySpeed);

  const claimTheRun = () => {
    if (started) return false;
    started = true;
    startButton.disabled = true;
    bootButton.disabled = true;
    if (programSelect) programSelect.disabled = true;
    return true;
  };

  const fail = (error) => {
    setStatus(`failed: ${error}`);
    appendConsole(`[host] ${error}\n`);
    console.error(error);
  };

  // --- The embedded demo -------------------------------------------------

  startButton.addEventListener('click', () => {
    if (!claimTheRun()) return;
    startButton.textContent = 'running...';
    (async () => {
      const box = await ensureMachine();
      const { writeStatus, entryStatus, size } = loadDemoProgram(box);
      if (writeStatus !== AF_OK || entryStatus !== AF_OK) {
        throw new Error(
          `loading the demo program failed (write=${writeStatus}, entry=${entryStatus})`,
        );
      }
      appendConsole(`[host] embedded demo program loaded: ${size} bytes\n`);
      await run(box, {
        canvas,
        setStatus,
        appendConsole,
        healthEl,
        volumeInput,
        muteCheckbox,
        stepBudget: 0,
        message:
          'running - the machine draws a pattern, plays a tone, and echoes ' +
          'whatever you type into the console below.',
      });
    })().catch(fail);
  });

  // --- The player's own directory (#84) ----------------------------------

  wireDirectoryPicker({
    input: el(DIRECTORY_INPUT_ID),
    dropZone: el(DROP_ZONE_ID),
    onError: fail,
    onFiles: async (files) => {
      if (started) return;
      const box = await ensureMachine();
      setStatus(`reading ${files.length} files...`);

      // A second directory replaces the first rather than merging with
      // it: two installations' files in one filesystem is not a state
      // any real machine has, and this page keeps nothing between
      // reloads anyway.
      box.vfsClear();

      // Two skipped lists, not one (#158). A path DOS could never have
      // named is the machine working and the player needs no more than
      // the name; a file the filesystem had no room for is a *hole in
      // the disk they are about to boot*, and saying those two in one
      // sentence is how a browser came to run an installation with seven
      // data files missing from it.
      const unnameable = [];
      const noRoom = [];
      const otherwise = [];
      const taken = [];
      for (const file of files) {
        // The path goes across as the player's own text and core decides
        // what it means, separators included (abi.h, #146).
        const status = box.vfsPut(file.path, file.bytes);
        if (status === AF_OK) taken.push(file);
        else if (status === AF_NO_ROOM) noRoom.push(file.path);
        else if (status === AF_INVALID) unnameable.push(file.path);
        else otherwise.push(`${file.path} (${describeSkip(status)})`);
      }

      appendConsole(
        `[host] filesystem: ${taken.length} files, ` +
          `${Math.round(box.vfsBytesUsed() / 1024)} KiB` +
          (unnameable.length > 0
            ? `, ${unnameable.length} ignored (not DOS-nameable, which is ` +
              `expected): ${unnameable.join(', ')}`
            : '') +
          (otherwise.length > 0
            ? `, ${otherwise.length} refused: ${otherwise.join(', ')}`
            : '') +
          '\n',
      );

      if (noRoom.length > 0) {
        // Its own line and its own sentence, because everything below
        // this point — the boot, the frames, a save — is happening on a
        // disk that is not the one the player chose.
        appendConsole(
          `[host] OUT OF ROOM: ${noRoom.length} file(s) did not fit in the ` +
            "machine's filesystem and are missing from the disk this page " +
            'will boot. The game will not find them, and a save may have ' +
            'nowhere to go. Choose a smaller directory.\n' +
            `[host]   ${noRoom.join(', ')}\n`,
        );
      }

      // What to offer as bootable is what went in, not `vfsList()`: the
      // root listing is the root's, and since #146 an entry in it may be
      // a directory `vfsPut` made on the way to a file below (abi.h).
      // This page knows which of the things it handed over were files,
      // because it handed them over.
      //
      // Programs first, everything else after: a player wants the .EXE
      // and should not have to hunt for it, and the rest is still offered
      // because nothing here should be deciding what is and is not
      // bootable. Ordering is the whole of what this looks at — the name
      // itself goes to `loadFromVfs` as the player spelled it, for core
      // to canonicalize.
      const isProgram = (path) => /\.(exe|com)$/i.test(path);
      const ordered = [
        ...taken.filter((file) => isProgram(file.path)),
        ...taken.filter((file) => !isProgram(file.path)),
      ];
      programSelect.replaceChildren(
        ...ordered.map((file) => {
          const option = document.createElement('option');
          option.value = file.path;
          option.textContent = `${file.path} (${file.bytes.length} bytes)`;
          return option;
        }),
      );
      programSelect.disabled = ordered.length === 0;
      bootButton.disabled = ordered.length === 0;
      // The one line a player who did not open the console will read, so
      // "the disk is incomplete" has to survive into it (#158). A boot
      // is still offered — it is their disk and their decision — but not
      // silently.
      setStatus(
        ordered.length === 0
          ? 'nothing in that directory has a DOS-legal name.'
          : noRoom.length > 0
            ? `${taken.length} files loaded, but ${noRoom.length} did not ` +
              'fit - this disk is incomplete; see the console.'
            : `${taken.length} files loaded - choose a program and press boot.`,
      );
    },
  });

  bootButton.addEventListener('click', () => {
    const program = programSelect.value;
    if (!program || !claimTheRun()) return;
    bootButton.textContent = 'running...';

    (async () => {
      const box = await ensureMachine();

      // The identity of the player's file, before anything runs — a fact
      // about it, never anything out of it (PLAN.md §2, CONTRIBUTING.md),
      // and the same digest the desktop host prints at load.
      const digest = box.vfsFingerprint(program);
      appendConsole(`[host] load ${program} sha256=${digest ?? 'unreadable'}\n`);

      box.setTrace(el(TRACE_CHECKBOX_ID)?.checked === true);

      const tail = el(TAIL_INPUT_ID)?.value ?? '';
      const status = box.loadFromVfs(program, tail === '' ? '' : ` ${tail}`);
      if (status !== AF_OK) {
        throw new Error(
          `${program} did not load (status ${status}, loader error ${box.loadError()})`,
        );
      }

      // The identity the load established, and the seams it makes
      // available (M4-F1 #95, M4-F4 #98). An unrecognized edition is an
      // answer, not a fault: the game runs as a plain machine and every
      // seam is listed as unavailable with the reason.
      const edition = box.edition();
      appendConsole(
        `[host] edition ${edition ?? 'unrecognized - no seams are available for this program'}\n`,
      );
      const editionEl = el(EDITION_ID);
      if (editionEl) editionEl.textContent = `edition: ${edition ?? 'unrecognized'}`;
      const refreshSeams = renderSeams(box, el(SEAMS_ID), appendConsole);

      const budget = Number.parseInt(el(STEPS_INPUT_ID)?.value ?? '', 10);
      await run(box, {
        canvas,
        setStatus,
        appendConsole,
        healthEl,
        refreshSeams,
        volumeInput,
        muteCheckbox,
        stepBudget: Number.isFinite(budget) && budget > 0 ? budget : 0,
        message: `running ${program} - the console below is what it says and ` +
          'what the machine refuses.',
      });
    })().catch(fail);
  });
}

/// What an enabled seam's row says beside its name: `armed fired=N`, in
/// the same words the desktop host ends a run with (hosts/sdl/src/main.cpp).
///
/// The two halves are different claims and that is the whole point
/// (#131): `armed` says an address was computed out of the seam's fact
/// table, `fired` says a handler ran there. `armed fired=0` after a run
/// that should have fired is the failure that reads exactly like
/// success, so it is called out in words rather than left to a reader to
/// notice a zero.
function seamRowText(seam) {
  if (seam.state === AF_SEAM_UNAVAILABLE) return `unavailable: ${seam.reason}`;
  if (seam.state !== AF_SEAM_ON) return 'off';
  // The reason is worth an extra word in a row a person is looking at,
  // and only there: an inert seam is waiting for its module and the row
  // is where somebody would ask what for. The console line below is the
  // desktop host's, unembellished.
  const why = !seam.armed && seam.reason !== 'none' ? ` (${seam.reason})` : '';
  return formatSeamFired(seam) + why;
}

/// One checkbox per seam, off by default, disabled with its reason when
/// the seam is not available for the loaded program. Toggling is a
/// configuration call between frames (host.mjs) — the page does it in
/// the change handler, which runs between two rAF callbacks and so never
/// from inside `runUntil()`. The listing is re-read after every toggle so
/// an on-but-inert seam (its module is not resident yet) shows as such.
///
/// Answers a `refresh()` the run loop calls on its own readout cadence,
/// which is how `fired=` becomes a live number instead of one taken once
/// before the machine had run a step. It rewrites the status text only —
/// rebuilding the checkboxes under a person's pointer while they were
/// aiming at one would be worse than the number being half a second old.
function renderSeams(machine, container, appendConsole) {
  if (!container) return () => {};
  const seams = machine.seamList();
  const rows = new Map();
  container.replaceChildren(
    ...seams.map((seam) => {
      const label = document.createElement('label');
      const box = document.createElement('input');
      box.type = 'checkbox';
      box.checked = seam.state === AF_SEAM_ON;
      box.disabled = seam.state === AF_SEAM_UNAVAILABLE;
      const status = document.createElement('span');
      status.textContent = ` [${seamRowText(seam)}]`;
      rows.set(seam.id, status);
      box.addEventListener('change', () => {
        const answer = box.checked ? machine.seamEnable(seam.id) : machine.seamDisable(seam.id);
        const after = machine.seamList().find((s) => s.id === seam.id);
        appendConsole(
          `[host] seam ${seam.id} ${box.checked ? 'on' : 'off'}` +
            (answer === AF_OK ? '' : ` refused (${after?.reason ?? '?'})`) +
            (after && after.state === AF_SEAM_ON && !after.armed ? ` (inert: ${after.reason})` : '') +
            '\n',
        );
        if (answer !== AF_OK) box.checked = !box.checked;
        if (after) status.textContent = ` [${seamRowText(after)}]`;
      });
      label.append(box, ` ${seam.id} - ${seam.about}`, status);
      label.title =
        seam.state === AF_SEAM_UNAVAILABLE ? `unavailable: ${seam.reason}` : seam.about;
      return label;
    }),
  );
  if (seams.length === 0) container.textContent = 'this build carries no seams';

  return () => {
    for (const seam of machine.seamList()) {
      const status = rows.get(seam.id);
      if (status) status.textContent = ` [${seamRowText(seam)}]`;
    }
  };
}

/// Present, run, and report — everything both entry points share.
async function run(
  machine,
  {
    canvas,
    setStatus,
    appendConsole,
    healthEl,
    refreshSeams,
    volumeInput,
    muteCheckbox,
    stepBudget,
    message,
  },
) {
  const ctx = canvas.getContext('2d');
  const width = machine.frameWidth();
  const height = machine.frameHeight();
  canvas.width = width;
  canvas.height = height;
  const imageData = ctx.createImageData(width, height);
  // Every pixel is opaque; set once rather than every frame.
  const alpha = imageData.data;
  for (let i = 3; i < alpha.length; i += 4) alpha[i] = 255;

  const present = () => {
    const pixels = machine.framebufferView();
    const palette = machine.paletteView();
    const data = imageData.data;
    for (let i = 0; i < pixels.length; ++i) {
      const entry = pixels[i] * 3;
      const out = i * 4;
      data[out] = palette[entry];
      data[out + 1] = palette[entry + 1];
      data[out + 2] = palette[entry + 2];
    }
    ctx.putImageData(imageData, 0, 0);
  };

  // --- Keyboard: keydown/keyup -> ABI key events -------------------------
  //
  // `scancodeFor()` (host.mjs) is the whole of the translation; anything
  // it does not recognise is left alone (no preventDefault, no post) so
  // the browser's own shortcuts still work for keys this 83-key keyboard
  // never had. Recognised keys are prevented from their usual browser
  // effect — Space scrolling the page, Backspace navigating back, Tab
  // moving focus, and since #84 the arrows and Page keys scrolling —
  // because the dev page's whole surface is the machine while it runs.
  const onKey = (down) => (event) => {
    const scancode = scancodeFor(event.code);
    if (scancode === undefined) return;
    event.preventDefault();
    machine.postKey(scancode, down);
  };
  const onKeyDown = onKey(true);
  const onKeyUp = onKey(false);
  window.addEventListener('keydown', onKeyDown);
  window.addEventListener('keyup', onKeyUp);

  // --- Audio: an AudioWorklet fed from the main thread --------------------
  //
  // See audio-worklet.mjs's own top comment for the threading contract
  // this implements: `machine.renderAudio()` runs here, on the main
  // thread (the machine thread, since this build has no wasm pthreads),
  // and every pulled chunk crosses to the worklet by `postMessage()`
  // with its buffer transferred, never shared.
  const audioContext = new AudioContext({ sampleRate: AUDIO_SAMPLE_RATE });
  await audioContext.audioWorklet.addModule('./audio-worklet.mjs');
  const speakerNode = new AudioWorkletNode(audioContext, 'amberfolio-speaker', {
    numberOfInputs: 0,
    numberOfOutputs: 1,
    outputChannelCount: [1],
  });
  speakerNode.connect(audioContext.destination);
  // AudioContext is created suspended by some browsers even inside a
  // user-gesture handler; resume() is a no-op if it is already running.
  await audioContext.resume();

  // The worklet's own starvation count, which is a different number from
  // core's: `machine.audioUnderruns()` counts pulls that ran past settled
  // virtual time, and this counts quanta the audio thread had no chunk
  // for. Both are host-pacing symptoms and neither is machine state
  // (platform.h) — and until #108 the page showed neither, so "why does
  // it sound wrong" had no number attached on either side of the
  // boundary. The worklet posts this only when it changes, so the message
  // rate is bounded by the thing being counted.
  let workletUnderruns = 0;
  speakerNode.port.onmessage = (event) => {
    const count = event.data?.underruns;
    if (typeof count === 'number') workletUnderruns = count;
  };

  // --- Volume and mute: #148's half of #106's scope ----------------------
  //
  // Two controls and not one, for the reason every mixer ever built has
  // two: mute is a latch that can be lifted, and lifting it should give
  // back the level that was there rather than one the player has to find
  // again. The gain that crosses to the worklet is `muted ? 0 : volume`.
  //
  // It crosses to the *worklet* and is not applied to the chunks here,
  // which audio-worklet.mjs's own comment argues at length: the held
  // level and the fade an underrun makes are on that side of the
  // boundary, so a mute applied on this one would not silence a stalled
  // tab. And it is not in core at all — a gain in
  // `audio_timeline::render()` would stop a sample being the exact
  // integral of the edge list, which is the property every number in
  // docs/hosts.md §4 is a measurement of.
  //
  // Nothing here is machine state: a run at 25% is the same run as one
  // at 100%, down to the last edge in the list and the last bit of the
  // frame.
  const sendGain = () => {
    const muted = muteCheckbox ? muteCheckbox.checked : false;
    const volume = volumeInput ? Number(volumeInput.value) / 100 : 1;
    speakerNode.port.postMessage({ gain: muted ? 0 : volume });
  };
  if (volumeInput) volumeInput.addEventListener('input', sendGain);
  if (muteCheckbox) muteCheckbox.addEventListener('change', sendGain);
  // Whatever the controls say now, which is not necessarily what they
  // said when the page loaded: a browser restores a form control's value
  // across a reload, and a page that started at full volume because it
  // never asked would be a control that lies.
  sendGain();

  setStatus(message);

  // --- The run loop: requestAnimationFrame, per abi.h's own snippet ------
  //
  // "next += af_ticks_per_second() / 60; af_machine_run_until(box, next)"
  // is abi.h's documented run loop, verbatim — a fixed virtual-time
  // increment per callback, not a duration measured from wall time. rAF
  // is what "wall time throttles presentation" means here: the browser
  // paces how often this callback runs, and each run advances virtual
  // time by exactly one 60 Hz frame, never more and never less. A frame
  // is presented only when `frameGeneration()` says a new one exists
  // (platform.h's pull contract) — most callbacks will find exactly one
  // new frame, since the renderer's own deadline is also 60 Hz.
  let next = 0;
  let frames = 0;
  let lastGeneration = -1;

  /// The run is over: say why, in the one fixed format the desktop host
  /// prints (machine/report.h). Formatted in core precisely so that "the
  /// same stop line at the same step" (#84) is a comparison anybody can
  /// make by eye.
  const finish = (how) => {
    showHealth();
    setStatus(
      how === AF_RUN_END_STOPPED
        ? `the machine stopped - see the report below.`
        : `the run was cut short at ${machine.steps()} steps - see the report below.`,
    );
    drainLog();
    if (partialLine.length > 0) {
      appendConsole(`${partialLine}\n`);
      partialLine = '';
    }
    const lost = machine.logDropped();
    if (lost > 0) {
      appendConsole(`[host] ${lost} diagnostic line(s) dropped - the log ring overflowed\n`);
    }
    appendConsole(machine.stopReport(how));
    const trace = machine.traceReport();
    if (!trace.startsWith('amberfolio: stop trace=off')) appendConsole(trace);

    // What each enabled seam actually did, in the desktop host's own
    // words (#131, #147). `armed` says an address was computed; `fired`
    // says a handler ran there, and a seam that is on and armed and
    // fired nothing is the failure that reads exactly like success. The
    // end of the run it belongs to is the one place a reader gets told
    // for free, and until now a browser run could not say it at all.
    for (const seam of machine.seamList()) {
      if (seam.state !== AF_SEAM_ON) continue;
      appendConsole(`amberfolio: seam ${seam.id} ${formatSeamFired(seam)}\n`);
    }
    window.removeEventListener('keydown', onKeyDown);
    window.removeEventListener('keyup', onKeyUp);
  };

  // The diagnostics stream, as the lines core renders from it
  // (machine/log.h, #108). Before this the page said nothing about what a
  // program did beyond the number it stopped with, while the same run on
  // the desktop host printed its notices, its file activity and every
  // seam transition - which made driving docs/playable.md's legs here
  // strictly worse than driving them there, for no reason but a missing
  // door.
  //
  // A drain can end mid-line (abi.h), so the tail of one is held back and
  // becomes the head of the next. Nothing is lost: the remainder is still
  // the ring's, not this page's.
  let partialLine = '';
  const drainLog = () => {
    const text = partialLine + machine.readLog();
    const lastBreak = text.lastIndexOf('\n');
    if (lastBreak < 0) {
      partialLine = text;
      return;
    }
    partialLine = text.slice(lastBreak + 1);
    appendConsole(text.slice(0, lastBreak + 1));
  };

  /// The health readout: how far the run has got, what the audio path
  /// had to paper over to keep up, and — since #147 — what each enabled
  /// seam has actually done. #106's policies have a visible half and
  /// this is it.
  ///
  /// Every half virtual second rather than every frame. It is a readout
  /// and not an instrument — the driver (`tools/drive.mjs`) is where a
  /// number gets measured — and writing the same three figures into the
  /// DOM sixty times a second is itself the sort of thing that causes
  /// the underruns it would be reporting. The seam rows ride the same
  /// cadence for the same reason.
  const showHealth = () => {
    if (refreshSeams) refreshSeams();
    if (!healthEl) return;
    // The listening level joins the line only when it is not unity, the
    // same rule and for the same reason as the SDL host's report line
    // (#148): a default run reads as it always has, and a run somebody
    // turned down says so where they will look when they wonder why
    // they heard nothing.
    const muted = muteCheckbox ? muteCheckbox.checked : false;
    const volume = volumeInput ? Number(volumeInput.value) : 100;
    const level = muted
      ? ' volume=muted'
      : volume === 100
        ? ''
        : ` volume=${volume}%`;
    healthEl.textContent =
      `frames=${machine.frameGeneration()} steps=${Math.round(machine.steps())} ` +
      `| audio underruns=${Math.round(machine.audioUnderruns())} ` +
      `resyncs=${Math.round(machine.audioResyncs())} ` +
      `starved=${workletUnderruns}${level}`;
  };
  showHealth();

  const frame = () => {
    if (stepBudget !== 0 && machine.steps() >= stepBudget) {
      finish(AF_RUN_END_STEP_BUDGET);
      return;
    }

    next += machine.ticksPerSecond() / 60;
    const status = machine.runUntil(next);

    const generation = machine.frameGeneration();
    if (generation !== lastGeneration) {
      present();
      lastGeneration = generation;
    }

    const consoleBytes = machine.readConsole();
    if (consoleBytes.length > 0) {
      appendConsole(decodeConsoleBytes(consoleBytes));
    }
    drainLog();

    // One frame's worth of audio, pulled and handed to the worklet
    // whether or not it is still connected — a detached port simply
    // drops the message, which is no worse than the underrun the
    // worklet already plays as silence.
    const audioFrames = Math.round(AUDIO_SAMPLE_RATE / 60);
    const { samples } = machine.renderAudio(audioFrames, AUDIO_SAMPLE_RATE);
    speakerNode.port.postMessage(samples, [samples.buffer]);

    if (frames % 30 === 0) showHealth();
    frames += 1;

    if (status !== AF_OK) {
      // Keep presenting and draining the console after a stop
      // (platform.h: "the pulls keep working... it is the console and
      // the diagnostics that say why it stopped"), but there is nothing
      // left to run.
      finish(AF_RUN_END_STOPPED);
      return;
    }

    window.requestAnimationFrame(frame);
  };

  // A page being closed or navigated away from is a host quit, and the
  // report says so rather than nothing — the same distinction the desktop
  // host draws between a machine that refused something and a person who
  // walked away.
  window.addEventListener('pagehide', () => {
    if (!machine.stopped()) console.log(machine.stopReport(AF_RUN_END_HOST_QUIT));
  });

  window.requestAnimationFrame(frame);
}
