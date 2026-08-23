// SPDX-License-Identifier: AGPL-3.0-only
//
// The replay harness (replay.h, M4-R1 #100): the grammar round-trips, a
// recording made of one run verifies against another run of the same
// program, and a recording that does not match says what differed —
// which line, which tick, which section.
//
// The program is this file's own: a loop that reads keys and counts them
// into memory, so a key delivered at a different tick is a different
// state. Every byte here is this file's own (PLAN.md §6).

#include "amberfolio/machine/replay.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/dos.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/int10.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_vfs.h"
#include "amberfolio/machine/pic.h"
#include "amberfolio/machine/pit.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/renderer.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/speaker.h"
#include "amberfolio/machine/state.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

[[nodiscard]] std::string line_of(const replay_event& event) {
  std::array<char, replay_max_line> out{};
  const std::size_t n = format_replay_line(event, out);
  return {out.data(), n};
}

[[nodiscard]] bool parse(std::string_view text, replay_event& out) {
  return parse_replay_line(std::span<const char>(text.data(), text.size()),
                           out);
}

// --- The grammar ---------------------------------------------------------

TEST(ReplayGrammar, RoundTripsEveryKindOfLine) {
  replay_event e{};
  e.kind = replay_line::header;
  e.format_version = recording_format_version;
  e.state_version = state_format_version;
  EXPECT_EQ(line_of(e), "amberfolio-recording 2 state=1\n");

  e = replay_event{};
  e.kind = replay_line::key;
  e.at = 123456;
  e.scancode = 0x1E;
  e.action = key_action::up;
  EXPECT_EQ(line_of(e), "key 123456 1e up\n");
  replay_event back{};
  ASSERT_TRUE(parse("key 123456 1e up", back));
  EXPECT_EQ(back.kind, replay_line::key);
  EXPECT_EQ(back.at, 123456u);
  EXPECT_EQ(back.scancode, 0x1E);
  EXPECT_EQ(back.action, key_action::up);

  e = replay_event{};
  e.kind = replay_line::wall;
  e.at = 7;
  e.when = {.year = 1986,
            .month = 6,
            .day = 17,
            .hour = 12,
            .minute = 30,
            .second = 5,
            .centisecond = 9};
  EXPECT_EQ(line_of(e), "wall 7 1986-06-17 12:30:05.09\n");
  ASSERT_TRUE(parse("wall 7 1986-06-17 12:30:05.09", back));
  EXPECT_EQ(back.when.year, 1986);
  EXPECT_EQ(back.when.centisecond, 9);

  e = replay_event{};
  e.kind = replay_line::tail;
  const std::string_view tail = " -X";
  for (std::size_t i = 0; i < tail.size(); ++i) {
    e.tail[i] = tail[i];
  }
  e.tail_length = tail.size();
  EXPECT_EQ(line_of(e), "tail 202d58\n");
  ASSERT_TRUE(parse("tail 202d58", back));
  EXPECT_EQ(std::string_view(back.tail_text().data(), back.tail_length), tail);
  ASSERT_TRUE(parse("tail", back))
      << "an empty tail is a line with nothing after it";
  EXPECT_EQ(back.tail_length, 0u);

  e = replay_event{};
  e.kind = replay_line::speed;
  e.subticks = 1024;
  EXPECT_EQ(line_of(e), "speed 1024\n");

  e = replay_event{};
  e.kind = replay_line::seam;
  const std::string_view id = "cheat-kill-all";
  for (std::size_t i = 0; i < id.size(); ++i) {
    e.id[i] = id[i];
  }
  e.id_length = id.size();
  EXPECT_EQ(line_of(e), "seam cheat-kill-all\n");
  ASSERT_TRUE(parse("seam cheat-kill-all", back));
  EXPECT_EQ(back.id_text(), id);

  e = replay_event{};
  e.kind = replay_line::end;
  e.at = 99;
  e.steps = 12;
  EXPECT_EQ(line_of(e), "end 99 12\n");
  ASSERT_TRUE(parse("end 99 12", back));
  EXPECT_EQ(back.steps, 12u);
}

TEST(ReplayGrammar, RoundTripsACheckpointWithSections) {
  replay_event e{};
  e.kind = replay_line::checkpoint;
  e.at = 1193182;
  e.steps = 298295;
  for (std::size_t i = 0; i < e.digest.bytes.size(); ++i) {
    e.digest.bytes[i] = static_cast<std::uint8_t>(i);
  }
  e.have_sections = true;
  for (std::size_t i = 0; i < state_section_count; ++i) {
    e.sections[i] = 0x1111111111111111ULL * (i + 1);
  }
  const std::string text = line_of(e);
  EXPECT_TRUE(text.starts_with("checkpoint 1193182 298295 000102030405"));
  EXPECT_NE(text.find(" clock=1111111111111111"), std::string::npos);
  EXPECT_NE(text.find(" stop=dddddddddddddddd"), std::string::npos);

  replay_event back{};
  ASSERT_TRUE(parse(text, back));
  EXPECT_EQ(back.kind, replay_line::checkpoint);
  EXPECT_EQ(back.at, e.at);
  EXPECT_EQ(back.steps, e.steps);
  EXPECT_EQ(back.digest, e.digest);
  EXPECT_TRUE(back.have_sections);
  EXPECT_EQ(back.sections, e.sections);

  // And without sections, which a short line is.
  e.have_sections = false;
  ASSERT_TRUE(parse(line_of(e), back));
  EXPECT_FALSE(back.have_sections);
}

