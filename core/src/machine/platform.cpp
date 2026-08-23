// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/platform.h"

#include <array>
#include <cstdint>

#include "amberfolio/machine/state.h"

namespace amberfolio::machine {
namespace {

// --- The civil calendar -----------------------------------------------
//
// `wall_clock` keeps one number — hundredths of a second since the DOS
// epoch — and DOS 2Ah wants a year, a month, a day and a weekday. These
// two functions are the conversion, and they are Howard Hinnant's
// well-known days_from_civil / civil_from_days pair for the proleptic
// Gregorian calendar: integer-only, no tables, no leap-year special
// cases, correct for every year in and far outside the range we accept.
//
// Written out here rather than reached for through the standard
// library's calendar types because core/ is freestanding: those types
// are not, and the arithmetic is twenty lines.

/// Days since 1970-01-01 for a Gregorian date. `month` is 1-12 and `day`
/// is a real day of that month; the callers below have already checked.
constexpr std::int64_t days_from_civil(int year, unsigned month,
                                       unsigned day) noexcept {
  // The year is shifted so that it starts in March, which is what makes
  // the leap day the last day of the year and so removes every special
  // case from the arithmetic that follows.
  const int shifted = year - (month <= 2 ? 1 : 0);
  const std::int64_t era = (shifted >= 0 ? shifted : shifted - 399) / 400;
  const auto year_of_era =
      static_cast<std::uint32_t>(shifted - static_cast<int>(era * 400));
  const std::uint32_t march_month = month > 2 ? month - 3 : month + 9;
  const std::uint32_t day_of_year = (153u * march_month + 2u) / 5u + day - 1u;
  const std::uint32_t day_of_era =
      year_of_era * 365u + year_of_era / 4u - year_of_era / 100u + day_of_year;
  return era * 146097 + static_cast<std::int64_t>(day_of_era) - 719468;
}

struct civil_date {
  int year{};
  unsigned month{};
  unsigned day{};
};

/// The inverse: a Gregorian date from days since 1970-01-01.
constexpr civil_date civil_from_days(std::int64_t days) noexcept {
  const std::int64_t shifted = days + 719468;
  const std::int64_t era = (shifted >= 0 ? shifted : shifted - 146096) / 146097;
  const auto day_of_era = static_cast<std::uint32_t>(shifted - era * 146097);
  const std::uint32_t year_of_era =
      (day_of_era - day_of_era / 1460u + day_of_era / 36524u -
       day_of_era / 146096u) /
      365u;
  const std::int64_t year = static_cast<std::int64_t>(year_of_era) + era * 400;
  const std::uint32_t day_of_year =
      day_of_era - (365u * year_of_era + year_of_era / 4u - year_of_era / 100u);
  const std::uint32_t march_month = (5u * day_of_year + 2u) / 153u;
  const std::uint32_t day = day_of_year - (153u * march_month + 2u) / 5u + 1u;
  const std::uint32_t month =
      march_month < 10u ? march_month + 3u : march_month - 9u;
  return {.year = static_cast<int>(year + (month <= 2 ? 1 : 0)),
          .month = month,
          .day = day};
}

/// 1980-01-01, the DOS epoch, in days since 1970-01-01. Asserted rather
/// than trusted, because everything `wall_clock` reports is offset from
/// it.
inline constexpr std::int64_t dos_epoch_days = 3652;
static_assert(days_from_civil(1980, 1, 1) == dos_epoch_days);

/// The weekday of the DOS epoch, so the default-constructed `wall_time`
/// in the header can state it: 1980-01-01 was a Tuesday.
static_assert((dos_epoch_days + 4) % 7 == 2);

// Each built from the one before it rather than from a fresh product of
// literals: the products are done in the type they are stored in, so
// there is no `int` multiplication being widened afterwards, and the
// arithmetic reads as the conversion it is.
inline constexpr std::uint64_t centiseconds_per_second = 100;
inline constexpr std::uint64_t centiseconds_per_minute =
    60 * centiseconds_per_second;
inline constexpr std::uint64_t centiseconds_per_hour =
    60 * centiseconds_per_minute;
inline constexpr std::uint64_t centiseconds_per_day =
    24 * centiseconds_per_hour;

constexpr bool is_leap_year(unsigned year) noexcept {
  return (year % 4u == 0 && year % 100u != 0) || year % 400u == 0;
}

constexpr unsigned days_in_month(unsigned year, unsigned month) noexcept {
  constexpr std::array<unsigned, 12> lengths{31, 28, 31, 30, 31, 30,
                                             31, 31, 30, 31, 30, 31};
  if (month == 2 && is_leap_year(year)) {
    return 29;
  }
  return lengths[month - 1];
}

/// Virtual ticks as hundredths of a second, without losing the remainder
/// to integer division: the whole seconds first, then the fraction. A
/// plain `delta * 100 / pit_input_hz` would overflow a 64-bit value after
/// about 4.9 years of emulated runtime, which is not a bound worth
/// having.
constexpr std::uint64_t elapsed_centiseconds(ticks delta) noexcept {
  return (delta / pit_input_hz) * centiseconds_per_second +
         (delta % pit_input_hz) * centiseconds_per_second / pit_input_hz;
}

}  // namespace

// --- framebuffer ------------------------------------------------------

void framebuffer::set_palette_entry(unsigned index, rgb color) noexcept {
  if (index >= palette_entries) {
    return;
  }
  palette_[index] = color;
}

void framebuffer::complete(ticks at) noexcept {
  completed_at_ = at;
  ++generation_;
}

void framebuffer::save_state(state_sink& out) const {
  out.u64(generation_);
  out.u64(completed_at_);
  out.bytes(pixels_);
  for (const rgb& entry : palette_) {
    out.u8(entry.red);
    out.u8(entry.green);
    out.u8(entry.blue);
  }
}

void framebuffer::reset() noexcept {
  // Filled in place, not `pixels_ = {}`.
  //
  // Assigning a freshly value-initialized array materializes a whole
  // frame — 64,000 bytes — as a temporary and copies it over. An
  // optimizing build folds that into a memset and nothing is visible; an
  // unoptimized one puts it on the stack, and Emscripten's default stack
  // is smaller than one frame, so this trapped with "memory access out of
  // bounds" the first time anything pulled RESET in a wasm build (M3-F2,
  // #84 — until then no host had). Relying on the optimizer for stack
  // safety is not a guarantee.
  //
  // `ega::reset()` and `memory_filesystem::clear()` already avoid the
  // same trap for the same reason; this is the one place that did not.
  pixels_.fill(0);
  palette_.fill(rgb{});
  complete(0);
}

// --- audio_timeline ---------------------------------------------------

bool audio_timeline::publish(ticks at, bool level) noexcept {
  if (have_published_ && at <= last_published_) {
    return false;
  }

  // The producer owns `head_`, so a relaxed load of its own value is
  // enough; `tail_` is the consumer's and has to be acquired, because
  // seeing a stale one is what would let this overwrite a slot the
  // consumer is still reading.
  const std::uint64_t head = head_.load(std::memory_order_relaxed);
  const std::uint64_t tail = tail_.load(std::memory_order_acquire);
  if (head - tail >= edge_capacity) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  edges_[head % edge_capacity] = {.at = at, .level = level};
  // Release, and after the slot is written: this store is what publishes
  // the edge, and an acquiring consumer sees everything written before
  // it.
  head_.store(head + 1, std::memory_order_release);

  last_published_ = at;
  have_published_ = true;

  // The producer's own record of what it has published, for the state
  // serialization (state.h): FNV-1a over the tick's eight bytes and the
  // level, the same mixing the framebuffer checks use.
  ++published_;
  std::uint64_t hash = edge_digest_;
  for (unsigned i = 0; i < 8; ++i) {
    hash ^= static_cast<std::uint8_t>(at >> (8U * i));
    hash *= 1099511628211ULL;
  }
  hash ^= level ? 1U : 0U;
  hash *= 1099511628211ULL;
  edge_digest_ = hash;

  // And, if anybody asked, the edge itself. The branch is the whole cost
  // of the facility to a run that did not ask for it — the same bargain
  // `trace_ring::record()` makes, and made here rather than at the call
  // site so that "off unless asked for" is a property of this class and
  // not a rule the speaker has to remember.
  if (logging_) {
    if (log_count_ == edge_log_capacity) {
      ++log_dropped_;
    } else {
      log_[(log_first_ + log_count_) % edge_log_capacity] = {.at = at,
                                                             .level = level};
      ++log_count_;
    }
  }
  return true;
}

std::size_t audio_timeline::read_edge_log(std::span<audio_edge> out) noexcept {
  std::size_t taken = 0;
  while (taken < out.size() && log_count_ > 0) {
    out[taken] = log_[log_first_];
    log_first_ = (log_first_ + 1) % edge_log_capacity;
    --log_count_;
    ++taken;
  }
  return taken;
}

void audio_timeline::save_state(state_sink& out) const {
  out.u64(published_);
  out.u64(edge_digest_);
  out.flag(have_published_);
  out.u64(have_published_ ? last_published_ : 0);
}

void audio_timeline::advance(ticks now) noexcept {
  // Monotonic. A caller that hands back a tick it already published is
  // ignored rather than allowed to pull the horizon backwards, which
  // would look to the consumer like time running in reverse.
  if (now <= horizon_.load(std::memory_order_relaxed)) {
    return;
  }
  horizon_.store(now, std::memory_order_release);
}

void audio_timeline::restart() noexcept {
  // The horizon first, then the epoch: a consumer that reads the epoch
  // before this ran and the horizon after gets a horizon behind its
  // cursor, which is an underrun — harmless, and corrected on its next
  // call. The other order would let it read the new epoch and the old
  // horizon, and play the previous run's tail as if it were this one's.
  horizon_.store(0, std::memory_order_release);
  epoch_.fetch_add(1, std::memory_order_release);
  last_published_ = 0;
  have_published_ = false;
  published_ = 0;
  edge_digest_ = 1469598103934665603ULL;

  // The log's contents, but not the setting that fills it: what it holds
  // is in-flight traffic from the run that just ended (the same rule
  // `console_output::clear()` follows), and a host that asked to observe
  // this machine did not stop asking because the machine was reset.
  log_first_ = 0;
  log_count_ = 0;
  log_dropped_ = 0;
}

std::uint64_t audio_timeline::integrate(ticks from, ticks to,
                                        std::uint64_t head) noexcept {
  std::uint64_t high = 0;
  ticks at = from;

  // `<`, not `!=`: the contract says only one thread calls `render()`, and
  // a second one racing on `taken_` could push it past `head`. That is a
  // bug in the host either way, but with `!=` it is an emulator that
  // never returns, and with `<` it is a call that does nothing.
  while (taken_ < head) {
    const audio_edge& next = edges_[taken_ % edge_capacity];
    if (next.at >= to) {
      break;
    }
    // An edge from before this interval contributes nothing but its
    // level: it is the state the interval starts in, not a transition
    // inside it.
    const ticks when = next.at > at ? next.at : at;
    if (level_) {
      high += when - at;
    }
    at = when;
    level_ = next.level;
    ++taken_;
  }

  if (level_) {
    high += to - at;
  }
  return high;
}

void audio_timeline::skip_to(ticks at, std::uint64_t head) noexcept {
  // `<` rather than `!=`, for the reason `integrate` gives.
  while (taken_ < head) {
    const audio_edge& next = edges_[taken_ % edge_capacity];
    if (next.at > at) {
      break;
    }
    level_ = next.level;
    ++taken_;
  }
  cursor_ = at;
  cursor_remainder_ = 0;
}

void audio_timeline::restart_playback(std::uint64_t head) noexcept {
  // Everything in the ring is stamped with ticks from a clock that no
  // longer exists, so none of it can be integrated — it is dropped
  // wholesale rather than walked, which is the one case `skip_to` cannot
  // serve.
  taken_ = head;
  level_ = false;
  cursor_ = 0;
  cursor_remainder_ = 0;
}

std::size_t audio_timeline::render(std::span<float> out,
                                   unsigned sample_rate) noexcept {
  if (sample_rate < min_sample_rate || sample_rate > max_sample_rate) {
    return 0;
  }

  const std::uint64_t epoch = epoch_.load(std::memory_order_acquire);
  const ticks limit = horizon_.load(std::memory_order_acquire);
  const std::uint64_t head = head_.load(std::memory_order_acquire);

  if (epoch != seen_epoch_) {
    seen_epoch_ = epoch;
    restart_playback(head);
  }

  if (limit > cursor_ && limit - cursor_ > max_lag) {
    skip_to(limit - resync_lag, head);
    resyncs_.fetch_add(1, std::memory_order_relaxed);
  }

  // The sample interval as a whole number of ticks plus a remainder over
  // the sample rate, carried between samples. Exact, so a long pull
  // cannot drift the way repeatedly adding a rounded interval would.
  const ticks whole = pit_input_hz / sample_rate;
  const std::uint64_t fraction = pit_input_hz % sample_rate;

  // A rate change makes the carried remainder meaningless — it is in
  // units of 1/rate of a tick. Dropped rather than rescaled: it is worth
  // less than one tick, and rescaling it would be arithmetic in service
  // of nothing.
  if (remainder_scale_ != sample_rate) {
    cursor_remainder_ = 0;
    remainder_scale_ = sample_rate;
  }

  std::size_t generated = 0;
  bool underran = false;

  for (float& sample : out) {
    ticks next = cursor_ + whole;
    std::uint64_t remainder = cursor_remainder_ + fraction;
    if (remainder >= sample_rate) {
      remainder -= sample_rate;
      ++next;
    }

    if (underran || next > limit) {
      // Hold, and leave the cursor where it is. The machine has not
      // generated this audio yet; when it does, playback resumes at
      // exactly this tick and nothing is lost.
      underran = true;
      sample = level_ ? speaker_amplitude : 0.0F;
      continue;
    }

    const std::uint64_t high = integrate(cursor_, next, head);
    const auto span = static_cast<std::uint64_t>(next - cursor_);
    sample =
        speaker_amplitude * static_cast<float>(high) / static_cast<float>(span);
    cursor_ = next;
    cursor_remainder_ = remainder;
    ++generated;
  }

  if (underran) {
    underruns_.fetch_add(1, std::memory_order_relaxed);
  }

  // Publish what has been consumed, so the producer sees the ring empty
  // again. Once per call rather than per edge: the producer only reads it
  // to decide whether it is full, and it has 2048 slots of slack.
  tail_.store(taken_, std::memory_order_release);
  return generated;
}

// --- input_queue ------------------------------------------------------

bool input_queue::post(std::uint8_t scancode, key_action action,
                       ticks at) noexcept {
  if (count_ == capacity) {
    ++dropped_;
    return false;
  }

  events_[(first_ + count_) % capacity] = {
      .at = at, .scancode = scancode, .action = action};
  ++count_;
  return true;
}

bool input_queue::take(key_event& out) noexcept {
  if (count_ == 0) {
    return false;
  }

  out = events_[first_];
  first_ = (first_ + 1) % capacity;
  --count_;
  return true;
}

const key_event* input_queue::peek() const noexcept {
  return count_ == 0 ? nullptr : &events_[first_];
}

void input_queue::clear() noexcept {
  first_ = 0;
  count_ = 0;
  dropped_ = 0;
}

void input_queue::save_state(state_sink& out) const {
  out.u8(static_cast<std::uint8_t>(count_));
  for (std::size_t i = 0; i < count_; ++i) {
    const key_event& ev = events_[(first_ + i) % capacity];
    out.u64(ev.at);
    out.u8(ev.scancode);
    out.u8(static_cast<std::uint8_t>(ev.action));
  }
  out.u64(dropped_);
}

// --- wall_clock -------------------------------------------------------

bool wall_clock::set(const wall_time& when, ticks now) noexcept {
  if (when.year < min_year || when.year > max_year) {
    return false;
  }
  if (when.month < 1 || when.month > 12) {
    return false;
  }
  if (when.day < 1 || when.day > days_in_month(when.year, when.month)) {
    return false;
  }
  if (when.hour > 23 || when.minute > 59 || when.second > 59 ||
      when.centisecond > 99) {
    return false;
  }

  const std::int64_t days =
      days_from_civil(when.year, when.month, when.day) - dos_epoch_days;

  base_centiseconds_ = static_cast<std::uint64_t>(days) * centiseconds_per_day +
                       when.hour * centiseconds_per_hour +
                       when.minute * centiseconds_per_minute +
                       when.second * centiseconds_per_second + when.centisecond;
  base_tick_ = now;
  seeded_ = true;
  return true;
}

wall_time wall_clock::at(ticks now) const noexcept {
  // A tick before the base cannot happen through the machine — the clock
  // only moves forward and `rebase()` follows it — but the guard is one
  // comparison and the alternative is an unsigned subtraction that wraps
  // into the year 5.8 billion.
  const ticks delta = now >= base_tick_ ? now - base_tick_ : 0;
  const std::uint64_t total = base_centiseconds_ + elapsed_centiseconds(delta);

  const std::uint64_t day_number = total / centiseconds_per_day;
  std::uint64_t rest = total % centiseconds_per_day;

  const std::int64_t absolute =
      static_cast<std::int64_t>(day_number) + dos_epoch_days;
  const civil_date date = civil_from_days(absolute);

  wall_time out{};
  out.year = static_cast<std::uint16_t>(date.year);
  out.month = static_cast<std::uint8_t>(date.month);
  out.day = static_cast<std::uint8_t>(date.day);
  // 1970-01-01 was a Thursday, which is 4 with Sunday at 0 — the
  // numbering DOS 2Ah reports in AL.
  out.weekday = static_cast<std::uint8_t>((absolute + 4) % 7);

  out.hour = static_cast<std::uint8_t>(rest / centiseconds_per_hour);
  rest %= centiseconds_per_hour;
  out.minute = static_cast<std::uint8_t>(rest / centiseconds_per_minute);
  rest %= centiseconds_per_minute;
  out.second = static_cast<std::uint8_t>(rest / centiseconds_per_second);
  out.centisecond = static_cast<std::uint8_t>(rest % centiseconds_per_second);
  return out;
}

void wall_clock::rebase(ticks from) noexcept {
  const ticks delta = from >= base_tick_ ? from - base_tick_ : 0;
  base_centiseconds_ += elapsed_centiseconds(delta);
  base_tick_ = 0;
}

void wall_clock::save_state(state_sink& out) const {
  out.u64(base_centiseconds_);
  out.u64(base_tick_);
  out.flag(seeded_);
}

// --- console_output ---------------------------------------------------

void console_output::put(std::uint8_t byte) noexcept {
  if (count_ == capacity) {
    ++dropped_;
    return;
  }
  bytes_[(first_ + count_) % capacity] = byte;
  ++count_;
}

void console_output::write(std::span<const std::uint8_t> bytes) noexcept {
  for (const std::uint8_t byte : bytes) {
    put(byte);
  }
}

std::size_t console_output::read(std::span<std::uint8_t> out) noexcept {
  std::size_t taken = 0;
  while (taken < out.size() && count_ > 0) {
    out[taken] = bytes_[first_];
    first_ = (first_ + 1) % capacity;
    --count_;
    ++taken;
  }
  return taken;
}

void console_output::clear() noexcept {
  first_ = 0;
  count_ = 0;
  dropped_ = 0;
}

void console_output::save_state(state_sink& out) const {
  out.u16(static_cast<std::uint16_t>(count_));
  for (std::size_t i = 0; i < count_; ++i) {
    out.u8(bytes_[(first_ + i) % capacity]);
  }
  out.u64(dropped_);
}

}  // namespace amberfolio::machine
