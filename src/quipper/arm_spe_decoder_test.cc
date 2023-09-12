#include "arm_spe_decoder.h"

#include <string>
#include <vector>

#include "compat/test.h"
#include "test_utils.h"

namespace {

std::vector<std::string> SampleSPEPackets = {
    ///////////////////////////////// record 0
    "b0 d0 c2 a1 ed 66 ba ff c0",  // PC 0xffba66eda1c2d0 el2 ns=1
    "00 00 00 00 00",              // PAD
    "65 80 5f 00 00",              // CONTEXT 0x5f80 el2
    "49 00",                       // LD GP-REG
    "52 16 00",                    // EV RETIRED L1D-ACCESS TLB-ACCESS
    "99 04 00",                    // LAT 4 ISSUE
    "98 0c 00",                    // LAT 12 TOT
    "b2 28 6b 09 03 37 0e ff 00",  // VA 0xff0e3703096b28
    "9a 01 00",                    // LAT 1 XLAT
    "00 00 00 00 00 00 00 00 00",  // PAD
    "43 00",                       // DATA-SOURCE 0
    "00 00",                       // PAD
    "71 2e 65 2f 6a 0a 00 00 00",  // TS 44731163950
    ///////////////////////////////// record 1
    "b0 e0 b0 ef ed 66 ba ff c0",  // PC 0xffba66edefb0e0 el2 ns=1
    "00 00 00 00 00",              // PAD
    "65 0e 00 00 00",              // CONTEXT 0xe el2
    "4a 01",                       // B COND
    "52 42 00",                    // EV RETIRED NOT-TAKEN
    "99 10 00",                    // LAT 16 ISSUE
    "98 11 00",                    // LAT 17 TOT
    "b1 e4 b0 ef ed 66 ba ff c0",  // TGT 0xffba66edefb0e4 el2 ns=1
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",  // PAD
    "71 8d 65 2f 6a 0a 00 00 00",                       // TS 44731164045
};

#define EXPECT_FIELD_EQ(actual, expected, field, all_equal) \
  {                                                         \
    all_equal &= (actual).field == (expected).field;        \
    EXPECT_EQ((actual).field, (expected).field);            \
  }

bool CheckEqual(const quipper::ArmSpeDecoder::Record& actual,
                const quipper::ArmSpeDecoder::Record& expected) {
  bool all_equal = true;
  // record.event
  EXPECT_FIELD_EQ(actual, expected, event.gen_exception, all_equal);
  EXPECT_FIELD_EQ(actual, expected, event.retired, all_equal);
  EXPECT_FIELD_EQ(actual, expected, event.l1d_access, all_equal);
  EXPECT_FIELD_EQ(actual, expected, event.tlb_access, all_equal);
  EXPECT_FIELD_EQ(actual, expected, event.tlb_walk, all_equal);
  EXPECT_FIELD_EQ(actual, expected, event.cond_not_taken, all_equal);
  EXPECT_FIELD_EQ(actual, expected, event.br_mis_pred, all_equal);
  EXPECT_FIELD_EQ(actual, expected, event.llc_access, all_equal);
  EXPECT_FIELD_EQ(actual, expected, event.llc_miss, all_equal);
  EXPECT_FIELD_EQ(actual, expected, event.remote_access, all_equal);
  EXPECT_FIELD_EQ(actual, expected, event.ldst_alignment, all_equal);
  EXPECT_FIELD_EQ(actual, expected, event.sve_partial_pred, all_equal);
  EXPECT_FIELD_EQ(actual, expected, event.sve_empty_pred, all_equal);
  // record.op
  EXPECT_FIELD_EQ(actual, expected, op.is_other, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.other.cond, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.other.sve, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.other.sve_pred, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.is_ldst, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.ldst.ld, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.ldst.st, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.ldst.gp_reg, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.ldst.atomic, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.ldst.atomic_at, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.ldst.atomic_excl, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.ldst.atomic_ar, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.ldst.simd_fp, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.ldst.sve, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.ldst.unsp_reg, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.ldst.mrs, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.ldst.alloc_tag, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.ldst.memcpy, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.ldst.memset, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.is_br_eret, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.br_eret.br_cond, all_equal);
  EXPECT_FIELD_EQ(actual, expected, op.br_eret.br_indirect, all_equal);
  // record latenties
  EXPECT_FIELD_EQ(actual, expected, total_lat, all_equal);
  EXPECT_FIELD_EQ(actual, expected, issue_lat, all_equal);
  EXPECT_FIELD_EQ(actual, expected, trans_lat, all_equal);
  // record.ip
  EXPECT_FIELD_EQ(actual, expected, ip.addr, all_equal);
  EXPECT_FIELD_EQ(actual, expected, ip.el, all_equal);
  EXPECT_FIELD_EQ(actual, expected, ip.ns, all_equal);
  // record.tgt_br_ip
  EXPECT_FIELD_EQ(actual, expected, tgt_br_ip.addr, all_equal);
  EXPECT_FIELD_EQ(actual, expected, tgt_br_ip.el, all_equal);
  EXPECT_FIELD_EQ(actual, expected, tgt_br_ip.ns, all_equal);
  // record.prev_br_ip
  EXPECT_FIELD_EQ(actual, expected, prev_br_ip.addr, all_equal);
  EXPECT_FIELD_EQ(actual, expected, prev_br_ip.el, all_equal);
  EXPECT_FIELD_EQ(actual, expected, prev_br_ip.ns, all_equal);
  // record.virt
  EXPECT_FIELD_EQ(actual, expected, virt.addr, all_equal);
  EXPECT_FIELD_EQ(actual, expected, virt.tag, all_equal);
  // record.phys
  EXPECT_FIELD_EQ(actual, expected, phys.addr, all_equal);
  EXPECT_FIELD_EQ(actual, expected, phys.ns, all_equal);
  EXPECT_FIELD_EQ(actual, expected, phys.ch, all_equal);
  EXPECT_FIELD_EQ(actual, expected, phys.pat, all_equal);
  // record.timestamp
  EXPECT_FIELD_EQ(actual, expected, timestamp, all_equal);
  // record.context
  EXPECT_FIELD_EQ(actual, expected, context.id, all_equal);
  EXPECT_FIELD_EQ(actual, expected, context.el1, all_equal);
  EXPECT_FIELD_EQ(actual, expected, context.el2, all_equal);
  // record.source
  EXPECT_FIELD_EQ(actual, expected, source, all_equal);
  return all_equal;
}

}  // namespace

