// SPDX-License-Identifier: AGPL-3.0-only
//
// The M2-H2 (#55) dev page's demo program: a small, self-written 8086
// binary that exercises the whole reference device set
// (core/src/abi.cpp's `reference_devices`, attached through
// `af_machine_attach_reference_devices()`) in one pass — video, sound and
// the keyboard — so the page (and the CI smoke check) can prove frame,
// audio and input all cross the wasm boundary.
//
// See demo_program.cpp for the assembly listing and the encoding. This
// header only exposes the finished bytes.
//
//
// Where #56's composite program slots in
// -----------------------------------------
//
// #56 (M2-T1) owns the canonical program suite, including a *composite*
// program described as "the demo the dev page embeds." That program does
// not exist in this tree yet — #56 is being implemented in parallel and
// may land after this issue does. When it lands, `demo_program_bytes()`
// below is the one function that should start returning its bytes
// instead of this file's own: nothing else in hosts/web needs to change,
// because everything downstream (the ABI calls that place it, the smoke
// check's assertions) only depends on "a program that sets mode 0Dh,
// draws something, plays a tone, and echoes a key" — not on which
// self-written program does it. Swapping the body of this function for a
// call into #56's suite, and updating the framebuffer hash and console
// bytes the smoke test expects to whatever the composite program
// actually produces, is the whole migration.

#pragma once

#include <cstdint>
#include <vector>

namespace amberfolio::web {

/// Assemble the demo program and answer its bytes. Deterministic and
/// self-written (CONTRIBUTING.md's clean-content rule) — never fetched,
/// never derived from anything but the encoding comments in
/// demo_program.cpp.
[[nodiscard]] std::vector<std::uint8_t> demo_program_bytes();

/// Where `af_machine_write_memory()` should place the program, and the
/// CS:IP/SS:SP `af_machine_set_entry()` should start it at. 1000:0000 —
/// the same convention hosts/web/tests/smoke.mjs's own boundary checks
/// and tests/core/abi_test.cpp already use for a placed program, so a
/// reader who has seen one has seen the other.
inline constexpr std::uint32_t demo_program_address = 0x10000;
inline constexpr std::uint16_t demo_program_cs = 0x1000;
inline constexpr std::uint16_t demo_program_ip = 0x0000;
inline constexpr std::uint16_t demo_program_ss = 0x1000;
inline constexpr std::uint16_t demo_program_sp = 0xFFFE;

}  // namespace amberfolio::web
