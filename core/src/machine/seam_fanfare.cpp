// SPDX-License-Identifier: AGPL-3.0-only
//
// The fanfare seam: a short piece on the Tandy sound chip when the
// game's title comes up (#538).
//
// **What a player meets.** With it off, the boot is silent from the
// publisher's logo to the first sound of the game: the title, the
// credits and the menus make none. With it on, a fanfare of about
// thirty-one seconds starts as the title picture goes up and plays on
// through the credits and into the menus, in virtual time, so it keeps
// its tempo at every governor. A key that hurries the title along does
// not stop it. The first sound the game makes does: the game's own sound
// always wins, and the fanfare gives way for good. It plays only when the
// game is sounding through the Tandy chip, which every copy is started
// with here; the piece is written for that chip and nothing else.
//
// **The piece is this project's own.** It was written for this seam, as a
// list of writes to the chip's port (fanfare_score.cpp); nothing in it
// comes from the program, which plays no music at its title.
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of this is addresses and offsets (CONTRIBUTING.md). Nothing of the
// program is reproduced here.
//
// **One routine shows the titles.** Overlay 1 (GAME.OVR file offset 212,
// `0xD4`, 2062 bytes; the overlay manager keeps its segment in the word at
// image offset `0x1A0`) at `0x0627`: the publisher's logo and a wait of
// five seconds or a key, then, when the configuration asks for the full
// titles (every copy's does), the title picture, five more seconds or a
// key, and the credits, ten more. The title picture is up once the far
// call returning to `0x06FD` has freed it, and the far call *at* `0x06FD`
// reads the clock the title's five seconds are counted from. That call is
// the point: reached once a boot, with DS the data segment.
//
// **One routine starts every sound.** The driver's dispatch entry, image
// offset `0x8BBC` (seam_quick_steps.cpp has its frame). The program
// writes the chip through the driver and only through it: at start-up the
// driver silences the chip, and after that it writes it only in answer to
// a request, which comes through this entry. Between the two, which is
// the whole of the title, the credits and the menus, nothing writes the
// chip.
//
// **The sound setting is a byte.** DGROUP `0x5372` (the data segment is
// the image's paragraph `0xC7C` on): 0 the Tandy chip, 1 the speaker, 2
// none. The configuration file sets it at start-up and Ctrl-S moves it
// in play.
//
// **The chip as the driver's start-up leaves it**: every voice silent,
// every period and the noise control zero, the last register named the
// noise voice's attenuation. Measured: the only writes before the title
// are the start-up's silencing of the four voices, done twice in the
// boot's first eighth of a second, over a chip that came up with zeros
// in its periods and its noise control (psg.h).
//
//
// What the seam does
// ------------------
//
// At the title, it asks the engine for a moment, now; at each moment it
// writes every cue of the score that is due, through the port as the
// program would (`machine::write_port8`), and asks for the moment of the
// next one. The engine hands each arrival the moment it asked for, so the
// piece is paced from where it started and never drifts. The point at a
// moment has no address because none would do: the main menu waits for a
// key inside the BIOS, where no address of the program's is reached.
//
// It stops, and puts the chip back as it found it, at the end of the
// score; at a moment that finds the setting is not the Tandy chip; and at
// the dispatch entry, before the program's first request gets to the
// driver, so the program's writes never meet the seam's.
//
// It refuses a title point whose DS is not the data segment or whose
// instruction is not a far call.
//
// **What it costs.** Before the title it moves nothing. From the title the
// chip's registers are the score's until the score ends or gives way, and
// then they are what they were; the writes themselves are in the run's
// audio record for good. `fired` counts the title, every moment, and every
// sound the program starts while the seam is on.
//
//
// What it is not yet (docs/seams.md §8.5)
// ---------------------------------------
//
// Nothing outstanding.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/tandy_sound.h"
#include "fanfare_score.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

/// The SHA-256 of the program image these offsets are facts about: the
/// baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> fanfare_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

