// SPDX-License-Identifier: AGPL-3.0-only
//
// The SDL3 desktop host: one host for Windows, macOS and Linux
// (PLAN.md §4). It builds a machine, points it at a directory, loads a
// program, and gives it a screen, a speaker and a keyboard.
//
//     amberfolio <dir> <program.exe> [--headless] [--scale N]
//
// `--headless` opens no window and no audio device. That is what keeps
// the CI smoke test meaningful on a runner with neither, and it is the
// path M2-T1's host checks take.
//
//
// The loop, and the one rule it exists to honour
// ----------------------------------------------
//
// PLAN.md §4: "host wall time only throttles presentation, outside
// machine state." So the loop is:
//
//     run the machine forward in *virtual* time to the next frame
//     boundary → present whatever frame that produced → sleep whatever
//     *wall* time is left over
//
// and never the other way round. If the host cannot keep up, the sleep
// is simply zero and presentation falls behind; virtual time is not
// slowed, not skipped, and not consulted about how long any of it took.
// A frame that was composed while the host was busy is dropped by the
// generation counter (platform.h) rather than delaying the machine.
//
// The corollary is that this host never asks the machine to catch up. A
// long stall on the host side does not become a burst of emulated
// instructions; the machine's clock is its own, and the only thing wall
// time decides is when we draw and how long we idle.
//
//
// Audio, and the thread that is allowed to touch it
// -------------------------------------------------
//
// `audio_timeline::render()` is the only function in the core that may be
// called off the machine thread, and by exactly one thread — not one at a
// time (platform.h states this contract). SDL's audio stream callback is
// that thread and the only place this file calls it. Everything else —
// `run()`, key posting, frame reads — happens on the main thread.
//
// An underrun is the host's problem: `render()` fills what it can and the
// rest is silence. Nothing back-pressures into machine state, because a
// machine that ran slower when the speaker was starved would no longer be
// deterministic, which is the whole point of the edge list being the
// canonical state rather than the samples.
//
//
// What is deliberately not here
// -----------------------------
//
// Config file, onboarding, gamepad and the virtual keyboard are M6. The
// period-correct non-square-pixel option PLAN.md §4 lists is a `--scale`
// integer for now and an obvious place to grow an aspect mode; M4's
// polish is where that gets decided rather than guessed at here.

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/dos.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/int10.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/pic.h"
#include "amberfolio/machine/pit.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/renderer.h"
#include "amberfolio/machine/speaker.h"
#include "amberfolio/version.h"
#include "directory_vfs.h"

// <cstdio> rather than std::format/std::print, and not only for the wasm
// host's reason (bundle size). libc++ gates std::format's floating-point
// path behind macOS 13.3 availability, so *any* std::format call fails to
// compile against a deployment target of 11.0 — which is what the macos
// preset asks for. Revisit if that floor ever rises.

namespace {

using namespace amberfolio;

constexpr unsigned default_scale = 3;
constexpr unsigned audio_sample_rate = 48000;

/// Everything the machine is made of, in one place so its construction
/// order is visible: the PIC exists before the PIT that raises IRQ0
/// through it, and the PIT before the speaker that gates channel 2.
struct wired_machine {
  explicit wired_machine(machine::diagnostics* log)
      : box(std::make_unique<machine::machine>(machine::memory_layout::pc,
                                               log)),
        irq(*box),
        timer(*box, irq),
        spk(*box, timer),
        video(std::make_unique<machine::ega>()),
        render(*box, *video) {
    box->attach(irq);
    box->attach(timer);
    box->attach(spk);
    box->attach(*video);

    box->schedule(timer.channel0_deadline());
    box->schedule(timer.channel2_deadline());
    box->schedule(spk);
    box->schedule(render);

    machine::install_int10(box->services());
    machine::install_dos_services(box->services());

    box->reset();
    render.reset();
  }

