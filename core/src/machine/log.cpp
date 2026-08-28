// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/log.h"

#include <array>
#include <cstddef>
#include <span>

#include "amberfolio/cpu/diagnostics.h"
#include "amberfolio/machine/diagnostics.h"
#include "amberfolio/machine/report.h"
#include "amberfolio/machine/seam.h"

namespace amberfolio::machine {

template <typename T>
void diagnostic_log::keep(const T& record) noexcept {
  std::array<char, diagnostic_line_capacity> line{};
  const std::size_t length = format_diagnostic(record, line);
  if (length == 0) {
    // The one record with no line: a program exiting (report.h).
    return;
  }
  if (count_ + length > capacity) {
    // Whole or absent, and counted either way — see log.h.
    ++dropped_;
    return;
  }
  for (std::size_t i = 0; i < length; ++i) {
    chars_[(first_ + count_ + i) % capacity] = line[i];
  }
  count_ += length;
}

std::size_t diagnostic_log::read(std::span<char> out) noexcept {
  const std::size_t taken = out.size() < count_ ? out.size() : count_;
  for (std::size_t i = 0; i < taken; ++i) {
    out[i] = chars_[(first_ + i) % capacity];
  }
  first_ = (first_ + taken) % capacity;
  count_ -= taken;
  return taken;
}

void diagnostic_log::clear() noexcept {
  first_ = 0;
  count_ = 0;
  dropped_ = 0;
}

/// Hand a record to the second sink, if there is one. Before `keep()`
/// rather than after, so that a consumer sees a record even when the ring
/// had no room for its line.
template <typename T>
void diagnostic_log::relay_to(const T& record) noexcept {
  if (relay_ != nullptr) {
    relay_->report(record);
  }
}

void diagnostic_log::report(const notice& what) {
  relay_to(what);
  keep(what);
}

// The two streams `set_tracing` gates are relayed **before** the gate,
// and deliberately. Tracing is about whether a reader wants the lines;
// a second sink is a consumer of the records, and a consumer that only
// worked under `--trace` would be a feature that turned itself off when
// nobody was watching.
void diagnostic_log::report(const service_call& call) {
  relay_to(call);
  if (!tracing_) {
    return;
  }
  keep(call);
}

void diagnostic_log::report(const file_event& event) {
  relay_to(event);
  if (!tracing_) {
    return;
  }
  keep(event);
}

void diagnostic_log::report(const stop_record& stop) {
  relay_to(stop);
  keep(stop);
}

void diagnostic_log::report(const cpu::stop_record& stop) {
  relay_to(stop);
  keep(stop);
}

void diagnostic_log::report(const device_stop& stop) {
  relay_to(stop);
  keep(stop);
}

void diagnostic_log::report(const seam_event& event) {
  relay_to(event);
  keep(event);
}

}  // namespace amberfolio::machine
