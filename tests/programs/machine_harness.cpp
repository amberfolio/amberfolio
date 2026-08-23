// SPDX-License-Identifier: AGPL-3.0-only

#include "programs/machine_harness.h"

#include <array>
#include <chrono>
#include <stdexcept>
#include <string>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/dos.h"
#include "amberfolio/machine/int10.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/service_floor.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::programs {
namespace {

using machine::ticks;

/// The threshold a sample has to cross for `tone_periods` to call the
/// speaker high. Half of full drive, which is where a box-filtered square
/// wave spends none of its time — every sample of a settled tone is at
/// one rail or the other, and the samples that are not are the ones an
/// edge landed inside.
constexpr float high_threshold = machine::speaker_amplitude / 2;

/// How many silent samples end one tone and begin the next.
///
/// A tone's own rises are `period` samples apart; the gaps between the
/// tones these programs play are whole delay loops long. Anything in
/// between would be ambiguous, and nothing produces one.
constexpr std::size_t silence_gap_samples = 200;

/// Write `contents` to `path`, creating it. Throws on failure: every
/// staged file is written in this repository, so a filesystem that
/// refuses one is a mistake in a fixture.
void stage(machine::filesystem& fs, const staged_file& file) {
  const machine::dos_path path = parse_path(file.path);
  const auto handle = fs.create(path);
  if (!handle.ok()) {
    throw std::logic_error("cannot create " + std::string(file.path));
  }
  const auto wrote = fs.write(handle.value, file.contents);
  const bool ok = wrote.ok() && wrote.value == file.contents.size();
  const machine::vfs_error closed = fs.close(handle.value);
  if (!ok || closed != machine::vfs_error::none) {
    throw std::logic_error("cannot write " + std::string(file.path));
  }
}

/// Read `path` back in full. A path that does not exist answers
/// `present == false` rather than throwing — "the program deleted it" is
/// a result, not a failure.
[[nodiscard]] harvested_file harvest(machine::filesystem& fs,
                                     std::string_view raw) {
  const machine::dos_path path = parse_path(raw);
  const auto info = fs.stat(path);
  if (!info.ok()) {
    return {};
  }

  harvested_file result{.present = true, .contents = {}};
  result.contents.resize(info.value.size);
  if (result.contents.empty()) {
    return result;
  }

  const auto handle = fs.open(path, machine::open_mode::read_only);
  if (!handle.ok()) {
    return {};
  }
  const auto got = fs.read(handle.value, result.contents);
  static_cast<void>(fs.close(handle.value));
  result.contents.resize(got.ok() ? got.value : 0);
  return result;
}

}  // namespace

machine::dos_path parse_path(std::string_view raw) {
  const auto resolved =
      machine::canonicalize(machine::dos_path{}, {raw.data(), raw.size()});
  if (!resolved.ok()) {
    throw std::logic_error("not a DOS path: " + std::string(raw));
  }
  return resolved.value;
}

std::uint64_t frame_hash(const machine::framebuffer& frame) {
  std::uint64_t hash = 1469598103934665603ULL;
  const auto mix = [&hash](std::uint8_t byte) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  };
  for (const std::uint8_t pixel : frame.pixels()) {
    mix(pixel);
  }
  for (const machine::rgb& entry : frame.palette()) {
    mix(entry.red);
    mix(entry.green);
    mix(entry.blue);
  }
  return hash;
}

std::vector<ticks> tone_periods(std::span<const float> samples) {
  // Every rising crossing, as a sample index, with the runs of silence
  // between tones recorded as breaks.
  std::vector<std::size_t> rises;
  std::vector<std::size_t> breaks;
  bool high = false;
  std::size_t quiet = 0;

  for (std::size_t i = 0; i < samples.size(); ++i) {
    if (!high && samples[i] > high_threshold) {
      if (quiet >= silence_gap_samples && !rises.empty()) {
        breaks.push_back(rises.size());
      }
      rises.push_back(i);
      quiet = 0;
      high = true;
      continue;
    }
    if (high && samples[i] < high_threshold) {
      high = false;
    }
    if (!high) {
      ++quiet;
    } else {
      quiet = 0;
    }
  }

  breaks.push_back(rises.size());

  std::vector<ticks> periods;
  std::size_t first = 0;
  for (const std::size_t last : breaks) {
    // Two rises is the least that measures anything: one cycle.
    if (last - first >= 2) {
      const std::size_t span = rises[last - 1] - rises[first];
      const std::size_t cycles = last - first - 1;
      periods.push_back(static_cast<ticks>(span) * ticks_per_sample /
                        static_cast<ticks>(cycles));
    }
    first = last;
  }
  return periods;
}