TEST(ReplayGrammar, RoundTripsACheckpointOfAStoppedMachine) {
  replay_event e{};
  e.kind = replay_line::checkpoint;
  e.at = 4096;
  e.steps = 1024;
  e.digest = sha256(std::span<const std::uint8_t>{});
  e.stopped = true;

  const std::string text = line_of(e);
  EXPECT_NE(text.find(" stopped\n"), std::string::npos) << text;

  replay_event back{};
  ASSERT_TRUE(parse(text, back));
  EXPECT_EQ(back.kind, replay_line::checkpoint);
  EXPECT_EQ(back.at, 4096u);
  EXPECT_TRUE(back.stopped);
  EXPECT_FALSE(back.have_sections);

  // And beside the sections, which a recorder writes together.
  e.have_sections = true;
  for (std::size_t i = 0; i < state_section_count; ++i) {
    e.sections[i] = i + 1;
  }
  replay_event both{};
  ASSERT_TRUE(parse(line_of(e), both));
  EXPECT_TRUE(both.stopped);
  EXPECT_TRUE(both.have_sections);
  EXPECT_EQ(both.sections[0], 1u);

  // A checkpoint of a running machine does not carry it, and one that
  // carries it twice is not a line.
  e.stopped = false;
  EXPECT_EQ(line_of(e).find(" stopped"), std::string::npos);
  replay_event twice{};
  EXPECT_FALSE(parse(
      "checkpoint 1 2 " + std::string(64, '0') + " stopped stopped\n", twice));
}

// The manifest's own grammar (#155): a `\`-joined path, the spelling
// `tests/sessions/*.session` already uses, bounded by `dos_path` at both
// ends — eight components of a legal DOS short name, and nothing else is
// a line.
TEST(ReplayGrammar, RoundTripsAManifestPath) {
  replay_event back{};
  ASSERT_TRUE(parse("dir SAVE", back));
  EXPECT_EQ(back.kind, replay_line::dir);
  EXPECT_EQ(back.path.depth(), 1u);
  EXPECT_EQ(line_of(back), "dir SAVE\n");

  const std::string digest(64, 'b');
  ASSERT_TRUE(parse("file SAVE\\CHARLIST.TXT 285 " + digest, back));
  EXPECT_EQ(back.kind, replay_line::file);
  EXPECT_EQ(back.path.depth(), 2u);
  EXPECT_EQ(back.size, 285u);
  EXPECT_EQ(line_of(back), "file SAVE\\CHARLIST.TXT 285 " + digest + "\n");

  // `dos_path::max_depth` components is a path; one more is not, and is
  // refused rather than truncated into a different, real location.
  std::string deep = "A";
  for (std::size_t i = 1; i < dos_path::max_depth; ++i) {
    deep += "\\A";
  }
  ASSERT_TRUE(parse("dir " + deep, back));
  EXPECT_EQ(back.path.depth(), dos_path::max_depth);
  EXPECT_FALSE(parse("dir " + deep + "\\A", back));

  // A manifest path is canonical already: relative to the root, no
  // separator at either end, no empty component, no drive.
  EXPECT_FALSE(parse("dir \\SAVE", back)) << "no leading separator";
  EXPECT_FALSE(parse("dir SAVE\\", back)) << "no trailing separator";
  EXPECT_FALSE(parse("dir SAVE\\\\A", back)) << "no empty component";
  EXPECT_FALSE(parse("dir C:\\SAVE", back)) << "no drive letter";
  EXPECT_FALSE(parse("dir", back)) << "the root is not an entry";
  EXPECT_FALSE(parse("dir SAVE extra", back));
  EXPECT_FALSE(parse("file SAVE\\A.DAT 1", back)) << "a file has a digest";
}

TEST(ReplayGrammar, RefusesWhatIsNotALine) {
  replay_event back{};
  EXPECT_FALSE(parse("keys 1 1e down", back)) << "not a kind";
  EXPECT_FALSE(parse("key 1 1e sideways", back));
  EXPECT_FALSE(parse("key 1 zz down", back));
  EXPECT_FALSE(parse("key 1 1e down extra", back));
  EXPECT_FALSE(parse("checkpoint 1 2 notahash", back));
  EXPECT_TRUE(parse("wall 1 1986-13-01 00:00:00.00", back))
      << "the shape is fine; month 13 is the machine's to refuse when the "
         "seed is applied";
  EXPECT_FALSE(parse("speed 0", back));
  EXPECT_FALSE(parse("file NAME.TOOLONG 1 " + std::string(64, 'a'), back));
  // Comments and blank lines are nothing, and well-formed.
  ASSERT_TRUE(parse("# a remark", back));
  EXPECT_EQ(back.kind, replay_line::nothing);
  ASSERT_TRUE(parse("", back));
  EXPECT_EQ(back.kind, replay_line::nothing);
  ASSERT_TRUE(parse("key 5 1e down\r\n", back)) << "a CRLF line ends the same";
}

// --- A recording of a run, verified against another -----------------------

