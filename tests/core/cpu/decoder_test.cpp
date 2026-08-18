// SPDX-License-Identifier: AGPL-3.0-only
//
// The decoder: prefixes, ModRM, effective addresses, and dispatch.
//
// Almost everything here is checked through a real `step()` against a
// table of the test's own (test_dispatch.h), because that is the only way
// to see what an instruction handler will actually be handed. The
// addressing-form table below is the centre of it: twenty-four memory
// forms, each with a default segment that the encoding does not state and
// that nothing else in the machine will remind you of. Getting one of
// them wrong produces an emulator that runs for a while and then reads
// the wrong stack frame.

#include "amberfolio/cpu/decoder.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include "amberfolio/cpu/dispatch.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "cpu/test_bus.h"
#include "cpu/test_dispatch.h"

namespace amberfolio::cpu {
namespace {

using test::recording_diagnostics;
using test::test_bus;

// MOV r/m8, r8: an opcode with a ModRM byte and no immediate, so the
// bytes after the ModRM are the displacement and nothing else.
constexpr std::uint8_t mov_rm8_r8 = 0x88;

/// The segment the program runs in, and where it starts.
constexpr std::uint16_t code_segment = 0x1000;
constexpr std::uint16_t code_offset = 0x0100;

constexpr std::uint8_t modrm_byte(int mod, int reg, int rm) {
  return static_cast<std::uint8_t>((mod << 6) | (reg << 3) | rm);
}

/// A processor with every opcode implemented, running the bytes it is
/// given at CODE_SEGMENT:CODE_OFFSET, with distinguishable values in the
/// registers an effective address can be built out of.
class decoded {
 public:
  explicit decoded(std::initializer_list<std::uint8_t> code)
      : cpu_(mem_, &log_, table_) {
    cpu_.regs()[sreg::cs] = code_segment;
    cpu_.regs().ip = code_offset;

    cpu_.regs()[sreg::ds] = 0x3000;
    cpu_.regs()[sreg::ss] = 0x4000;
    cpu_.regs()[sreg::es] = 0x5000;

    cpu_.regs()[reg16::bx] = 0x1000;
    cpu_.regs()[reg16::bp] = 0x2000;
    cpu_.regs()[reg16::si] = 0x0300;
    cpu_.regs()[reg16::di] = 0x0040;

    mem_.poke(code_segment, code_offset, code);
  }

  step_status step() { return cpu_.step(); }

  [[nodiscard]] processor& cpu() { return cpu_; }
  [[nodiscard]] test_bus& mem() { return mem_; }
  [[nodiscard]] recording_diagnostics& log() { return log_; }

  /// How many bytes the instruction consumed.
  [[nodiscard]] int length() const {
    return static_cast<int>(cpu_.regs().ip) - code_offset;
  }

 private:
  test_bus mem_;
  recording_diagnostics log_;
  dispatch_table table_ = test::everything();
  processor cpu_;
};

// --- Effective addresses ---------------------------------------------

// One row of the table below. It has a constructor rather than being an
// aggregate so the rows can stay one line each: the table is meant to be
// read against an 8086 addressing-mode chart, and six named fields per
// row would bury the thing being checked in punctuation.
struct form {
  form(int a_mod, int a_rm, std::vector<std::uint8_t> a_disp,
       std::uint16_t a_segment, std::uint16_t a_offset, const char* a_what)
      : mod(a_mod),
        rm(a_rm),
        disp(std::move(a_disp)),
        segment(a_segment),
        offset(a_offset),
        what(a_what) {}