machine_harness::machine_harness(const machine_setup& setup)
    : setup_(&setup),
      box_(std::make_unique<machine::machine>(machine::memory_layout::pc,
                                              &log_)),
      fs_(std::make_unique<machine::memory_filesystem>()),
      irq_(std::make_unique<machine::pic::controller>(*box_)),
      timer_(std::make_unique<machine::pit>(*box_, *irq_)),
      sound_(std::make_unique<machine::speaker>(*box_, *timer_)),
      video_(std::make_unique<machine::ega>(*box_)),
      screen_(std::make_unique<machine::renderer>(*box_, *video_)) {
  // Attach order is load-bearing since #100: the canonical state hashes
  // attached devices in attach order (machine/state.h,
  // `state_section::devices`), so a recording made against one wiring
  // verifies only against the same wiring. This order — interrupt
  // controller, timer, speaker, display — is `hosts/sdl`'s and
  // `reference_devices`' (core/src/abi.cpp) too. All three have to stay
  // the same list, or "the same run on every target" is not a claim
  // anything can check.
  //
  // Nothing else depends on it: every claim here is a distinct,
  // non-overlapping window or port range, so no bus dispatch changes.
  // Which is what makes it free to be chosen for the one thing that does.
  box_->attach(*irq_);
  box_->attach(*timer_);
  box_->attach(*sound_);
  box_->attach(*video_);

  // Registration order is the scheduler's tie-break (machine.h), so this
  // is also the order two devices due on the same tick are woken in: the
  // timer first, because an interrupt it raises should be visible to the
  // instruction the step goes on to run; then the speaker, whose edge is
  // a consequence of the channel the timer just moved; then the frame.
  box_->schedule(timer_->channel0_deadline());
  box_->schedule(timer_->channel2_deadline());
  box_->schedule(*sound_);
  box_->schedule(*screen_);

  box_->set_filesystem(*fs_);
  machine::install_int10(box_->services());
  machine::install_dos_services(box_->services());

  // The RESET line, then the one thing it cannot do: a renderer is a
  // scheduled participant and not a device, so `machine::reset()` does
  // not know to re-arm it (renderer.h says so, and says whoever owns one
  // must).
  box_->reset();
  screen_->reset();
  box_->set_step_cost(setup.step_cost);

  // The program's own seams, registered before anything is loaded — the
  // registry is wiring, like an attached device, and a definition that
  // does not fit is a mistake in a fixture rather than a run-time
  // condition.
  for (const machine::seam_definition* seam : setup.seam_definitions) {
    if (seam == nullptr || !box_->seams().add(*seam)) {
      throw std::logic_error("a seam definition could not be registered");
    }
  }

  // `reset()` bumped the audio timeline's epoch, and the first `render()`
  // after an epoch bump throws away whatever the ring holds (platform.h).
  // Spending that call here, on an empty ring, is what a host's audio
  // thread does by simply having been running already; doing it on
  // purpose is what keeps the first real pull from discarding a tone.
  std::array<float, 1> priming{};
  static_cast<void>(box_->audio().render(priming, audio_sample_rate));
  primed_underruns_ = box_->audio().underruns();

  presented_ = box_->display().generation();
}

machine_harness::~machine_harness() = default;

std::uint16_t machine_harness::result_segment() const noexcept {
  return setup_->exe.empty() ? machine_layout::code_segment
                             : result_.loaded.load_segment;
}

bool machine_harness::start() {
  for (const staged_file& file : setup_->files) {
    stage(*fs_, file);
  }

  next_frame_ = machine::renderer::frame_period;

  if (!setup_->exe.empty()) {
    stage(*fs_, {.path = setup_->exe_path, .contents = setup_->exe});
    const auto loaded = machine::load_program(
        *box_, *fs_, parse_path(setup_->exe_path),
        std::span<const char>(setup_->command_tail.data(),
                              setup_->command_tail.size()));
    result_.load_error = loaded.error;
    result_.loaded = loaded.value;
    running_ = loaded.ok();
    if (!loaded.ok()) {
      return false;
    }

    // Identify what was loaded and turn on what was asked for, in that
    // order — a seam is keyed to the file's fingerprint and placed
    // against the segment the loader chose (machine/seam.h), and both
    // are known only now. A refusal is a fixture mistake: the program
    // and its seam were written together.
    if (!box_->seams().identify(*fs_, parse_path(setup_->exe_path),
                                loaded.value.load_segment)) {
      throw std::logic_error("the loaded program could not be fingerprinted");
    }
    for (const std::string_view id : setup_->seams) {
      if (box_->seams().enable(id) != machine::seam_reason::none) {
        throw std::logic_error("seam " + std::string(id) + " was refused");
      }
    }
    // And the triggers the fixture asks for, after the enables and
    // before the first step. A refusal is a fixture mistake for the same
    // reason an enable's is.
    for (const std::string_view id : setup_->pulls) {
      if (box_->seams().pull(id, box_->time()) != machine::seam_reason::none) {
        throw std::logic_error("seam " + std::string(id) +
                               " would not be pulled");
      }
    }
    return true;
  }

  // A raw image, placed through `memory().ram()` and not through a bus
  // cycle: this is the machine loading a program, not the program writing
  // memory, and memory_map.h keeps the two apart on purpose.
  const std::span<std::uint8_t> ram = box_->memory().ram();
  for (std::size_t i = 0; i < setup_->code.size(); ++i) {
    ram[cpu::physical_address(machine_layout::code_segment,
                              static_cast<std::uint16_t>(i))] = setup_->code[i];
  }

  cpu::registers& regs = box_->processor().regs();
  regs[cpu::sreg::cs] = machine_layout::code_segment;
  regs[cpu::sreg::ds] = machine_layout::code_segment;
  regs[cpu::sreg::es] = machine_layout::code_segment;
  regs[cpu::sreg::ss] = machine_layout::code_segment;
  regs[cpu::reg16::sp] = machine_layout::stack_pointer;
  regs.ip = 0;
  return true;
}