  std::unique_ptr<machine::machine> box;
  machine::pic::controller irq;
  machine::pit timer;
  machine::speaker spk;
  std::unique_ptr<machine::ega> video;
  machine::renderer render;
};

/// Reports what the core would not fake, to stderr. A host has to have
/// one of these or "log, don't fake" is only half a mechanism
/// (machine/diagnostics.h).
class stderr_diagnostics final : public machine::diagnostics {
 public:
  void report(const machine::notice& what) override {
    std::fprintf(stderr, "amberfolio: nothing answers at %05X (kind %u)\n",
                 what.at, static_cast<unsigned>(what.what));
  }

  void report(const machine::stop_record& stop) override {
    // A program exiting is not a diagnostic. It is the run ending
    // the way it was asked to, and main() turns it into this
    // process's exit code; saying anything on stderr would make
    // every ordinary run look as though something had gone wrong.
    if (stop.reason == machine::stop_reason::program_exited) {
      return;
    }
    std::fprintf(stderr, "amberfolio: machine stopped, reason %u at %05X\n",
                 static_cast<unsigned>(stop.reason), stop.at);
  }

  void report(const cpu::stop_record& stop) override {
    std::fprintf(stderr,
                 "amberfolio: cpu stopped on opcode %02X at %04X:%04X\n",
                 stop.opcode, stop.cs, stop.ip);
  }

  void report(const machine::device_stop& stop) override {
    std::fprintf(stderr, "amberfolio: device declined %04X\n", stop.at);
  }