  int mod;
  int rm;
  std::vector<std::uint8_t> disp;
  std::uint16_t segment;  // expected segment *value*, not register
  std::uint16_t offset;   // expected offset
  const char* what;
};

TEST(Decoder, EveryMemoryAddressingForm) {
  // BX=1000 BP=2000 SI=0300 DI=0040, DS=3000 SS=4000.
  //
  // The default segment is the whole point of the third column: an address
  // built on BP is a stack address and defaults to SS; everything else
  // defaults to DS. The one that catches people is mod 00 rm 110, which is
  // where BP-with-no-displacement would sit — the encoding gives that slot
  // to a bare 16-bit address instead, and that address is a DS one.
  const std::vector<form> addressing_forms = {
      {0, 0, {}, 0x3000, 0x1300, "[BX+SI]"},
      {0, 1, {}, 0x3000, 0x1040, "[BX+DI]"},
      {0, 2, {}, 0x4000, 0x2300, "[BP+SI]"},
      {0, 3, {}, 0x4000, 0x2040, "[BP+DI]"},
      {0, 4, {}, 0x3000, 0x0300, "[SI]"},
      {0, 5, {}, 0x3000, 0x0040, "[DI]"},
      {0, 6, {0x34, 0x12}, 0x3000, 0x1234, "[disp16]"},
      {0, 7, {}, 0x3000, 0x1000, "[BX]"},

      {1, 0, {0x10}, 0x3000, 0x1310, "[BX+SI+disp8]"},
      {1, 1, {0x10}, 0x3000, 0x1050, "[BX+DI+disp8]"},
      {1, 2, {0x10}, 0x4000, 0x2310, "[BP+SI+disp8]"},
      {1, 3, {0x10}, 0x4000, 0x2050, "[BP+DI+disp8]"},
      {1, 4, {0x10}, 0x3000, 0x0310, "[SI+disp8]"},
      {1, 5, {0x10}, 0x3000, 0x0050, "[DI+disp8]"},
      {1, 6, {0x10}, 0x4000, 0x2010, "[BP+disp8]"},
      {1, 7, {0x10}, 0x3000, 0x1010, "[BX+disp8]"},

      {2, 0, {0x00, 0x01}, 0x3000, 0x1400, "[BX+SI+disp16]"},
      {2, 1, {0x00, 0x01}, 0x3000, 0x1140, "[BX+DI+disp16]"},
      {2, 2, {0x00, 0x01}, 0x4000, 0x2400, "[BP+SI+disp16]"},
      {2, 3, {0x00, 0x01}, 0x4000, 0x2140, "[BP+DI+disp16]"},
      {2, 4, {0x00, 0x01}, 0x3000, 0x0400, "[SI+disp16]"},
      {2, 5, {0x00, 0x01}, 0x3000, 0x0140, "[DI+disp16]"},
      {2, 6, {0x00, 0x01}, 0x4000, 0x2100, "[BP+disp16]"},
      {2, 7, {0x00, 0x01}, 0x3000, 0x1100, "[BX+disp16]"},
  };

  ASSERT_EQ(addressing_forms.size(), 24u) << "all 24 memory forms, or none";

  for (const form& f : addressing_forms) {
    std::vector<std::uint8_t> code{mov_rm8_r8, modrm_byte(f.mod, 0, f.rm)};
    code.insert(code.end(), f.disp.begin(), f.disp.end());

    test_bus mem;
    const dispatch_table table = test::everything();
    processor cpu(mem, nullptr, table);
    cpu.regs()[sreg::cs] = code_segment;
    cpu.regs().ip = code_offset;
    cpu.regs()[sreg::ds] = 0x3000;
    cpu.regs()[sreg::ss] = 0x4000;
    cpu.regs()[reg16::bx] = 0x1000;
    cpu.regs()[reg16::bp] = 0x2000;
    cpu.regs()[reg16::si] = 0x0300;
    cpu.regs()[reg16::di] = 0x0040;
    for (std::size_t i = 0; i < code.size(); ++i) {
      mem.poke(code_segment, static_cast<std::uint16_t>(code_offset + i),
               {code[i]});
    }

    ASSERT_EQ(cpu.step(), step_status::ran) << f.what;
    EXPECT_TRUE(cpu.current().modrm_present) << f.what;
    EXPECT_FALSE(cpu.current().modrm.names_a_register()) << f.what;
    EXPECT_EQ(cpu.current().ea.segment, f.segment) << f.what;
    EXPECT_EQ(cpu.current().ea.offset, f.offset) << f.what;

    // The instruction is as long as its parts: opcode, ModRM, and however
    // much displacement the form carries. If it were not, the next
    // instruction would start in the middle of this one.
    EXPECT_EQ(cpu.regs().ip, code_offset + static_cast<int>(code.size()))
        << f.what;
  }
}

TEST(Decoder, ModThreeNamesARegisterAndHasNoAddress) {
  decoded d{mov_rm8_r8, modrm_byte(3, 2, 5)};

  ASSERT_EQ(d.step(), step_status::ran);
  EXPECT_TRUE(d.cpu().current().modrm_present);
  EXPECT_TRUE(d.cpu().current().modrm.names_a_register());
  EXPECT_EQ(d.cpu().current().modrm.reg, 2);
  EXPECT_EQ(d.cpu().current().modrm.rm, 5);
  EXPECT_EQ(d.length(), 2);
}

// A displacement byte is signed. [BX-16] with BX=0x1000 is 0x0FF0, not
// 0x10F0.
TEST(Decoder, DisplacementBytesAreSigned) {
  decoded d{mov_rm8_r8, modrm_byte(1, 0, 7), 0xF0};

  ASSERT_EQ(d.step(), step_status::ran);
  EXPECT_EQ(d.cpu().current().ea.offset, 0x0FF0);
}

// The offset arithmetic wraps in sixteen bits and stays inside its
// segment: 0xFFFF + 0x10 is 0x000F of the same segment, not the start of
// the next one.
TEST(Decoder, EffectiveAddressesWrapInsideTheirSegment) {
  decoded d{mov_rm8_r8, modrm_byte(1, 0, 7), 0x10};
  d.cpu().regs()[reg16::bx] = 0xFFFF;

  ASSERT_EQ(d.step(), step_status::ran);
  EXPECT_EQ(d.cpu().current().ea.segment, 0x3000);
  EXPECT_EQ(d.cpu().current().ea.offset, 0x000F);
}

// --- Segment overrides -----------------------------------------------

TEST(Decoder, EachOverridePrefixSelectsItsSegment) {
  struct override_case {
    std::uint8_t prefix;
    sreg segment;
    const char* what;
  };
  const std::array<override_case, 4> cases = {{
      {.prefix = 0x26, .segment = sreg::es, .what = "ES:"},
      {.prefix = 0x2E, .segment = sreg::cs, .what = "CS:"},
      {.prefix = 0x36, .segment = sreg::ss, .what = "SS:"},
      {.prefix = 0x3E, .segment = sreg::ds, .what = "DS:"},
  }};

  for (const override_case& c : cases) {
    decoded d{c.prefix, mov_rm8_r8, modrm_byte(0, 0, 7)};  // [BX], normally DS

    ASSERT_EQ(d.step(), step_status::ran) << c.what;
    EXPECT_TRUE(d.cpu().current().prefixes.has_segment_override) << c.what;
    EXPECT_EQ(d.cpu().current().prefixes.segment_override, c.segment) << c.what;
    EXPECT_EQ(d.cpu().current().ea.segment, d.cpu().regs()[c.segment])
        << c.what;
    EXPECT_EQ(d.cpu().current().ea.offset, 0x1000) << c.what;
  }
}

// An override beats the SS default too — it is not a "change DS to X", it
// is the segment, whatever the form would otherwise have chosen.
TEST(Decoder, AnOverrideReplacesTheStackDefaultAsWell) {
  decoded d{0x26, mov_rm8_r8, modrm_byte(1, 0, 6), 0x10};  // ES:[BP+0x10]

  ASSERT_EQ(d.step(), step_status::ran);
  EXPECT_EQ(d.cpu().current().ea.segment, 0x5000);
  EXPECT_EQ(d.cpu().current().ea.offset, 0x2010);
}

TEST(Decoder, WithoutAPrefixThereIsNoOverride) {
  decoded d{mov_rm8_r8, modrm_byte(0, 0, 7)};

  ASSERT_EQ(d.step(), step_status::ran);
  EXPECT_FALSE(d.cpu().current().prefixes.has_segment_override);
  EXPECT_EQ(d.cpu().current().prefixes.count, 0);
}

// --- Prefix chains ---------------------------------------------------

// The vectors prepend random prefixes, useless ones included, so any
// number in any order has to decode. Later prefixes of a kind replace
// earlier ones.
TEST(Decoder, ArbitraryPrefixChainsDecodeAndTheLastOfEachKindWins) {
  decoded d{0x2E, 0x36, 0xF3,       0xF2,
            0x26, 0xF0, mov_rm8_r8, modrm_byte(0, 0, 7)};

  ASSERT_EQ(d.step(), step_status::ran);

  const prefix_state& p = d.cpu().current().prefixes;
  EXPECT_EQ(p.count, 6);
  EXPECT_TRUE(p.has_segment_override);
  EXPECT_EQ(p.segment_override, sreg::es);  // 26 came after 2E and 36
  EXPECT_EQ(p.rep, repeat::repne);          // F2 came after F3
  EXPECT_TRUE(p.lock);
  EXPECT_EQ(d.cpu().current().opcode, mov_rm8_r8);
  EXPECT_EQ(d.cpu().current().ea.segment, 0x5000);
}

TEST(Decoder, RepAndRepneAreToldApart) {
  {
    decoded d{0xF3, 0xA4};  // REP MOVSB
    ASSERT_EQ(d.step(), step_status::ran);
    EXPECT_EQ(d.cpu().current().prefixes.rep, repeat::repe);
  }
  {
    decoded d{0xF2, 0xAE};  // REPNE SCASB
    ASSERT_EQ(d.step(), step_status::ran);
    EXPECT_EQ(d.cpu().current().prefixes.rep, repeat::repne);
  }
}

// F1 is undocumented and decodes exactly as F0 does.
TEST(Decoder, BothLockPrefixesDecode) {
  for (const std::uint8_t prefix : {std::uint8_t{0xF0}, std::uint8_t{0xF1}}) {
    decoded d{prefix, 0x90};
    ASSERT_EQ(d.step(), step_status::ran) << "prefix " << int{prefix};
    EXPECT_TRUE(d.cpu().current().prefixes.lock) << "prefix " << int{prefix};
  }
}

// M1-F7 needs this: an interrupted REP resumes at the *last* prefix, not
// at the start of the instruction, so the decoder has to record where
// that byte was.
TEST(Decoder, TheOffsetOfTheLastPrefixIsRecorded) {
  decoded d{0x2E, 0xF3, 0x26, 0xA4};

  ASSERT_EQ(d.step(), step_status::ran);
  EXPECT_EQ(d.cpu().current().prefixes.count, 3);
  EXPECT_EQ(d.cpu().current().prefixes.last_prefix_ip, code_offset + 2);
  EXPECT_EQ(d.cpu().current().start_ip, code_offset);
}

// A prefix on an instruction with no memory operand is decoded, recorded
// and has no effect. The vectors do this on purpose.
TEST(Decoder, AUselessOverrideIsHarmless) {
  decoded d{0x36, 0x90};  // SS: NOP

  ASSERT_EQ(d.step(), step_status::ran);
  EXPECT_TRUE(d.cpu().current().prefixes.has_segment_override);
  EXPECT_FALSE(d.cpu().current().modrm_present);
  EXPECT_EQ(d.length(), 2);
}

// A run of prefix bytes with no opcode at the end never completes on real
// hardware — it is a hang. Reproducing a hang faithfully means hanging,
// so this machine refuses instead, and says so. No real instruction comes
// near the limit.
TEST(Decoder, AnEndlessPrefixRunStopsRatherThanSpinning) {
  test_bus mem;
  recording_diagnostics log;
  const dispatch_table table = test::everything();
  processor cpu(mem, &log, table);
  cpu.regs()[sreg::cs] = code_segment;
  cpu.regs().ip = code_offset;

  for (unsigned i = 0; i <= processor::prefix_limit + 4; ++i) {
    mem.poke(code_segment, static_cast<std::uint16_t>(code_offset + i), {0x26});
  }

  EXPECT_EQ(cpu.step(), step_status::stopped);
  EXPECT_EQ(cpu.stop().reason, stop_reason::prefix_chain_too_long);
  EXPECT_EQ(cpu.stop().ip, code_offset);
  EXPECT_EQ(cpu.regs().ip, code_offset);
  ASSERT_EQ(log.reports.size(), 1u);
}

// One below the limit still runs, so the limit is a limit and not an
// off-by-one that quietly rejects legal instructions.
TEST(Decoder, APrefixRunAtTheLimitStillRuns) {
  test_bus mem;
  const dispatch_table table = test::everything();
  processor cpu(mem, nullptr, table);
  cpu.regs()[sreg::cs] = code_segment;
  cpu.regs().ip = code_offset;

  for (unsigned i = 0; i < processor::prefix_limit; ++i) {
    mem.poke(code_segment, static_cast<std::uint16_t>(code_offset + i), {0x26});
  }
  mem.poke(code_segment,
           static_cast<std::uint16_t>(code_offset + processor::prefix_limit),
           {0x90});

  EXPECT_EQ(cpu.step(), step_status::ran);
  EXPECT_EQ(cpu.current().prefixes.count,
            static_cast<int>(processor::prefix_limit));
  EXPECT_EQ(cpu.current().opcode, 0x90);
}

// --- Which opcodes carry a ModRM byte ---------------------------------

TEST(Decoder, TheModrmTableMatchesTheOpcodeMap) {
  // Spot checks against the 8086 opcode map, one per shape.
  EXPECT_TRUE(has_modrm(0x00));  // ADD r/m8, r8
  EXPECT_TRUE(has_modrm(0x3B));  // CMP r16, r/m16
  EXPECT_TRUE(has_modrm(0x83));  // ALU r/m16, imm8 (group)
  EXPECT_TRUE(has_modrm(0x8D));  // LEA
  EXPECT_TRUE(has_modrm(0x8F));  // POP r/m16
  EXPECT_TRUE(has_modrm(0xC5));  // LDS
  EXPECT_TRUE(has_modrm(0xC7));  // MOV r/m16, imm16
  EXPECT_TRUE(has_modrm(0xD3));  // shift r/m16, CL (group)
  EXPECT_TRUE(has_modrm(0xDF));  // ESC
  EXPECT_TRUE(has_modrm(0xFF));  // INC/DEC/CALL/JMP/PUSH (group)

  EXPECT_FALSE(has_modrm(0x04));  // ADD AL, imm8
  EXPECT_FALSE(has_modrm(0x40));  // INC AX
  EXPECT_FALSE(has_modrm(0x74));  // JZ rel8
  EXPECT_FALSE(has_modrm(0x90));  // NOP
  EXPECT_FALSE(has_modrm(0xA4));  // MOVSB
  EXPECT_FALSE(has_modrm(0xB8));  // MOV AX, imm16
  EXPECT_FALSE(has_modrm(0xC3));  // RET
  EXPECT_FALSE(has_modrm(0xD4));  // AAM imm8
  EXPECT_FALSE(has_modrm(0xD7));  // XLAT
  EXPECT_FALSE(has_modrm(0xF4));  // HLT

  // Every group opcode has one by definition — the reg field it dispatches
  // on lives in it.
  for (const std::uint8_t opcode : group_opcodes) {
    EXPECT_TRUE(has_modrm(opcode)) << "group opcode " << int{opcode};
  }

  int count = 0;
  for (int opcode = 0; opcode < 256; ++opcode) {
    if (has_modrm(static_cast<std::uint8_t>(opcode))) {
      ++count;
    }
  }
  EXPECT_EQ(count, 68) << "the 8086 has 68 opcodes with a ModRM byte";
}

TEST(Decoder, ThePrefixTableIsTheEightPrefixBytes) {
  int count = 0;
  for (int byte = 0; byte < 256; ++byte) {
    if (is_prefix(static_cast<std::uint8_t>(byte))) {
      ++count;
    }
  }
  EXPECT_EQ(count, 8);

  for (const std::uint8_t byte :
       {std::uint8_t{0x26}, std::uint8_t{0x2E}, std::uint8_t{0x36},
        std::uint8_t{0x3E}, std::uint8_t{0xF0}, std::uint8_t{0xF1},
        std::uint8_t{0xF2}, std::uint8_t{0xF3}}) {
    EXPECT_TRUE(is_prefix(byte)) << int{byte};
  }
}

TEST(Decoder, OverridePrefixesMapOntoTheSegmentRegisterNumbering) {
  EXPECT_EQ(override_segment(0x26), sreg::es);
  EXPECT_EQ(override_segment(0x2E), sreg::cs);
  EXPECT_EQ(override_segment(0x36), sreg::ss);
  EXPECT_EQ(override_segment(0x3E), sreg::ds);
}

// --- Dispatch --------------------------------------------------------

TEST(Dispatch, GroupOpcodesAreTheTwelveThatDecodeTheirRegField) {
  EXPECT_EQ(group_opcodes.size(), 12u);
  for (const std::uint8_t opcode : group_opcodes) {
    EXPECT_NE(group_slot(opcode), not_a_group) << int{opcode};
  }

  // The near misses: nominally groups, but the 8086 ignores their reg
  // field, so they are ordinary opcodes here.
  EXPECT_EQ(group_slot(0x8F), not_a_group);
  EXPECT_EQ(group_slot(0xC6), not_a_group);
  EXPECT_EQ(group_slot(0xC7), not_a_group);
  EXPECT_EQ(group_slot(0x00), not_a_group);
}

TEST(Dispatch, AGroupOpcodeChoosesItsHandlerByTheRegField) {
  dispatch_table table{};
  const std::size_t slot = group_slot(0xFF);
  ASSERT_NE(slot, not_a_group);
  table.group[slot][0] = &test::mark<10>;  // INC r/m16
  table.group[slot][6] = &test::mark<16>;  // PUSH r/m16
  // The primary entry must never be consulted for a group opcode.
  table.primary[0xFF] = &test::mark<99>;

  test_bus mem;
  processor cpu(mem, nullptr, table);
  cpu.regs()[sreg::cs] = code_segment;
  cpu.regs().ip = code_offset;
  mem.poke(code_segment, code_offset,
           {0xFF, modrm_byte(3, 6, 0), 0xFF, modrm_byte(3, 0, 0)});

  test::ran.clear();
  ASSERT_EQ(cpu.step(), step_status::ran);
  ASSERT_EQ(cpu.step(), step_status::ran);

  EXPECT_EQ(test::ran, (std::vector<int>{16, 10}));
}

TEST(Dispatch, AMissingGroupEntryStopsAndNamesTheRegField) {
  dispatch_table table{};
  table.group[group_slot(0xFF)][0] = test::present;  // only INC exists

  test_bus mem;
  recording_diagnostics log;
  processor cpu(mem, &log, table);
  cpu.regs()[sreg::cs] = code_segment;
  cpu.regs().ip = code_offset;
  mem.poke(code_segment, code_offset, {0xFF, modrm_byte(3, 6, 0)});

  EXPECT_EQ(cpu.step(), step_status::stopped);
  EXPECT_EQ(cpu.stop().reason, stop_reason::unimplemented_opcode);
  EXPECT_EQ(cpu.stop().opcode, 0xFF);
  // Without this, the report would say "FF is unimplemented" when four of
  // its five instructions are implemented.
  EXPECT_EQ(cpu.stop().extension, 6);
  EXPECT_EQ(cpu.stop().ip, code_offset);
  EXPECT_EQ(cpu.regs().ip, code_offset);
  ASSERT_EQ(log.reports.size(), 1u);
}

// A stop rewinds past the ModRM byte and the displacement too, not just
// past the opcode — the whole instruction is un-consumed.
TEST(Dispatch, AStopRewindsPastTheWholeDecodedInstruction) {
  dispatch_table table{};

  test_bus mem;
  processor cpu(mem, nullptr, table);
  cpu.regs()[sreg::cs] = code_segment;
  cpu.regs().ip = code_offset;
  // ES: MOV r/m8, r8 with a 16-bit displacement — five bytes of prefix,
  // opcode, ModRM and displacement.
  mem.poke(code_segment, code_offset,
           {0x26, mov_rm8_r8, modrm_byte(2, 0, 7), 0x34, 0x12});

  EXPECT_EQ(cpu.step(), step_status::stopped);
  EXPECT_EQ(cpu.regs().ip, code_offset);
  EXPECT_EQ(cpu.stop().ip, code_offset);
  EXPECT_EQ(cpu.stop().opcode, mov_rm8_r8);
}

TEST(Dispatch, TheShippedInstructionSetIsWhatTheProcessorRunsByDefault) {
  // A property, not a list. M1-F3 wrote this as "the shipped table is
  // empty", which was true of the table it shipped and stopped being true
  // the moment the first of the wide phase's sixteen families landed —
  // and a list of opcodes here would be a sixteenth merge conflict for
  // every one of them. What is worth asserting either way is the thing
  // the name claims: a processor built without a table argument dispatches
  // by instruction_set(), not by some other table.
  //
  // It is provable from the un-implemented side, which is the side that
  // has an observable answer whatever the table holds: an entry with no
  // handler must stop a default-constructed processor, and the stop
  // record must name that entry.
  //
  // The group tables are where that stays true forever. FE's reg fields
  // 2-7 are not instructions on an 8086 at all, so they are null now and
  // will still be null when M1-C1 has filled everything that is real —
  // which is what keeps this from quietly becoming a test of nothing.
  const dispatch_table& shipped = instruction_set();

  constexpr std::uint8_t group_opcode = 0xFE;
  constexpr std::uint8_t undefined_extension = 2;
  ASSERT_EQ(shipped.group[group_slot(group_opcode)][undefined_extension],
            nullptr)
      << "FE /2 is not an 8086 instruction and nothing should have wired it";

  {
    test_bus mem;
    processor cpu(mem);  // no table argument: the point of the test
    cpu.regs()[sreg::cs] = code_segment;
    cpu.regs().ip = code_offset;
    mem.poke(code_segment, code_offset,
             {group_opcode, modrm_byte(3, undefined_extension, 0)});

    EXPECT_EQ(cpu.step(), step_status::stopped);
    EXPECT_EQ(cpu.stop().reason, stop_reason::unimplemented_opcode);
    EXPECT_EQ(cpu.stop().opcode, group_opcode);
    EXPECT_EQ(cpu.stop().extension, undefined_extension);
  }

  // And the same from the primary table, for as long as the wide phase
  // leaves anything in it — a prefix byte does not count, because it is
  // consumed before dispatch and so has no handler by construction.
  for (int opcode = 0; opcode < 256; ++opcode) {
    const auto byte = static_cast<std::uint8_t>(opcode);
    if (group_slot(byte) != not_a_group || is_prefix(byte) ||
        shipped.primary[static_cast<std::size_t>(opcode)] != nullptr) {
      continue;
    }

    test_bus mem;
    processor cpu(mem);
    cpu.regs()[sreg::cs] = code_segment;
    cpu.regs().ip = code_offset;
    // Five zero bytes behind it, so an opcode that carries a ModRM byte
    // and a displacement still has something to decode before it stops.
    mem.poke(code_segment, code_offset, {byte, 0x00, 0x00, 0x00, 0x00, 0x00});

    EXPECT_EQ(cpu.step(), step_status::stopped) << "opcode " << opcode;
    EXPECT_EQ(cpu.stop().reason, stop_reason::unimplemented_opcode);
    EXPECT_EQ(cpu.stop().opcode, byte);
  }
}

// --- The memory-access layer ------------------------------------------

// A word is two byte accesses, low half first, and the second one wraps
// inside the segment rather than running into the next.
TEST(Memory, AWordAtTheTopOfASegmentWrapsToItsBottom) {
  test_bus mem;
  const dispatch_table table = test::everything();
  processor cpu(mem, nullptr, table);

  mem.poke(physical_address(0x2000, 0xFFFF), {0xCD});
  mem.poke(physical_address(0x2000, 0x0000), {0xAB});

  mem.accesses.clear();
  EXPECT_EQ(cpu.read_word(0x2000, 0xFFFF), 0xABCD);

  ASSERT_EQ(mem.accesses.size(), 2u);
  EXPECT_EQ(mem.accesses[0].address, physical_address(0x2000, 0xFFFF));
  EXPECT_EQ(mem.accesses[1].address, physical_address(0x2000, 0x0000));
}

TEST(Memory, WritingAWordAtTheTopOfASegmentWrapsToo) {
  test_bus mem;
  const dispatch_table table = test::everything();
  processor cpu(mem, nullptr, table);

  cpu.write_word(0x2000, 0xFFFF, 0xABCD);

  EXPECT_EQ(mem.peek(physical_address(0x2000, 0xFFFF)), 0xCD);
  EXPECT_EQ(mem.peek(physical_address(0x2000, 0x0000)), 0xAB);
}

TEST(Memory, WordsAreLittleEndian) {
  test_bus mem;
  const dispatch_table table = test::everything();
  processor cpu(mem, nullptr, table);

  mem.poke(physical_address(0x1000, 0x0010), {0x34, 0x12});
  EXPECT_EQ(cpu.read_word(0x1000, 0x0010), 0x1234);

  cpu.write_word(0x1000, 0x0020, 0x5678);
  EXPECT_EQ(mem.peek(physical_address(0x1000, 0x0020)), 0x78);
  EXPECT_EQ(mem.peek(physical_address(0x1000, 0x0021)), 0x56);
}

// Both wraps at once: the offset wraps inside the segment, and the
// physical address the pair folds down to wraps at 1 MiB.
TEST(Memory, BothWrapsApplyAtTheTopOfTheAddressSpace) {
  test_bus mem;
  const dispatch_table table = test::everything();
  processor cpu(mem, nullptr, table);

  // F800:FFFF is physical 0x107FFF, which is 0x07FFF; the second byte of
  // the word is at F800:0000, physical 0xF8000.
  mem.poke(0x07FFF, {0x11});
  mem.poke(0xF8000, {0x22});

  EXPECT_EQ(cpu.read_word(0xF800, 0xFFFF), 0x2211);
}

TEST(Memory, WidthParameterizedAccessPicksTheRightSize) {
  test_bus mem;
  const dispatch_table table = test::everything();
  processor cpu(mem, nullptr, table);
  const address at{.segment = 0x1000, .offset = 0x0030};

  mem.poke(physical_address(0x1000, 0x0030), {0x34, 0x12});

  EXPECT_EQ(cpu.read(width::byte, at), 0x34);
  EXPECT_EQ(cpu.read(width::word, at), 0x1234);

  cpu.write(width::byte, at, 0xFF);
  EXPECT_EQ(mem.peek(physical_address(0x1000, 0x0030)), 0xFF);
  EXPECT_EQ(mem.peek(physical_address(0x1000, 0x0031)), 0x12);

  cpu.write(width::word, at, 0xBEEF);
  EXPECT_EQ(mem.peek(physical_address(0x1000, 0x0030)), 0xEF);
  EXPECT_EQ(mem.peek(physical_address(0x1000, 0x0031)), 0xBE);
}

// --- The decoded operands ---------------------------------------------

TEST(Operands, RmReachesARegisterWhenModIsThree) {
  decoded d{mov_rm8_r8, modrm_byte(3, 0, 3)};  // rm = BL, reg = AL
  d.cpu().regs()[reg16::bx] = 0x1234;
  d.cpu().regs()[reg16::ax] = 0x5678;

  ASSERT_EQ(d.step(), step_status::ran);
  EXPECT_EQ(d.cpu().read_rm(width::byte), 0x34);
  EXPECT_EQ(d.cpu().read_reg(width::byte), 0x78);
  EXPECT_EQ(d.cpu().read_rm(width::word), 0x1234);

  d.cpu().write_rm(width::byte, 0xFF);
  EXPECT_EQ(d.cpu().regs()[reg16::bx], 0x12FF);

  d.cpu().write_reg(width::word, 0xCAFE);
  EXPECT_EQ(d.cpu().regs()[reg16::ax], 0xCAFE);
}

TEST(Operands, RmReachesMemoryOtherwise) {
  decoded d{mov_rm8_r8, modrm_byte(0, 1, 7)};  // [BX], reg = CL

  ASSERT_EQ(d.step(), step_status::ran);
  ASSERT_EQ(d.cpu().current().ea,
            (address{.segment = 0x3000, .offset = 0x1000}));

  d.mem().poke(physical_address(0x3000, 0x1000), {0x5A, 0xA5});
  EXPECT_EQ(d.cpu().read_rm(width::byte), 0x5A);
  EXPECT_EQ(d.cpu().read_rm(width::word), 0xA55A);

  d.cpu().write_rm(width::word, 0x1234);
  EXPECT_EQ(d.mem().peek(physical_address(0x3000, 0x1000)), 0x34);
  EXPECT_EQ(d.mem().peek(physical_address(0x3000, 0x1001)), 0x12);
}

// The reg field is a register whatever the r/m operand turned out to be,
// and which register depends on the width: field 2 is DL as a byte and DX
// as a word, because reg8 and reg16 are two different numberings of the
// same three bits.
TEST(Operands, RegIsAlwaysARegister) {
  decoded d{mov_rm8_r8, modrm_byte(0, 2, 7)};  // [BX], reg = DL / DX
  d.cpu().regs()[reg16::dx] = 0xABCD;

  ASSERT_EQ(d.step(), step_status::ran);
  EXPECT_FALSE(d.cpu().current().modrm.names_a_register());
  EXPECT_EQ(d.cpu().read_reg(width::byte), 0xCD);
  EXPECT_EQ(d.cpu().read_reg(width::word), 0xABCD);
}

// --- Fetching ---------------------------------------------------------

TEST(Fetch, ImmediatesFollowTheDisplacementAndAdvanceIp) {
  // MOV r/m8, imm8 has no group, one ModRM byte, a disp16 and then the
  // immediate — the shape most likely to leave IP in the wrong place.
  decoded d{0xC6, modrm_byte(2, 0, 7), 0x34, 0x12, 0x77};

  ASSERT_EQ(d.step(), step_status::ran);
  EXPECT_EQ(d.cpu().current().ea.offset, 0x1234 + 0x1000);
  // The handler has not fetched the immediate; it is the next byte.
  EXPECT_EQ(d.cpu().regs().ip, code_offset + 4);
  EXPECT_EQ(d.cpu().fetch_byte(), 0x77);
  EXPECT_EQ(d.cpu().regs().ip, code_offset + 5);
}

TEST(Fetch, WordsComeOutOfTheStreamLittleEndian) {
  decoded d{0x90, 0x34, 0x12};

  ASSERT_EQ(d.step(), step_status::ran);
  EXPECT_EQ(d.cpu().fetch_word(), 0x1234);
  EXPECT_EQ(d.cpu().regs().ip, code_offset + 3);
}

}  // namespace
}  // namespace amberfolio::cpu
