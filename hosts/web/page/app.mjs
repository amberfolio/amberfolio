// SPDX-License-Identifier: AGPL-3.0-only
//
// Browser-only glue for the M2-H2 (#55) dev page: canvas presentation,
// keyboard input, the AudioWorklet wiring, and the console <pre> sink.
// Deliberately separate from host.mjs, which has to stay DOM-free so
// tests/smoke.mjs can import it under node (host.mjs's own top comment).
//
// This is the bare dev page PLAN.md §7 asks for, not the M6 reference
// shell: one canvas, one button, no onboarding, no persistence, no
// touch. Its only job is to prove frame, audio and input all cross the
// wasm boundary in a real browser — everything here is in service of
// that and nothing more.
//
//
// Why everything waits for one click
// -----------------------------------
//
// Browsers refuse to start an AudioContext without a user gesture, and a
// page that ran the machine (and so started the demo program's tone)
// before the button existed to explain why nothing is audible yet would
// be confusing about the one thing this page exists to demonstrate. So
// the whole run — module load, machine creation, the run loop, the
// AudioWorklet — waits for the Start button rather than only the audio
// half of it: one state machine (idle -> running) instead of two
// ("the picture is up but the sound needs another click"), which is the
// simpler thing to get right on scaffolding.

import {
  loadAmberfolio,
  Machine,
  loadDemoProgram,
  scancodeFor,
  decodeConsoleBytes,
  AF_OK,
} from './host.mjs';

const CANVAS_ID = 'screen';
const START_BUTTON_ID = 'start';
const STATUS_ID = 'status';
const CONSOLE_ID = 'console';

/// A frame's worth of audio, in samples, at this rate — matched to the
/// video frame rate so one rAF callback pulls roughly one frame of both
/// (the run loop below advances virtual time by one frame per callback
/// too; see its own comment).
const AUDIO_SAMPLE_RATE = 44100;

/// Wires the page up and starts it once the Start button is clicked.
/// Called once, from index.html's own inline module script.
export function runDevPage() {
  const canvas = document.getElementById(CANVAS_ID);
  const startButton = document.getElementById(START_BUTTON_ID);
  const statusEl = document.getElementById(STATUS_ID);
  const consoleEl = document.getElementById(CONSOLE_ID);

  const setStatus = (text) => {
    if (statusEl) statusEl.textContent = text;
  };

  const appendConsole = (text) => {
    if (consoleEl) consoleEl.textContent += text;
    if (text.length > 0) console.log('[amberfolio console]', text);
  };

  startButton.addEventListener('click', () => {
    startButton.disabled = true;
    startButton.textContent = 'running...';
    start({ canvas, setStatus, appendConsole }).catch((error) => {
      setStatus(`failed: ${error}`);
      console.error(error);
      startButton.disabled = false;
      startButton.textContent = 'start (retry)';
    });
  });
}

async function start({ canvas, setStatus, appendConsole }) {
  setStatus('loading the wasm module...');
  const { module, version, output } = await loadAmberfolio({
    print: appendConsole,
    printErr: appendConsole,
  });
  for (const line of output) appendConsole(`${line}\n`);

  setStatus(
    `amberfolio ${version.major}.${version.minor}.${version.patch} - ` +
      'creating the machine...',
  );

  const machine = new Machine(module);
  const attachStatus = machine.attachReferenceDevices();
  if (attachStatus !== AF_OK) {
    throw new Error(
      `af_machine_attach_reference_devices() answered ${attachStatus}`,
    );
  }

  const { writeStatus, entryStatus, size } = loadDemoProgram(machine);
  if (writeStatus !== AF_OK || entryStatus !== AF_OK) {
    throw new Error(
      `loading the demo program failed (write=${writeStatus}, entry=${entryStatus})`,
    );
  }
  appendConsole(`[host] embedded demo program loaded: ${size} bytes\n`);

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
  // effect (Space scrolling the page, Backspace navigating back, Tab
  // moving focus) because the dev page's whole surface is the machine
  // while it is running.
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

  setStatus(
    'running - the machine draws a pattern, plays a tone, and echoes ' +
      'whatever you type into the console below.',
  );

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
  let lastGeneration = -1;

  const frame = () => {
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

    // One frame's worth of audio, pulled and handed to the worklet
    // whether or not it is still connected — a detached port simply
    // drops the message, which is no worse than the underrun the
    // worklet already plays as silence.
    const audioFrames = Math.round(AUDIO_SAMPLE_RATE / 60);
    const { samples } = machine.renderAudio(audioFrames, AUDIO_SAMPLE_RATE);
    speakerNode.port.postMessage(samples, [samples.buffer]);

    if (status !== AF_OK) {
      setStatus(
        `the machine stopped (reason ${machine.stopReason()}) - see the ` +
          'console below for what it logged.',
      );
      // Keep presenting and draining the console after a stop
      // (platform.h: "the pulls keep working... it is the console and
      // the diagnostics that say why it stopped"), but there is nothing
      // left to run.
      return;
    }

    window.requestAnimationFrame(frame);
  };

  window.requestAnimationFrame(frame);
}