/// The whole reference device set, a memory filesystem and the DOS layer
/// — the shape machine_harness builds — with the one program every test
/// here runs: poll for keys forever, counting the polls, and count each
/// key and its scan code into the result block. The poll count is what
/// makes *when* a key arrived part of the state.
///
///         push cs / pop ds
///         xor  bx, bx                 ; keys
///         xor  cx, cx                 ; polls
/// next:   mov  ah, 01h / int 16h      ; anything waiting?
///         jnz  got
///         inc  cx
///         jmp  next
/// got:    mov  ah, 00h / int 16h
///         inc  bx
///         mov  [0800h], bx            ; how many
///         add  [0802h], ax            ; what, summed
///         mov  [0804h], cx            ; how long it took
///         jmp  next
struct rig {
  rig()
      : box(std::make_unique<machine>(memory_layout::pc)),
        fs(std::make_unique<memory_filesystem>()),
        irq(std::make_unique<pic::controller>(*box)),
        timer(std::make_unique<pit>(*box, *irq)),
        sound(std::make_unique<speaker>(*box, *timer)),
        video(std::make_unique<ega>(*box)),
        screen(std::make_unique<renderer>(*box, *video)) {
    box->attach(*irq);
    box->attach(*timer);
    box->attach(*sound);
    box->attach(*video);
    box->schedule(timer->channel0_deadline());
    box->schedule(timer->channel2_deadline());
    box->schedule(*sound);
    box->schedule(*screen);
    box->set_filesystem(*fs);
    install_int10(box->services());
    install_dos_services(box->services());
    box->reset();
    screen->reset();
    box->set_step_cost(1);

    // The program, as a file the loader places and the recording names.
    const std::array<std::uint8_t, 34> image{
        0x0E, 0x1F,              // push cs / pop ds
        0x31, 0xDB,              // xor bx, bx
        0x31, 0xC9,              // xor cx, cx
        0xB4, 0x01, 0xCD, 0x16,  // next: mov ah, 1 / int 16h
        0x75, 0x03,              // jnz got
        0x41,                    // inc cx
        0xEB, 0xF7,              // jmp next
        0xB4, 0x00, 0xCD, 0x16,  // got: mov ah, 0 / int 16h
        0x43,                    // inc bx
        0x89, 0x1E, 0x00, 0x08,  // mov [0800h], bx
        0x01, 0x06, 0x02, 0x08,  // add [0802h], ax
        0x89, 0x0E, 0x04, 0x08,  // mov [0804h], cx
        0xEB, 0xE4};             // jmp next
    std::vector<std::uint8_t> file(32, 0);
    const auto put16 = [&file](std::size_t at, std::uint16_t v) {
      file[at] = static_cast<std::uint8_t>(v);
      file[at + 1] = static_cast<std::uint8_t>(v >> 8);
    };
    file[0] = 'M';
    file[1] = 'Z';
    put16(2, static_cast<std::uint16_t>(32 + image.size()));
    put16(4, 1);
    put16(8, 2);
    put16(10, 0x0100);
    put16(12, 0xFFFF);
    put16(16, 0x0F00);
    put16(24, 0x001C);
    file.insert(file.end(), image.begin(), image.end());
    stage("\\KEYS.EXE", file);
    stage("\\DATA.BIN", std::vector<std::uint8_t>{1, 2, 3});

    // And one that ends: spin out a few thousand ticks so there is a run
    // to record, then exit. A machine that stops is the case the tick
    // alone cannot describe — stopping happens inside a step and spends
    // neither the step nor its ticks — so it needs a program of its own.
    //
    //         mov  cx, 0400h
    //  spin:  loop spin
    //         mov  ax, 4C07h / int 21h
    const std::array<std::uint8_t, 10> ending{
        0xB9, 0x00, 0x04,  // mov cx, 0400h
        0xE2, 0xFE,        // spin: loop spin
        0xB8, 0x07, 0x4C,  // mov ax, 4C07h
        0xCD, 0x21};       // int 21h
    std::vector<std::uint8_t> second(32, 0);
    const auto put16b = [&second](std::size_t at, std::uint16_t v) {
      second[at] = static_cast<std::uint8_t>(v);
      second[at + 1] = static_cast<std::uint8_t>(v >> 8);
    };
    second[0] = 'M';
    second[1] = 'Z';
    put16b(2, static_cast<std::uint16_t>(32 + ending.size()));
    put16b(4, 1);
    put16b(8, 2);
    put16b(10, 0x0100);
    put16b(12, 0xFFFF);
    put16b(16, 0x0F00);
    put16b(24, 0x001C);
    second.insert(second.end(), ending.begin(), ending.end());
    stage("\\ENDS.EXE", second);
  }

  /// A directory on the disk. `\SAVE\` is where the game keeps its saves
  /// and the reason #155 exists; here it is any subdirectory at all.
  void make_dir(std::string_view raw) const {
    const auto path =
        canonicalize(dos_path{}, std::span<const char>(raw.data(), raw.size()));
    ASSERT_TRUE(path.ok());
    ASSERT_EQ(fs->mkdir(path.value), vfs_error::none);
  }

  void stage(std::string_view raw,
             const std::vector<std::uint8_t>& bytes) const {
    const auto path =
        canonicalize(dos_path{}, std::span<const char>(raw.data(), raw.size()));
    ASSERT_TRUE(path.ok());
    const auto made = fs->create(path.value);
    ASSERT_TRUE(made.ok());
    const auto wrote = fs->write(made.value, bytes);
    ASSERT_TRUE(wrote.ok());
    ASSERT_EQ(fs->close(made.value), vfs_error::none);
  }

  /// Load a program and identify it, the way a host does.
  void start(std::string_view raw = "\\KEYS.EXE") const {
    const auto path =
        canonicalize(dos_path{}, std::span<const char>(raw.data(), raw.size()));
    ASSERT_TRUE(path.ok());
    const auto loaded = load_program(*box, *fs, path.value, {});
    ASSERT_TRUE(loaded.ok());
    ASSERT_TRUE(
        box->seams().identify(*fs, path.value, loaded.value.load_segment));
  }

  [[nodiscard]] std::uint16_t result(std::size_t index) const {
    const std::uint32_t at = cpu::physical_address(
        image_load_segment, static_cast<std::uint16_t>(0x0800 + 2 * index));
    return static_cast<std::uint16_t>(box->memory().ram()[at] |
                                      (box->memory().ram()[at + 1] << 8));
  }

  std::unique_ptr<machine> box;
  std::unique_ptr<memory_filesystem> fs;
  std::unique_ptr<pic::controller> irq;
  std::unique_ptr<pit> timer;
  std::unique_ptr<speaker> sound;
  std::unique_ptr<ega> video;
  std::unique_ptr<renderer> screen;
};

/// A host's recording loop over `r`: the preamble, then keys at the ticks
/// given, a checkpoint every `every` ticks, and the end at `until`.
[[nodiscard]] std::string record(
    const rig& r, const std::vector<std::pair<ticks, std::uint8_t>>& keys,
    ticks every, ticks until) {
  std::string text;
  std::vector<char> buffer(replay_preamble_capacity);
  const std::size_t n = write_preamble(*r.box, *r.fs, "KEYS.EXE", {}, buffer);
  EXPECT_GT(n, 0u);
  text.append(buffer.data(), n);

  std::array<char, replay_max_line> line{};
  const auto emit = [&](const replay_event& e) {
    const std::size_t m = format_replay_line(e, line);
    EXPECT_GT(m, 0u);
    text.append(line.data(), m);
  };

  std::size_t next_key = 0;
  ticks next_checkpoint = every;
  while (r.box->time() < until) {
    ticks target = until;
    if (next_checkpoint < target) {
      target = next_checkpoint;
    }
    if (next_key < keys.size() && keys[next_key].first < target) {
      target = keys[next_key].first;
    }
    r.box->run(target);
    const ticks now = r.box->time();
    while (next_key < keys.size() && keys[next_key].first <= now) {
      replay_event e{};
      e.kind = replay_line::key;
      e.at = now;
      e.scancode = keys[next_key].second;
      e.action = key_action::down;
      r.box->post_key(e.scancode, e.action);
      emit(e);
      ++next_key;
    }
    if (now >= next_checkpoint) {
      emit(checkpoint_of(*r.box));
      next_checkpoint += every;
    }
  }
  replay_event e{};
  e.kind = replay_line::end;
  e.at = r.box->time();
  e.steps = r.box->steps();
  emit(e);
  return text;
}