bool machine_harness::turn() {
  if (!running_) {
    return false;
  }

  // Every key whose moment has arrived, posted at the machine's own tick
  // — which is what `post_key()` timestamps it with, and the reason the
  // loop runs to a key's tick rather than posting it at the nearest frame
  // boundary.
  const std::vector<scripted_key>& keys = setup_->keys;
  while (keys_posted_ < keys.size() && keys[keys_posted_].at <= box_->time()) {
    const scripted_key& key = keys[keys_posted_];
    box_->post_key(key.scancode, key.action);
    ++keys_posted_;
  }

  ticks target = next_frame_;
  if (keys_posted_ < keys.size() && keys[keys_posted_].at < target) {
    target = keys[keys_posted_].at;
  }

  const auto started = std::chrono::steady_clock::now();
  const machine::run_result ran = box_->run(target);
  const auto ended = std::chrono::steady_clock::now();

  result_.seconds += std::chrono::duration<double>(ended - started).count();
  result_.steps += ran.steps;
  result_.time = box_->time();

  while (next_frame_ <= box_->time()) {
    next_frame_ += machine::renderer::frame_period;
  }

  // Present, drain, pull — platform.h's order, and the whole of what a
  // host does between two runs.
  const std::uint64_t generation = box_->display().generation();
  if (generation != presented_) {
    presented_ = generation;
    ++result_.frames;
    result_.frame_hash = frame_hash(box_->display());

    const std::span<const std::uint8_t> pixels = box_->display().pixels();
    const std::span<const machine::rgb> palette = box_->display().palette();
    result_.frame_pixels.assign(pixels.begin(), pixels.end());
    result_.frame_palette.assign(palette.begin(), palette.end());
  }

  std::array<std::uint8_t, 256> drained{};
  for (;;) {
    const std::size_t got = box_->console().read(drained);
    if (got == 0) {
      break;
    }
    result_.console.insert(result_.console.end(), drained.begin(),
                           drained.begin() + static_cast<std::ptrdiff_t>(got));
  }

  // Pulled by settled time rather than by a fixed block size: the loop
  // above runs to a key's tick as readily as to a frame boundary, so
  // "one frame's worth of samples" is not a constant here. Asking for
  // exactly what the horizon has settled is also what guarantees the
  // underrun count stays zero.
  const ticks settled = box_->audio().horizon();
  const ticks played = box_->audio().playback_position();
  if (settled > played) {
    const auto wanted =
        static_cast<std::size_t>((settled - played) / ticks_per_sample);
    if (wanted > 0) {
      std::vector<float> block(wanted, 0.0F);
      static_cast<void>(box_->audio().render(block, audio_sample_rate));
      result_.audio.insert(result_.audio.end(), block.begin(), block.end());
    }
  }

  if (box_->stopped()) {
    running_ = false;
  }
  if (result_.steps >= setup_->step_cap) {
    result_.capped = true;
    running_ = false;
  }
  return running_;
}

machine_outcome machine_harness::finish() {
  result_.stop = box_->stop();
  result_.cpu_stop = log_.cpu_stop;
  result_.notices = log_.notices;
  result_.device_stops = log_.device_stops;
  result_.service_calls = log_.service_calls;
  result_.underruns = box_->audio().underruns() - primed_underruns_;
  result_.seam_events = log_.seam_events;
  result_.file_events = log_.files;

  result_.results.clear();
  const std::span<const std::uint8_t> ram = box_->memory().ram();
  for (std::size_t i = 0; i < setup_->result_words; ++i) {
    const std::uint32_t at = cpu::physical_address(
        result_segment(),
        static_cast<std::uint16_t>(machine_layout::result_offset + 2 * i));
    result_.results.push_back(static_cast<std::uint16_t>(
        ram[at] | (static_cast<unsigned>(ram[at + 1]) << 8U)));
  }

  result_.files.clear();
  for (const std::string_view path : setup_->read_back) {
    result_.files.push_back(harvest(*fs_, path));
  }

  return result_;
}

machine_outcome run_machine_setup(const machine_setup& setup) {
  machine_harness harness(setup);
  if (harness.start()) {
    while (harness.turn()) {
      // The loop is the whole of it; `turn()` answers when to stop.
    }
  }
  return harness.finish();
}

}  // namespace amberfolio::programs