  void report(const machine::service_call& call) override {
    (void)call;  // Traced only when someone asks; silent by default.
  }
};

/// Drain whatever DOS console output has accumulated to stdout.
///
/// Pulled, not pushed: `console_output` is a buffer the host empties, not
/// a sink the core writes through, because nothing in core ever calls out
/// (platform.h). There is no text mode to render to and none is planned,
/// so stdout is the whole of what a program's console output means here.
void drain_console(machine::machine& box) {
  std::array<std::uint8_t, 256> buffer{};
  for (;;) {
    const std::size_t got = box.console().read(buffer);
    if (got == 0) {
      return;
    }
    std::fwrite(buffer.data(), 1, got, stdout);
  }
}

/// SDL scancode → the raw XT make/break code the core's translation table
/// expects. The table itself lives in core (machine/keyboard.h) because
/// programs read the BDA shift flags directly; this is only the mapping
/// from the host's own event vocabulary onto the wire the machine has.
[[nodiscard]] std::uint8_t xt_scancode(SDL_Scancode code) {
  switch (code) {
    case SDL_SCANCODE_ESCAPE:
      return 0x01;
    case SDL_SCANCODE_1:
      return 0x02;
    case SDL_SCANCODE_2:
      return 0x03;
    case SDL_SCANCODE_3:
      return 0x04;
    case SDL_SCANCODE_4:
      return 0x05;
    case SDL_SCANCODE_5:
      return 0x06;
    case SDL_SCANCODE_6:
      return 0x07;
    case SDL_SCANCODE_7:
      return 0x08;
    case SDL_SCANCODE_8:
      return 0x09;
    case SDL_SCANCODE_9:
      return 0x0A;
    case SDL_SCANCODE_0:
      return 0x0B;
    case SDL_SCANCODE_MINUS:
      return 0x0C;
    case SDL_SCANCODE_EQUALS:
      return 0x0D;
    case SDL_SCANCODE_BACKSPACE:
      return 0x0E;
    case SDL_SCANCODE_TAB:
      return 0x0F;
    case SDL_SCANCODE_Q:
      return 0x10;
    case SDL_SCANCODE_W:
      return 0x11;
    case SDL_SCANCODE_E:
      return 0x12;
    case SDL_SCANCODE_R:
      return 0x13;
    case SDL_SCANCODE_T:
      return 0x14;
    case SDL_SCANCODE_Y:
      return 0x15;
    case SDL_SCANCODE_U:
      return 0x16;
    case SDL_SCANCODE_I:
      return 0x17;
    case SDL_SCANCODE_O:
      return 0x18;
    case SDL_SCANCODE_P:
      return 0x19;
    case SDL_SCANCODE_LEFTBRACKET:
      return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET:
      return 0x1B;
    case SDL_SCANCODE_RETURN:
      return 0x1C;
    case SDL_SCANCODE_LCTRL:
      return 0x1D;
    case SDL_SCANCODE_A:
      return 0x1E;
    case SDL_SCANCODE_S:
      return 0x1F;
    case SDL_SCANCODE_D:
      return 0x20;
    case SDL_SCANCODE_F:
      return 0x21;
    case SDL_SCANCODE_G:
      return 0x22;
    case SDL_SCANCODE_H:
      return 0x23;
    case SDL_SCANCODE_J:
      return 0x24;
    case SDL_SCANCODE_K:
      return 0x25;
    case SDL_SCANCODE_L:
      return 0x26;
    case SDL_SCANCODE_SEMICOLON:
      return 0x27;
    case SDL_SCANCODE_APOSTROPHE:
      return 0x28;
    case SDL_SCANCODE_GRAVE:
      return 0x29;
    case SDL_SCANCODE_LSHIFT:
      return 0x2A;
    case SDL_SCANCODE_BACKSLASH:
      return 0x2B;
    case SDL_SCANCODE_Z:
      return 0x2C;
    case SDL_SCANCODE_X:
      return 0x2D;
    case SDL_SCANCODE_C:
      return 0x2E;
    case SDL_SCANCODE_V:
      return 0x2F;
    case SDL_SCANCODE_B:
      return 0x30;
    case SDL_SCANCODE_N:
      return 0x31;
    case SDL_SCANCODE_M:
      return 0x32;
    case SDL_SCANCODE_COMMA:
      return 0x33;
    case SDL_SCANCODE_PERIOD:
      return 0x34;
    case SDL_SCANCODE_SLASH:
      return 0x35;
    case SDL_SCANCODE_RSHIFT:
      return 0x36;
    case SDL_SCANCODE_LALT:
      return 0x38;
    case SDL_SCANCODE_SPACE:
      return 0x39;
    case SDL_SCANCODE_CAPSLOCK:
      return 0x3A;
    case SDL_SCANCODE_F1:
      return 0x3B;
    case SDL_SCANCODE_F2:
      return 0x3C;
    case SDL_SCANCODE_F3:
      return 0x3D;
    case SDL_SCANCODE_F4:
      return 0x3E;
    case SDL_SCANCODE_F5:
      return 0x3F;
    case SDL_SCANCODE_F6:
      return 0x40;
    case SDL_SCANCODE_F7:
      return 0x41;
    case SDL_SCANCODE_F8:
      return 0x42;
    case SDL_SCANCODE_F9:
      return 0x43;
    case SDL_SCANCODE_F10:
      return 0x44;
    case SDL_SCANCODE_NUMLOCKCLEAR:
      return 0x45;
    case SDL_SCANCODE_SCROLLLOCK:
      return 0x46;
    case SDL_SCANCODE_HOME:
      return 0x47;
    case SDL_SCANCODE_UP:
      return 0x48;
    case SDL_SCANCODE_PAGEUP:
      return 0x49;
    case SDL_SCANCODE_LEFT:
      return 0x4B;
    case SDL_SCANCODE_RIGHT:
      return 0x4D;
    case SDL_SCANCODE_END:
      return 0x4F;
    case SDL_SCANCODE_DOWN:
      return 0x50;
    case SDL_SCANCODE_PAGEDOWN:
      return 0x51;
    case SDL_SCANCODE_INSERT:
      return 0x52;
    case SDL_SCANCODE_DELETE:
      return 0x53;
    default:
      return 0;
  }
}

/// The audio callback's shared state. `box` is only ever read for its
/// `audio()`, and `render()` is the one core call the contract allows off
/// the machine thread.
struct audio_bridge {
  machine::machine* box{};
  std::vector<float> scratch;
};

void SDLCALL feed_audio(void* userdata, SDL_AudioStream* stream, int additional,
                        int /*total*/) {
  auto* bridge = static_cast<audio_bridge*>(userdata);
  if (bridge == nullptr || bridge->box == nullptr || additional <= 0) {
    return;
  }

  const auto wanted = static_cast<std::size_t>(additional) / sizeof(float);
  if (bridge->scratch.size() < wanted) {
    // Grown on the audio thread, which is not ideal, but it happens once
    // per device-buffer size rather than per callback and the alternative
    // is guessing SDL's buffer size before it tells us.
    bridge->scratch.resize(wanted);
  }

  const std::span<float> out(bridge->scratch.data(), wanted);
  bridge->box->audio().render(out, audio_sample_rate);
  SDL_PutAudioStreamData(stream, out.data(),
                         static_cast<int>(wanted * sizeof(float)));
}

struct options {
  std::filesystem::path root;
  std::string program;
  bool headless{false};
  unsigned scale{default_scale};
  bool valid{false};
};

[[nodiscard]] options parse(int argc, char** argv) {
  options opts;
  std::vector<std::string_view> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--headless") {
      opts.headless = true;
    } else if (arg == "--scale" && i + 1 < argc) {
      // strtol rather than atoi, which cannot tell "0" from "not a
      // number" - a distinction worth having when the answer decides
      // how big a window is.
      char* end = nullptr;
      const long value = std::strtol(argv[++i], &end, 10);
      opts.scale = (end != nullptr && *end == '\0' && value > 0)
                       ? static_cast<unsigned>(value)
                       : default_scale;
    } else if (arg.starts_with("--")) {
      std::fprintf(stderr, "amberfolio: unknown option %.*s\n",
                   static_cast<int>(arg.size()), arg.data());
      return opts;
    } else {
      positional.push_back(arg);
    }
  }

  if (positional.size() != 2) {
    std::fprintf(stderr,
                 "usage: amberfolio <dir> <program.exe> [--headless]"
                 " [--scale N]\n");
    return opts;
  }
  opts.root = std::filesystem::path(positional[0]);
  opts.program = std::string(positional[1]);
  opts.valid = true;
  return opts;
}

}  // namespace