/// A host's recording loop over a run that ends by itself: a frame at a
/// time to the machine's own boundary, a checkpoint after each, until the
/// machine stops. This is the desktop host's loop, minus everything that
/// is not the recording.
[[nodiscard]] std::string record_to_stop(const rig& r,
                                         std::string_view program) {
  std::string text;
  std::vector<char> buffer(replay_preamble_capacity);
  const std::size_t n = write_preamble(*r.box, *r.fs, program, {}, buffer);
  EXPECT_GT(n, 0u);
  text.append(buffer.data(), n);

  std::array<char, replay_max_line> line{};
  const auto emit = [&](const replay_event& e) {
    const std::size_t m = format_replay_line(e, line);
    EXPECT_GT(m, 0u);
    text.append(line.data(), m);
  };

  // A cap, so that a program which does not stop fails this test rather
  // than hanging it.
  for (int frame = 0; frame < 64 && !r.box->stopped(); ++frame) {
    r.box->run(r.box->time() + renderer::frame_period);
    emit(checkpoint_of(*r.box));
  }
  EXPECT_TRUE(r.box->stopped()) << "the program was supposed to end";

  replay_event e{};
  e.kind = replay_line::end;
  e.at = r.box->time();
  e.steps = r.box->steps();
  emit(e);
  return text;
}

/// Every manifest line of a recording, as `KIND PATH` — the two fields
/// that say *what the disk is*, without the digests that would make the
/// expectation a wall of hex. The order is the assertion.
[[nodiscard]] std::vector<std::string> manifest_of(const std::string& text) {
  std::vector<std::string> out;
  std::size_t at = 0;
  while (at < text.size()) {
    const std::size_t end = text.find('\n', at);
    const std::string line =
        text.substr(at, end == std::string::npos ? end : end - at);
    at = end == std::string::npos ? text.size() : end + 1;
    if (!line.starts_with("file ") && !line.starts_with("dir ")) {
      continue;
    }
    const std::size_t first = line.find(' ');
    const std::size_t second = line.find(' ', first + 1);
    out.push_back(line.substr(0, second));
  }
  return out;
}

/// `digest`, as a manifest line spells it.
[[nodiscard]] std::string hex_of(const sha256_digest& digest) {
  std::array<char, sha256_digest::text_length + 1> out{};
  const std::size_t n = format_hex(digest, out);
  return {out.data(), n};
}

/// The report a player wrote, as a string.
[[nodiscard]] std::string report_of(const replay_player& player) {
  std::array<char, replay_report_capacity> out{};
  static_cast<void>(player.report(out));
  return {out.data()};
}

/// Rewrite a recording's manifest, keeping everything else. The one way
/// to make a text a *recorder* would not write, which is what a
/// compatibility test and a refusal test both need.
[[nodiscard]] std::string with_manifest(
    const std::string& text, std::uint32_t version,
    const std::vector<std::string>& manifest) {
  std::string out;
  bool written = false;
  std::size_t at = 0;
  while (at < text.size()) {
    const std::size_t end = text.find('\n', at);
    const std::string line =
        text.substr(at, end == std::string::npos ? end : end - at);
    at = end == std::string::npos ? text.size() : end + 1;
    if (line.starts_with("amberfolio-recording ")) {
      out += "amberfolio-recording " + std::to_string(version) +
             " state=" + std::to_string(state_format_version) + "\n";
      continue;
    }
    if (line.starts_with("file ") || line.starts_with("dir ")) {
      if (!written) {
        for (const std::string& one : manifest) {
          out += one + "\n";
        }
        written = true;
      }
      continue;
    }
    out += line + "\n";
  }
  EXPECT_TRUE(written) << "the recording had no manifest to replace";
  return out;
}

/// A version-1 recording of the same run: the manifest as version 1 wrote
/// one — the root only, in the pinned order, a directory listed by name
/// with a zero size and a zero digest.
[[nodiscard]] std::string downgraded_to_version_one(const std::string& text) {
  std::vector<std::string> manifest;
  std::size_t at = 0;
  while (at < text.size()) {
    const std::size_t end = text.find('\n', at);
    const std::string line =
        text.substr(at, end == std::string::npos ? end : end - at);
    at = end == std::string::npos ? text.size() : end + 1;
    if (line.find('\\') != std::string::npos) {
      continue;  // Below the root: version 1 never saw it.
    }
    if (line.starts_with("file ")) {
      manifest.push_back(line);
    } else if (line.starts_with("dir ")) {
      manifest.push_back("file " + line.substr(4) + " 0 " +
                         std::string(sha256_digest::text_length, '0'));
    }
  }
  return with_manifest(text, 1, manifest);
}