/// Overlay 1, the titles': file offset 212 (`0xD4`), 2062 bytes, and the
/// word the overlay manager keeps its segment in.
constexpr seam_module titles_module{
    .file = "GAME.OVR",
    .file_offset = 212,
    .length = 2062,
    .digest =
        "b6ca55979d60ca53a0e0063c4929a61ca8c5f21843f77fd390598ee29fc4967a",
    .load_segment_at = 0x1A0};

/// In it: the far call that reads the clock for the title's five seconds,
/// with the title picture up.
constexpr std::uint32_t title_offset = 0x06FD;
constexpr std::uint8_t far_call = 0x9A;

/// The driver's dispatch entry, from the image segment.
constexpr std::uint32_t dispatch_offset = 0x8BBC;

/// The data segment is the image's paragraph `0xC7C` on; the sound setting
/// is a byte in it, 0 for the Tandy chip.
constexpr std::uint16_t dgroup_paragraphs = 0xC7C;
constexpr std::uint16_t data_sound = 0x5372;
constexpr std::uint8_t sound_on_the_chip = 0;

/// The seam's one word: the next cue of the score.
constexpr unsigned next_cue = 0;

/// What puts the chip back as the driver's start-up left it: each tone
/// voice's period and the noise control to zero, every voice silent, and
/// the noise voice's attenuation named last.
constexpr std::array<std::uint8_t, 11> as_found{
    0x80, 0x00, 0xA0, 0x00, 0xC0, 0x00, 0xE0, 0x9F, 0xBF, 0xDF, 0xFF};

[[nodiscard]] std::uint16_t data_segment(const seam_context& ctx) {
  return static_cast<std::uint16_t>((ctx.image_base() >> 4U) +
                                    dgroup_paragraphs);
}

[[nodiscard]] bool sounding_on_the_chip(machine& box, const seam_context& ctx) {
  return box.processor().read_byte(data_segment(ctx), data_sound) ==
         sound_on_the_chip;
}

void hand_the_chip_back(machine& box) {
  for (const std::uint8_t value : as_found) {
    box.write_port8(tandy_sound_port, value);
  }
}

void start_at_the_title(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const cpu::registers& regs = cpu.regs();
  if (regs[cpu::sreg::ds] != data_segment(ctx) ||
      cpu.read_byte(regs[cpu::sreg::cs], regs.ip) != far_call) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (ctx.offered() || !sounding_on_the_chip(box, ctx)) {
    return;
  }
  ctx.set_scratch(next_cue, 0);
  ctx.offer_at(box.time());
}

void play_what_is_due(machine& box, seam_context& ctx) {
  const std::span<const chip_cue> score = fanfare_score();
  std::size_t next = ctx.scratch(next_cue);
  if (next >= score.size() || !sounding_on_the_chip(box, ctx)) {
    hand_the_chip_back(box);
    return;
  }
  // Where the piece started, from the moment this arrival was asked for:
  // the cue's own, so a late arrival plays everything it missed and the
  // next moment is still the score's.
  const ticks start = ctx.moment() - score[next].at;
  while (next < score.size() && start + score[next].at <= box.time()) {
    box.write_port8(tandy_sound_port, score[next].value);
    ++next;
  }
  if (next == score.size()) {
    hand_the_chip_back(box);
    return;
  }
  ctx.set_scratch(next_cue, static_cast<std::uint16_t>(next));
  ctx.offer_at(start + score[next].at);
}

void give_way(machine& box, seam_context& ctx) {
  if (!ctx.offered()) {
    return;
  }
  ctx.withdraw_offer();
  hand_the_chip_back(box);
}

constexpr std::array<seam_point, 3> fanfare_points{
    {{.module = titles_module,
      .offset = title_offset,
      .run = &start_at_the_title},
     {.module = resident_image, .run = &play_what_is_due, .at_moment = true},
     {.module = resident_image, .offset = dispatch_offset, .run = &give_way}}};

constexpr seam_definition fanfare_definition{
    .id = "fanfare",
    .about = "a fanfare on the Tandy chip when the title comes up",
    .fingerprints = fanfare_binaries,
    .points = fanfare_points,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& fanfare_seam() noexcept { return fanfare_definition; }

}  // namespace amberfolio::machine