int main(int argc, char** argv) try {
  const options opts = parse(argc, argv);
  if (!opts.valid) {
    return EXIT_FAILURE;
  }

  sdl::directory_filesystem files(opts.root);
  if (!files.usable()) {
    std::fprintf(stderr, "amberfolio: %s is not a directory\n",
                 opts.root.string().c_str());
    return EXIT_FAILURE;
  }

  stderr_diagnostics log;
  wired_machine wired(&log);
  machine::machine& box = *wired.box;
  box.set_filesystem(files);

  const machine::vfs_result<machine::dos_path> where = machine::canonicalize(
      machine::dos_path{},
      std::span<const char>(opts.program.data(), opts.program.size()));
  if (!where.ok()) {
    std::fprintf(stderr, "amberfolio: %s is not a usable DOS name\n",
                 opts.program.c_str());
    return EXIT_FAILURE;
  }

  const machine::loader_result<machine::loaded_program> loaded =
      machine::load_program(box, files, where.value);
  if (!loaded.ok()) {
    std::fprintf(stderr, "amberfolio: cannot load %s (loader error %u)\n",
                 opts.program.c_str(), static_cast<unsigned>(loaded.error));
    return EXIT_FAILURE;
  }

  const std::uint32_t init_flags =
      opts.headless ? 0U : (SDL_INIT_VIDEO | SDL_INIT_AUDIO);
  if (!SDL_Init(init_flags)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* texture = nullptr;
  SDL_AudioStream* audio = nullptr;
  audio_bridge bridge;

  if (!opts.headless) {
    const int w = static_cast<int>(machine::frame_width * opts.scale);
    const int h = static_cast<int>(machine::frame_height * opts.scale);
    if (!SDL_CreateWindowAndRenderer("amberfolio", w, h, 0, &window,
                                     &renderer)) {
      std::fprintf(stderr, "SDL_CreateWindowAndRenderer failed: %s\n",
                   SDL_GetError());
      SDL_Quit();
      return EXIT_FAILURE;
    }
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                static_cast<int>(machine::frame_width),
                                static_cast<int>(machine::frame_height));
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    bridge.box = &box;
    const SDL_AudioSpec spec{.format = SDL_AUDIO_F32,
                             .channels = 1,
                             .freq = static_cast<int>(audio_sample_rate)};
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                      feed_audio, &bridge);
    if (audio != nullptr) {
      SDL_ResumeAudioStreamDevice(audio);
    }
  }

  // One virtual frame is the renderer's own period, so the loop and the
  // renderer agree by construction rather than by two constants matching.
  const machine::ticks frame_ticks = machine::renderer::frame_period;
  std::uint64_t presented = 0;
  std::vector<std::uint32_t> argb(machine::frame_pixels);
  bool quit = false;

  while (!quit && !box.stopped()) {
    const auto frame_started = std::chrono::steady_clock::now();

    if (!opts.headless) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
          quit = true;
        } else if (event.type == SDL_EVENT_KEY_DOWN ||
                   event.type == SDL_EVENT_KEY_UP) {
          const std::uint8_t code = xt_scancode(event.key.scancode);
          if (code != 0 && !event.key.repeat) {
            box.post_key(code, event.type == SDL_EVENT_KEY_DOWN
                                   ? machine::key_action::down
                                   : machine::key_action::up);
          }
        }
      }
    }

    // Virtual time first, and to a boundary the machine chose. Nothing
    // about how long the last frame took on the wall gets to influence
    // how much machine time passes here.
    box.run(box.time() + frame_ticks);
    drain_console(box);

    if (!opts.headless && box.display().generation() != presented) {
      presented = box.display().generation();
      const std::span<const std::uint8_t> pixels = box.display().pixels();
      const std::span<const machine::rgb> palette = box.display().palette();
      for (std::size_t i = 0; i < argb.size(); ++i) {
        const machine::rgb color = palette[pixels[i] & 0x0FU];
        argb[i] = (static_cast<std::uint32_t>(color.red) << 16) |
                  (static_cast<std::uint32_t>(color.green) << 8) |
                  static_cast<std::uint32_t>(color.blue);
      }
      SDL_UpdateTexture(
          texture, nullptr, argb.data(),
          static_cast<int>(machine::frame_width * sizeof(std::uint32_t)));
      SDL_RenderClear(renderer);
      SDL_RenderTexture(renderer, texture, nullptr, nullptr);
      SDL_RenderPresent(renderer);
    }

    if (!opts.headless) {
      // Whatever wall time is left of this frame, and never a negative
      // one: a host that fell behind simply does not sleep. It does not
      // then run the machine faster to compensate — see this file's top
      // comment.
      const auto spent = std::chrono::steady_clock::now() - frame_started;
      const auto budget = std::chrono::duration<double>(
          static_cast<double>(frame_ticks) / machine::pit_input_hz);
      const auto left =
          std::chrono::duration_cast<std::chrono::milliseconds>(budget - spent);
      if (left.count() > 0) {
        std::this_thread::sleep_for(left);
      }
    }
  }

  if (audio != nullptr) {
    SDL_DestroyAudioStream(audio);
  }
  if (texture != nullptr) {
    SDL_DestroyTexture(texture);
  }
  if (renderer != nullptr) {
    SDL_DestroyRenderer(renderer);
  }
  if (window != nullptr) {
    SDL_DestroyWindow(window);
  }
  SDL_Quit();

  const machine::stop_record& stop = box.stop();
  if (stop.reason == machine::stop_reason::program_exited) {
    std::fflush(stdout);
    return static_cast<int>(stop.exit_code);
  }
  if (quit) {
    return EXIT_SUCCESS;
  }

  std::fprintf(stderr, "amberfolio: stopped without exiting (reason %u)\n",
               static_cast<unsigned>(stop.reason));
  return EXIT_FAILURE;
} catch (const std::exception& e) {
  // A function-try-block on main, because everything above allocates -
  // the megabyte of machine, the frame buffer, the host's own strings -
  // and a host that lets an allocation failure escape as an unhandled
  // exception tells the player nothing at all.
  std::fprintf(stderr, "amberfolio: %s\n", e.what());
  return EXIT_FAILURE;
} catch (...) {
  std::fprintf(stderr, "amberfolio: unknown error\n");
  return EXIT_FAILURE;
}