/// A filesystem that is nothing but depth: every directory holds one
/// directory named `D`, all the way down.
///
/// No backend in this tree can build one — `canonicalize()` refuses a
/// ninth component, so nothing can be created down there — but a
/// directory-backed host is handed whatever the host's filesystem holds.
/// So the walk's bound is checked against a disk that has none, rather
/// than assumed from a backend that cannot break it.
class bottomless : public filesystem {
 public:
  vfs_result<file_handle> open(const dos_path&, open_mode) override {
    return {.value = {}, .error = vfs_error::access_denied};
  }
  vfs_result<file_handle> create(const dos_path&) override {
    return {.value = {}, .error = vfs_error::access_denied};
  }
  vfs_result<std::size_t> read(file_handle, std::span<std::uint8_t>) override {
    return {.value = 0, .error = vfs_error::invalid_handle};
  }
  vfs_result<std::size_t> write(file_handle,
                                std::span<const std::uint8_t>) override {
    return {.value = 0, .error = vfs_error::invalid_handle};
  }
  vfs_result<std::uint32_t> seek(file_handle, seek_origin,
                                 std::int32_t) override {
    return {.value = 0, .error = vfs_error::invalid_handle};
  }
  vfs_error truncate(file_handle) override { return vfs_error::invalid_handle; }
  vfs_error close(file_handle) override { return vfs_error::invalid_handle; }
  vfs_error unlink(const dos_path&) override {
    return vfs_error::access_denied;
  }
  vfs_error mkdir(const dos_path&) override { return vfs_error::access_denied; }
  [[nodiscard]] bool exists(const dos_path&) const override { return true; }
  [[nodiscard]] vfs_result<file_stat> stat(const dos_path&) const override {
    return {.value = {.size = 0, .is_directory = true},
            .error = vfs_error::none};
  }
  [[nodiscard]] vfs_result<std::size_t> entry_count(
      const dos_path&) const override {
    return {.value = 1, .error = vfs_error::none};
  }
  [[nodiscard]] vfs_result<directory_entry> entry_at(
      const dos_path&, std::size_t index) const override {
    if (index != 0) {
      return {.value = {}, .error = vfs_error::no_more_files};
    }
    const std::string_view name = "D";
    const vfs_result<dos_name> parsed =
        dos_name::parse(std::span<const char>(name.data(), name.size()));
    return {.value = {.name = parsed.value, .size = 0, .is_directory = true},
            .error = vfs_error::none};
  }
};

/// A host's replay loop over `r`: run to the next event (or a frame
/// boundary), apply, until the player is done or has something to say.
[[nodiscard]] replay_status play(const rig& r, replay_player& player) {
  replay_status status = player.apply(*r.box);
  while (status == replay_status::ok) {
    ticks target = r.box->time() + renderer::frame_period;
    if (player.next_tick() < target) {
      target = player.next_tick();
    }
    r.box->run(target);
    status = player.apply(*r.box);
  }
  return status;
}

