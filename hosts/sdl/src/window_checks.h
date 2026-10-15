// SPDX-License-Identifier: AGPL-3.0-only
//
// Checking the paths a headless run cannot (#80, #472, split from main.cpp):
// `--verify`'s readback of the window, its tally, and the key event
// `--press` pushes.
//
// Checking the paths a headless run cannot
// ----------------------------------------
//
// Everything above the window — the machine, the VFS, the loader, the
// console, the exit code — is what `--headless` exercises and what CI has
// checked since M2-H1. Everything at the window was compiled and never
// run (#80): the texture upload, the integer scaling, the audio callback,
// and the step from an SDL key event to a posted XT scan code. "It
// compiles" is not the same claim as "it works", and the difference was
// due to be discovered by a game that has its own problems.
//
// So two options, and neither of them fakes anything:
//
//   --verify           after each frame is drawn and before it is
//                      presented, read the render target back and compare
//                      every pixel of it against the bytes this host
//                      uploaded. Count what the audio callback did on its
//                      own thread, and what the timeline thought of the
//                      pacing while it did it — underruns, resyncs and
//                      dropped edges (M4-A1, #106). Report all of it on
//                      stderr at exit, and fail the process if the
//                      picture did not match or if nothing was ever
//                      presented.
//
//   --press KEY@FRAME  push a real SDL keyboard event — down and up — into
//                      SDL's own queue at frame FRAME, so it comes back
//                      out of SDL_PollEvent and travels the same path a
//                      typed key does, mapping table included. KEY is
//                      whatever SDL_GetScancodeFromName accepts: `A`,
//                      `Escape`, `Left`, `Keypad 5`. `KEY@FRAME:down`
//                      pushes only the make and `KEY@FRAME:up` only the
//                      break, for holding a modifier across other keys
//                      (press_spec.h, #313).
//
// Together they let one CTest case run the M2-T1 composite program in a
// real window, with a real audio device, on every desktop target — under
// SDL's `dummy` video and audio drivers, which are still the real SDL
// code paths, only pointed at no hardware. What that
// cannot check is the last inch: a photon leaving a display, a pressure
// wave leaving a speaker. docs/hosts.md says how a person checks those,
// and that is the part of #80 no runner can close.
//
// A note on the readback, since it is the load-bearing half. It happens
// *before* SDL_RenderPresent, because on an accelerated backend the
// contents of the back buffer after a present are undefined; before it,
// the target still holds what was drawn on every backend. And it derives
// its expectation rather than pinning a hash: each target pixel must
// equal the source pixel at (x/scale, y/scale), which is the definition
// of nearest-neighbour integer scaling and not a golden of whatever this
// machine happened to produce.
//

#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <span>

#include "amberfolio/machine/machine.h"
#include "audio_device.h"

namespace amberfolio::sdl {

/// Everything `--verify` has to say at the end of a run.
struct verify_report {
  std::uint64_t composed{};    ///< Frames the renderer finished.
  std::uint64_t presented{};   ///< Frames this host uploaded and presented.
  std::uint64_t checked{};     ///< Presented frames read back and compared.
  std::uint64_t mismatched{};  ///< Pixels that came back wrong, in total.
  std::uint64_t unreadable{};  ///< Presents whose target would not read back.
  std::uint64_t odd_size{};    ///< Presents whose target was not a whole
                               ///< multiple of the frame (a HiDPI backing
                               ///< store, say) and so was not compared.
  std::uint64_t keys{};        ///< Key events this host posted to the machine.
};

/// Push one SDL key event, as though a keyboard had sent it.
///
/// `SDL_PushEvent` puts it on the same queue a device driver's events go
/// on, so it comes back out of the `SDL_PollEvent` loop below and is
/// mapped, filtered and posted by exactly the code a typed key meets.
/// An event this host synthesized and then handled itself would prove
/// nothing about that code, which is the whole point.
void push_key_event(SDL_Window* window, SDL_Scancode code, bool down);

/// Read the render target back and compare it, pixel for pixel, with the
/// buffer this host uploaded.
///
/// The expectation is derived, not stored: nearest-neighbour scaling by
/// an integer factor means target pixel (x, y) is source pixel
/// (x / scale, y / scale), and nothing else. So this checks the upload,
/// the scaling and the draw in one pass over the whole target, without a
/// golden anywhere — a wrong stride, a swapped colour channel, a texture
/// that never got the new frame and a scale that is not integer all show
/// up as a mismatch count rather than as a picture nobody looked at.
///
/// Called before `SDL_RenderPresent`: after it, an accelerated backend's
/// back buffer holds whatever the driver left there.
void verify_target(SDL_Renderer* renderer, std::span<const std::uint32_t> src,
                   verify_report& report);

/// `--verify`'s tally and its verdict, printed at the end of the run. False
/// when the check failed -- the caller's exit code is then the check's, ahead
/// of the program's own. What makes this a check rather than a printout is
/// below: a run that presented nothing proves nothing, and neither does one
/// whose every present was unreadable.
[[nodiscard]] bool report_verify(verify_report& report,
                                 const machine::machine& box,
                                 const audio_bridge& bridge);

}  // namespace amberfolio::sdl