namespace quipper {

TEST(ArmSpeDecoderTest, CorrectlyParseRecords) {
  std::string trace = GenerateBinaryTrace(SampleSPEPackets);
  ArmSpeDecoder decoder(trace, false);

  ArmSpeDecoder::Record record[2];
  ArmSpeDecoder::Record tmp;

  // Expect two records in total.
  EXPECT_TRUE(decoder.NextRecord(&record[0]));
  EXPECT_TRUE(decoder.NextRecord(&record[1]));
  EXPECT_FALSE(decoder.NextRecord(&tmp));

  // Expect parsing the binary trace into packets, which form the records.
  EXPECT_TRUE(CheckEqual(
      record[0],
      ArmSpeDecoder::Record{
          .event = {.retired = true, .l1d_access = true, .tlb_access = true},
          .op = {.is_ldst = true, .ldst = {.ld = true, .gp_reg = true}},
          .total_lat = 12,
          .issue_lat = 4,
          .trans_lat = 1,
          .ip = {.addr = 0xffffba66eda1c2d0, .el = 2, .ns = 1},
          .virt = {.addr = 0xffff0e3703096b28},
          .timestamp = 44731163950,
          .context = {.id = 0x5f80, .el2 = true},
      }));

  EXPECT_TRUE(CheckEqual(
      record[1],
      ArmSpeDecoder::Record{
          .event = {.retired = true, .cond_not_taken = true},
          .op = {.is_br_eret = true, .br_eret = {.br_cond = true}},
          .total_lat = 17,
          .issue_lat = 16,
          .ip = {.addr = 0xffffba66edefb0e0, .el = 2, .ns = 1},
          .tgt_br_ip = {.addr = 0xffffba66edefb0e4, .el = 2, .ns = 1},
          .timestamp = 44731164045,
          .context = {.id = 0xe, .el2 = true},
      }));
}

TEST(ArmSpeDecoderTest, ReturnFalseUponInvalidData) {
  std::string trace = GenerateBinaryTrace({"ab cd ef ff 99 88 77 66 55 44"});
  ArmSpeDecoder decoder(trace, false);

  ArmSpeDecoder::Record tmp;

  EXPECT_FALSE(decoder.NextRecord(&tmp));
}

}  // namespace quipper