TEST(Replay, ARecordingOfOneRunVerifiesAgainstAnother) {
  const rig first;
  first.start();
  const std::string text = record(
      first, {{5'000, 0x1E}, {50'000, 0x20}, {120'000, 0x30}}, 40'000, 200'000);
  EXPECT_NE(text.find("program KEYS.EXE "), std::string::npos);
  EXPECT_NE(text.find("file DATA.BIN 3 "), std::string::npos);
  EXPECT_NE(text.find("file KEYS.EXE 66 "), std::string::npos);
  EXPECT_NE(text.find("speed 256\n"), std::string::npos);
  EXPECT_NE(text.find("key 5000 1e down\n"), std::string::npos);
  EXPECT_NE(text.find("end 200000 "), std::string::npos);
  EXPECT_EQ(first.result(0), 3u) << "three keys were read";

  const rig second;
  second.start();
  replay_player player;
  ASSERT_TRUE(player.load(std::span<const char>(text.data(), text.size())));
  ASSERT_EQ(player.check_initial(*second.box, second.fs.get()),
            replay_status::ok);
  EXPECT_EQ(play(second, player), replay_status::done);
  EXPECT_EQ(player.checkpoints_verified(), 5u);
  EXPECT_EQ(player.keys_delivered(), 3u);
  EXPECT_EQ(second.result(0), 3u);
  EXPECT_EQ(second.result(1), first.result(1)) << "the same keys, the same sum";

  std::array<char, 256> report{};
  static_cast<void>(player.report(report));
  EXPECT_TRUE(
      std::string_view(report.data())
          .starts_with("amberfolio: replay verified checkpoints=5 keys=3"))
      << report.data();
}

TEST(Replay, AKeyAtAnotherTickIsADivergenceNamedBySection) {
  const rig first;
  first.start();
  std::string text = record(first, {{5'000, 0x1E}}, 40'000, 100'000);

  // The recording says the key came at tick 5000. Move it.
  const std::size_t at = text.find("key 5000 1e down");
  ASSERT_NE(at, std::string::npos);
  text.replace(at, 8, "key 9000");

  const rig second;
  second.start();
  replay_player player;
  ASSERT_TRUE(player.load(std::span<const char>(text.data(), text.size())));
  ASSERT_EQ(player.check_initial(*second.box, second.fs.get()),
            replay_status::ok);
  EXPECT_EQ(play(second, player), replay_status::diverged);
  EXPECT_EQ(player.checkpoints_verified(), 0u);

  std::array<char, 320> report{};
  static_cast<void>(player.report(report));
  const std::string_view said(report.data());
  EXPECT_TRUE(said.starts_with("amberfolio: replay diverged ")) << said;
  EXPECT_NE(said.find(" tick=40000 "), std::string_view::npos) << said;
  // The first checkpoint disagrees in the first section that kept the
  // difference: RAM, where the program stored how many polls the key
  // took. The clock and the processor agree — the same ticks passed, and
  // CX went on counting past the key in both runs — so neither is named.
  EXPECT_NE(said.find(" section=ram "), std::string_view::npos) << said;
  EXPECT_NE(said.find(" expected="), std::string_view::npos) << said;
}

TEST(Replay, InitialConditionsAreCheckedFieldByField) {
  const rig first;
  first.start();
  const std::string text = record(first, {}, 40'000, 50'000);

  // A different program: the digest does not match.
  {
    const rig other;
    other.stage("\\KEYS.EXE", std::vector<std::uint8_t>{1, 2, 3, 4});
    // Staging again replaced the file; loading it is not the point.
    sha256_digest elsewhere{};
    elsewhere.bytes[3] = 3;
    other.box->seams().loaded(elsewhere, image_load_segment);
    other.box->set_step_cost(1);
    replay_player player;
    ASSERT_TRUE(player.load(std::span<const char>(text.data(), text.size())));
    EXPECT_EQ(player.check_initial(*other.box, other.fs.get()),
              replay_status::malformed);
    std::array<char, 256> report{};
    static_cast<void>(player.report(report));
    EXPECT_NE(std::string_view(report.data()).find("the program loaded is not"),
              std::string_view::npos)
        << report.data();
  }
  // The wrong speed.
  {
    const rig other;
    other.start();
    other.box->set_step_cost(4);
    replay_player player;
    ASSERT_TRUE(player.load(std::span<const char>(text.data(), text.size())));
    EXPECT_EQ(player.check_initial(*other.box, other.fs.get()),
              replay_status::malformed);
    std::array<char, 256> report{};
    static_cast<void>(player.report(report));
    EXPECT_NE(std::string_view(report.data()).find("speed"),
              std::string_view::npos);
  }
  // A file that differs.
  {
    const rig other;
    other.stage("\\DATA.BIN", std::vector<std::uint8_t>{1, 2, 4});
    other.start();
    replay_player player;
    ASSERT_TRUE(player.load(std::span<const char>(text.data(), text.size())));
    EXPECT_EQ(player.check_initial(*other.box, other.fs.get()),
              replay_status::malformed);
    std::array<char, 320> report{};
    static_cast<void>(player.report(report));
    EXPECT_NE(std::string_view(report.data()).find("fingerprint"),
              std::string_view::npos)
        << report.data();
  }
  // Everything as recorded: fine, and without a filesystem the manifest
  // is simply not checked.
  {
    const rig other;
    other.start();
    replay_player player;
    ASSERT_TRUE(player.load(std::span<const char>(text.data(), text.size())));
    EXPECT_EQ(player.check_initial(*other.box, nullptr), replay_status::ok);
    EXPECT_EQ(player.check_initial(*other.box, other.fs.get()),
              replay_status::ok);
  }
}

// --- The manifest, all the way down (#155) --------------------------------
//
// A recording's manifest is the statement of what disk the run started
// from. Until #155 it walked the root and nothing below it, so every byte
// of `\SAVE\` — which is the saved party a load leg reads — was outside a
// recording's initial conditions.

TEST(Replay, TheManifestNamesTheWholeDiskInOneOrder) {
  const rig first;
  first.make_dir("\\SAVE");
  // Staged out of order on purpose: the manifest's order is the walk's,
  // not the order anything was created in.
  const std::vector<std::uint8_t> b{2, 2, 2};
  const std::vector<std::uint8_t> a{1, 1};
  first.stage("\\SAVE\\B.DAT", b);
  first.stage("\\SAVE\\A.DAT", a);
  first.stage("\\SAVED.TXT", std::vector<std::uint8_t>{9});
  first.start();
  const std::string text = record(first, {}, 40'000, 50'000);

  // Depth first, each directory's entries in the VFS's pinned name order,
  // a directory before its contents — which is also why the subtree under
  // `SAVE` lands between `SAVE` and `SAVED.TXT`, a path sorting before
  // every path it is a prefix of.
  EXPECT_EQ(manifest_of(text),
            (std::vector<std::string>{
                "file DATA.BIN", "file ENDS.EXE", "file KEYS.EXE", "dir SAVE",
                "file SAVE\\A.DAT", "file SAVE\\B.DAT", "file SAVED.TXT"}))
      << text;

  // And a nested file is pinned, not merely named: its size and *the*
  // SHA-256 of its bytes.
  EXPECT_NE(text.find("file SAVE\\A.DAT 2 " + hex_of(sha256(a))),
            std::string::npos)
      << text;
  EXPECT_NE(text.find("file SAVE\\B.DAT 3 " + hex_of(sha256(b))),
            std::string::npos)
      << text;
  // A directory carries neither, because a directory has neither.
  EXPECT_NE(text.find("\ndir SAVE\n"), std::string::npos) << text;

  // The recording is still a recording of the run.
  const rig second;
  second.make_dir("\\SAVE");
  second.stage("\\SAVE\\B.DAT", b);
  second.stage("\\SAVE\\A.DAT", a);
  second.stage("\\SAVED.TXT", std::vector<std::uint8_t>{9});
  second.start();
  replay_player player;
  ASSERT_TRUE(player.load(std::span<const char>(text.data(), text.size())));
  ASSERT_EQ(player.check_initial(*second.box, second.fs.get()),
            replay_status::ok)
      << report_of(player);
  EXPECT_EQ(play(second, player), replay_status::done);
}

// The whole point of #155: a disk whose *nested* file differs is refused
// at the manifest, by name, before a step is taken — not thousands of
// frames later as a checkpoint hash that disagrees, which reads as a
// finding about the machine and is really a finding about a directory.
TEST(Replay, ANestedFileThatDiffersIsRefusedAtTheManifestByName) {
  const rig first;
  first.make_dir("\\SAVE");
  first.stage("\\SAVE\\CHRDATA1.SAV", std::vector<std::uint8_t>{1, 2, 3});
  first.stage("\\SAVE\\SAVGAMA.DAT", std::vector<std::uint8_t>{7});
  first.start();
  const std::string text = record(first, {}, 40'000, 50'000);

  // One byte of the saved party, one run further along. Through
  // `verify_recording()`, because that is the browser's only path to a
  // recording (`af_machine_verify_recording`) and the one the manifest
  // has to speak for.
  {
    const rig second;
    second.make_dir("\\SAVE");
    second.stage("\\SAVE\\CHRDATA1.SAV", std::vector<std::uint8_t>{1, 2, 4});
    second.stage("\\SAVE\\SAVGAMA.DAT", std::vector<std::uint8_t>{7});
    second.start();
    replay_player player;
    const verify_result out =
        verify_recording(*second.box, second.fs.get(),
                         std::span<const char>(text.data(), text.size()),
                         renderer::frame_period, player);
    EXPECT_EQ(out.status, replay_status::malformed);
    EXPECT_EQ(out.checkpoints, 0u) << "refused before the run, not during it";
    const std::string said = report_of(player);
    EXPECT_TRUE(said.starts_with("amberfolio: replay refused ")) << said;
    EXPECT_NE(said.find("fingerprint"), std::string::npos) << said;
    EXPECT_NE(said.find("path=SAVE\\CHRDATA1.SAV"), std::string::npos) << said;
    EXPECT_NE(said.find("expected="), std::string::npos) << said;
  }

  // A saved game the recording names and the disk has not got.
  {
    const rig second;
    second.make_dir("\\SAVE");
    second.stage("\\SAVE\\CHRDATA1.SAV", std::vector<std::uint8_t>{1, 2, 3});
    second.start();
    replay_player player;
    ASSERT_TRUE(player.load(std::span<const char>(text.data(), text.size())));
    EXPECT_EQ(player.check_initial(*second.box, second.fs.get()),
              replay_status::malformed);
    const std::string said = report_of(player);
    EXPECT_NE(said.find("does not hold"), std::string::npos) << said;
    EXPECT_NE(said.find("path=SAVE\\SAVGAMA.DAT"), std::string::npos) << said;
  }

  // And one the disk has and the recording does not name — as much "not
  // that disk" as a missing one, on `scripts/sweep.py`'s own reasoning:
  // the program opens its saves by name, so an extra slot is invisible
  // today and the day it stops being invisible is the day this would
  // have been the only warning.
  {
    const rig second;
    second.make_dir("\\SAVE");
    second.stage("\\SAVE\\CHRDATA1.SAV", std::vector<std::uint8_t>{1, 2, 3});
    second.stage("\\SAVE\\CHRDATA2.SAV", std::vector<std::uint8_t>{5});
    second.stage("\\SAVE\\SAVGAMA.DAT", std::vector<std::uint8_t>{7});
    second.start();
    replay_player player;
    ASSERT_TRUE(player.load(std::span<const char>(text.data(), text.size())));
    EXPECT_EQ(player.check_initial(*second.box, second.fs.get()),
              replay_status::malformed);
    const std::string said = report_of(player);
    EXPECT_NE(said.find("does not name"), std::string::npos) << said;
    EXPECT_NE(said.find("path=SAVE\\CHRDATA2.SAV"), std::string::npos) << said;
  }

  // A directory where the recording names a file is neither of those.
  {
    const rig second;
    second.make_dir("\\SAVE");
    second.stage("\\SAVE\\CHRDATA1.SAV", std::vector<std::uint8_t>{1, 2, 3});
    second.make_dir("\\SAVE\\SAVGAMA.DAT");
    second.start();
    replay_player player;
    ASSERT_TRUE(player.load(std::span<const char>(text.data(), text.size())));
    EXPECT_EQ(player.check_initial(*second.box, second.fs.get()),
              replay_status::malformed);
    const std::string said = report_of(player);
    EXPECT_NE(said.find("a directory where the recording names a file"),
              std::string::npos)
        << said;
    EXPECT_NE(said.find("path=SAVE\\SAVGAMA.DAT"), std::string::npos) << said;
  }
}

// The compatibility that had to hold. The seven recordings in
// `tests/sessions/` are version 1 and six of them are of a game whose
// disk is nobody's to re-record, so version 1 is not retired — it is read
// as version 1 wrote it, pinning the root and saying so.
TEST(Replay, AVersionOneRecordingStillVerifiesAndPinsOnlyTheRoot) {
  const rig first;
  first.make_dir("\\SAVE");
  first.stage("\\SAVE\\CHRDATA1.SAV", std::vector<std::uint8_t>{1, 2, 3});
  first.start();
  const std::string two = record(first, {}, 40'000, 50'000);
  const std::string one = downgraded_to_version_one(two);
  EXPECT_TRUE(one.starts_with("amberfolio-recording 1 state=1\n")) << one;
  EXPECT_NE(one.find("\nfile SAVE 0 " +
                     std::string(sha256_digest::text_length, '0') + "\n"),
            std::string::npos)
      << one;
  EXPECT_EQ(one.find("SAVE\\"), std::string::npos) << one;

  const auto disk_with = [](const rig& r,
                            const std::vector<std::uint8_t>& save) {
    r.make_dir("\\SAVE");
    r.stage("\\SAVE\\CHRDATA1.SAV", save);
    r.start();
  };
  const auto verdict = [](const rig& r, const std::string& text,
                          replay_player& player) {
    return verify_recording(*r.box, r.fs.get(),
                            std::span<const char>(text.data(), text.size()),
                            renderer::frame_period, player)
        .status;
  };

  {
    const rig same;
    disk_with(same, {1, 2, 3});
    replay_player player;
    EXPECT_EQ(verdict(same, one, player), replay_status::done)
        << report_of(player);
  }
  // And it goes on saying exactly what it always said: a version-1
  // manifest does not reach the saved game, so a disk that differs only
  // there verifies. That is the gap #155 closes for new recordings, kept
  // here as what the version number on the first line *means*.
  {
    const rig other;
    disk_with(other, {1, 2, 4});
    replay_player player;
    EXPECT_EQ(verdict(other, one, player), replay_status::done)
        << report_of(player);
  }
  // The version-2 recording of the same run over the same disk refuses
  // it, by name.
  {
    const rig other;
    disk_with(other, {1, 2, 4});
    replay_player player;
    EXPECT_EQ(verdict(other, two, player), replay_status::malformed);
    EXPECT_NE(report_of(player).find("path=SAVE\\CHRDATA1.SAV"),
              std::string::npos)
        << report_of(player);
  }

  // A version-1 recording that carries a version-2 manifest line is not a
  // recording. A format is what the first line says it is, and half of
  // one is not a grammar.
  {
    const std::string mixed = with_manifest(
        two, 1,
        {"dir SAVE", "file SAVE\\CHRDATA1.SAV 3 " +
                         std::string(sha256_digest::text_length, '0')});
    replay_player player;
    EXPECT_FALSE(
        player.load(std::span<const char>(mixed.data(), mixed.size())));
    EXPECT_NE(report_of(player).find("does not have"), std::string::npos)
        << report_of(player);
  }
}

// `load()` resets, and it has to do that without building a
// player-sized temporary: a player carries the whole manifest now, and
// `*this = replay_player{}` put some tens of kilobytes on the stack —
// which on a wasm module's is a fault, and was one. A player handed a
// second recording must keep nothing of the first.
TEST(Replay, APlayerReloadedKeepsNothingOfTheRecordingBefore) {
  const rig first;
  first.make_dir("\\SAVE");
  first.stage("\\SAVE\\A.DAT", std::vector<std::uint8_t>{1});
  first.start();
  const std::string big = record(first, {}, 40'000, 50'000);

  replay_player player;
  ASSERT_TRUE(player.load(std::span<const char>(big.data(), big.size())));
  EXPECT_EQ(player.preamble().file_count, 5u)
      << "three at the root, the directory, and the file in it";

  const rig plain;
  plain.start();
  const std::string small = record(plain, {}, 40'000, 50'000);
  ASSERT_TRUE(player.load(std::span<const char>(small.data(), small.size())));
  EXPECT_EQ(player.preamble().file_count, 3u);
  EXPECT_EQ(player.checkpoints_verified(), 0u);
  EXPECT_EQ(player.check_initial(*plain.box, plain.fs.get()), replay_status::ok)
      << report_of(player);
}

// Depth is bounded by `dos_path::max_depth` and refused rather than
// truncated at it, on both sides — a manifest that stopped short would
// pin a disk that is not the disk.
TEST(Replay, ADiskDeeperThanAPathIsRefusedRatherThanTruncated) {
  const rig r;
  r.start();
  bottomless deep;

  std::vector<char> buffer(replay_preamble_capacity);
  EXPECT_EQ(write_preamble(*r.box, deep, "KEYS.EXE", {}, buffer), 0u)
      << "a disk that cannot be described is not described in part";

  // The checking side, against a manifest that matches all the way down
  // and then asks for one component more than a path has.
  std::vector<std::string> manifest;
  std::string path;
  for (std::size_t i = 0; i < dos_path::max_depth; ++i) {
    path += i == 0 ? "D" : "\\D";
    manifest.push_back("dir " + path);
  }
  const std::string text =
      with_manifest(record(r, {}, 40'000, 50'000), 2, manifest);

  replay_player player;
  ASSERT_TRUE(player.load(std::span<const char>(text.data(), text.size())));
  EXPECT_EQ(player.check_initial(*r.box, &deep), replay_status::malformed);
  EXPECT_NE(report_of(player).find("deeper than a path"), std::string::npos)
      << report_of(player);
}

// The case a tick cannot describe on its own. A machine stops *inside* a
// step: the step is not counted and its ticks are not spent, so the
// stopped machine and the machine one step short of it stand at the same
// tick and the same step count. A player held at that tick would run to
// it, find nothing left to do and compare a machine that was never given
// the chance to stop — which is why the checkpoint says `stopped` and
// `next_tick()` lets the host run past it.
TEST(Replay, ARecordingThatEndsInAStopIsReachedByRunningPastIt) {
  const rig first;
  first.start("\\ENDS.EXE");
  const std::string text = record_to_stop(first, "ENDS.EXE");
  ASSERT_TRUE(first.box->stopped());
  EXPECT_EQ(first.box->stop().reason, stop_reason::program_exited);
  EXPECT_EQ(first.box->stop().exit_code, 7u);

  // Exactly one checkpoint is of a stopped machine: the last.
  EXPECT_NE(text.find(" stopped "), std::string::npos) << text;

  const rig second;
  second.start("\\ENDS.EXE");
  replay_player player;
  ASSERT_TRUE(player.load(std::span<const char>(text.data(), text.size())));
  ASSERT_EQ(player.check_initial(*second.box, second.fs.get()),
            replay_status::ok);
  EXPECT_EQ(play(second, player), replay_status::done);
  EXPECT_TRUE(second.box->stopped());
  EXPECT_EQ(second.box->stop().exit_code, 7u);
  EXPECT_EQ(second.box->time(), first.box->time());
  EXPECT_EQ(second.box->steps(), first.box->steps());
}

// And the marker is load-bearing, not decoration: without it the player
// holds the host at the tick, the machine never takes the step that
// stops it, and the state it is asked about is a different one.
TEST(Replay, AStoppedCheckpointWithoutItsMarkerDoesNotVerify) {
  const rig first;
  first.start("\\ENDS.EXE");
  std::string text = record_to_stop(first, "ENDS.EXE");

  const std::size_t at = text.find(" stopped ");
  ASSERT_NE(at, std::string::npos);
  text.erase(at, std::strlen(" stopped"));

  const rig second;
  second.start("\\ENDS.EXE");
  replay_player player;
  ASSERT_TRUE(player.load(std::span<const char>(text.data(), text.size())));
  ASSERT_EQ(player.check_initial(*second.box, second.fs.get()),
            replay_status::ok);
  EXPECT_EQ(play(second, player), replay_status::diverged);
  EXPECT_FALSE(second.box->stopped());
}

TEST(Replay, RefusesATextThatIsNotARecording) {
  replay_player player;
  const std::string_view junk = "hello\n";
  EXPECT_FALSE(player.load(std::span<const char>(junk.data(), junk.size())));
  EXPECT_EQ(player.status(), replay_status::malformed);

  // A `std::string` and not a view: the concatenation below is a
  // temporary, and a view of it would dangle at the semicolon. Version 3
  // is one this build has never written; 1 and 2 it has, and reads both.
  const std::string other_version =
      "amberfolio-recording 3 state=1\nprogram A.EXE " + std::string(64, 'a') +
      "\n";
  EXPECT_FALSE(player.load(
      std::span<const char>(other_version.data(), other_version.size())));

  const std::string no_program = "amberfolio-recording 1 state=1\nspeed 256\n";
  EXPECT_FALSE(
      player.load(std::span<const char>(no_program.data(), no_program.size())));
  std::array<char, 256> report{};
  static_cast<void>(player.report(report));
  EXPECT_TRUE(std::string_view(report.data())
                  .starts_with("amberfolio: replay refused "))
      << report.data();
}

TEST(Replay, ARecordingWithoutAnEndIsIncompleteNotDiverged) {
  const rig first;
  first.start();
  std::string text = record(first, {}, 40'000, 50'000);
  const std::size_t at = text.find("\nend ");
  ASSERT_NE(at, std::string::npos);
  text.resize(at + 1);

  const rig second;
  second.start();
  replay_player player;
  ASSERT_TRUE(player.load(std::span<const char>(text.data(), text.size())));
  EXPECT_EQ(play(second, player), replay_status::malformed);
  EXPECT_EQ(player.checkpoints_verified(), 1u) << "what was there held";
}

}  // namespace
}  // namespace amberfolio::machine
