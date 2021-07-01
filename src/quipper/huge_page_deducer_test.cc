// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "huge_page_deducer.h"

#include <sys/mman.h>

#include "base/logging.h"
#include "compat/string.h"
#include "compat/test.h"
#include "perf_reader.h"
#include "perf_serializer.h"
#include "test_perf_data.h"

namespace quipper {
namespace {
using PerfEvent = PerfDataProto::PerfEvent;
using MMapEvent = PerfDataProto::MMapEvent;
using ::testing::EqualsProto;
using ::testing::Pointwise;
using ::testing::proto::Partially;

// AddMmap is a helper function to create simple MMapEvents, with which
// testcases can encode "maps" entries similar to /proc/self/maps in a tabular
// one-line-per-entry.
void AddMmap(uint32_t pid, uint64_t mmap_start, uint64_t length, uint64_t pgoff,
             const std::string& file, RepeatedPtrField<PerfEvent>* events) {
  MMapEvent* ev = events->Add()->mutable_mmap_event();
  ev->set_pid(pid);
  ev->set_start(mmap_start);
  ev->set_len(length);
  ev->set_pgoff(pgoff);
  ev->set_filename(file);
}

// AddMmapWithoutPid is similar to AddMmap function but ignores pid.
void AddMmapWithoutPid(uint64_t mmap_start, uint64_t length, uint64_t pgoff,
                       const std::string& file,
                       RepeatedPtrField<PerfEvent>* events) {
  MMapEvent* ev = events->Add()->mutable_mmap_event();
  ev->set_start(mmap_start);
  ev->set_len(length);
  ev->set_pgoff(pgoff);
  ev->set_filename(file);
}

TEST(HugePageDeducer, HugePagesMappings) {
  RepeatedPtrField<PerfEvent> events;
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x40000000);
    ev->set_len(0x18000);
    ev->set_pgoff(0);
    ev->set_filename("/usr/lib/libfoo.so");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x40018000);
    ev->set_len(0x1e8000);
    ev->set_pgoff(0);
    ev->set_filename("/opt/google/chrome/chrome");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x40200000);
    ev->set_len(0x1c00000);
    ev->set_pgoff(0);
    ev->set_filename("//anon");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(1234);
    ev->set_start(0x41e00000);
    ev->set_len(0x4000000);
    ev->set_pgoff(0x1de8000);
    ev->set_filename("/opt/google/chrome/chrome");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(2345);
    ev->set_start(0x45e00000);
    ev->set_len(0x1e00000);
    ev->set_pgoff(0);
    ev->set_filename("//anon");
  }
  {
    MMapEvent* ev = events.Add()->mutable_mmap_event();
    ev->set_pid(2345);
    ev->set_start(0x47c00000);
    ev->set_len(0x4000000);
    ev->set_pgoff(0x1e00000);
    ev->set_filename("/opt/google/chrome/chrome");
  }

  DeduceHugePages(&events);
  CombineMappings(&events);

  ASSERT_GE(events.size(), 3);
  EXPECT_EQ(events.size(), 3);

  EXPECT_THAT(events,
              Pointwise(Partially(EqualsProto()),
                        {
                            "mmap_event: { start: 0x40000000 len:0x18000 "
                            "pgoff: 0 filename: '/usr/lib/libfoo.so'}",
                            "mmap_event: { start: 0x40018000 len:0x5de8000 "
                            "pgoff: 0 filename: '/opt/google/chrome/chrome'}",
                            "mmap_event: { start: 0x45e00000 len:0x5e00000 "
                            "pgoff: 0 filename: '/opt/google/chrome/chrome'}",
                        }));

  EXPECT_EQ("/usr/lib/libfoo.so", events[0].mmap_event().filename());
  EXPECT_EQ(0x40000000, events[0].mmap_event().start());
  EXPECT_EQ(0x18000, events[0].mmap_event().len());
  EXPECT_EQ(0x0, events[0].mmap_event().pgoff());

  // The split Chrome mappings should have been combined.
  EXPECT_EQ("/opt/google/chrome/chrome", events[2].mmap_event().filename());
  EXPECT_EQ(0x40018000, events[1].mmap_event().start());
  EXPECT_EQ(0x5de8000, events[1].mmap_event().len());
  EXPECT_EQ(0x0, events[1].mmap_event().pgoff());

  EXPECT_EQ("/opt/google/chrome/chrome", events[2].mmap_event().filename());
  EXPECT_EQ(0x45e00000, events[2].mmap_event().start());
  EXPECT_EQ(0x5e00000, events[2].mmap_event().len());
  EXPECT_EQ(0x0, events[2].mmap_event().pgoff());
}

enum HugepageTextStyle {
  kSlashSlashAnon,
  kAnonHugepage,
  kAnonHugepageDeleted,
  kNoHugepageText,
  kMemFdHugePageText,
};

class HugepageTextStyleDependent
    : public ::testing::TestWithParam<HugepageTextStyle> {
 protected:
  void AddHugepageTextMmap(uint32_t pid, uint64_t mmap_start, uint64_t length,
                           uint64_t pgoff, std::string file,
                           RepeatedPtrField<PerfEvent>* events) {
    // Various hugepage implementations and perf versions result in various
    // quirks in how hugepages are reported.

    switch (GetParam()) {
      case kNoHugepageText:
        // Do nothing; the maps are complete and file-backed
        break;
      case kSlashSlashAnon:
        // exec is remapped into anonymous memory, which perf reports as
        // '//anon'. Anonymous sections have no pgoff.
        file = "//anon";
        pgoff = 0;
        break;
      case kAnonHugepage:
        file = "/anon_hugepage";
        pgoff = 0;
        break;
      case kAnonHugepageDeleted:
        file = "/anon_hugepage (deleted)";
        pgoff = 0;
        break;
      case kMemFdHugePageText:
        // exec is remapped onto hugepages obtained from tmpfs/tlbfs using
        // memfd_create that doesn't create (even momentarily) a backing file.
        file =
            "/memfd:hugepage_text.buildid_aabbccddeeff00112233445566778899 "
            "(deleted)";
        break;
      default:
        CHECK(false) << "Unimplemented";
    }
    AddMmap(pid, mmap_start, length, pgoff, file, events);
  }
};

TEST_P(HugepageTextStyleDependent, OnlyOneMappingThatIsHuge) {
  RepeatedPtrField<PerfEvent> events;
  AddHugepageTextMmap(1, 0x100200000, 0x200000, 0, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  // Don't check filename='file'; if it's backed by anonymous memory, it isn't
  // possible for quipper to deduce the filename without other mmaps immediately
  // adjacent.
  EXPECT_THAT(
      events,
      Pointwise(Partially(EqualsProto()),
                {"mmap_event: { start: 0x100200000 len: 0x200000 pgoff: 0}"}));
}

TEST_P(HugepageTextStyleDependent, OnlyOneMappingUnaligned) {
  RepeatedPtrField<PerfEvent> events;
  AddMmap(2, 0x200201000, 0x200000, 0, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x200201000 "
                                 "len:0x200000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, FirstPageIsHugeWithSmallTail) {
  RepeatedPtrField<PerfEvent> events;
  AddHugepageTextMmap(3, 0x300400000, 0x400000, 0, "file", &events);
  AddMmap(3, 0x300800000, 0x001000, 0x400000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x300400000 "
                                 "len:0x401000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, DISABLED_FirstPageIsSmallWithHugeTail) {
  // This test is disabled because DeduceHugePage requires a non-zero pgoff
  // *after* a hugepage_text section in order to correctly deduce it, so it
  // is unable to deduce these cases.
  RepeatedPtrField<PerfEvent> events;
  AddMmap(4, 0x4003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(4, 0x400400000, 0x200000, 0x001000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x4003ff000 "
                                 "len:0x201000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, HugePageBetweenTwoSmallSections) {
  RepeatedPtrField<PerfEvent> events;
  AddMmap(5, 0x5003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(5, 0x500400000, 0x200000, 0x001000, "file", &events);
  AddMmap(5, 0x500600000, 0x001000, 0x201000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x5003ff000 "
                                 "len:0x202000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, HugePageSplitByEarlyMlockBetweenTwoSmall) {
  RepeatedPtrField<PerfEvent> events;
  AddMmap(6, 0x6003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(6, 0x600400000, 0x3f8000, 0x001000, "file", &events);
  AddHugepageTextMmap(6, 0x6007f8000, 0x008000, 0x3f9000, "file", &events);
  AddMmap(6, 0x600800000, 0x001000, 0x401000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x6003ff000 "
                                 "len:0x402000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, HugePageSplitByLateMlockBetweenTwoSmall) {
  RepeatedPtrField<PerfEvent> events;
  AddMmap(7, 0x7003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(7, 0x700400000, 0x008000, 0x001000, "file", &events);
  AddHugepageTextMmap(7, 0x700408000, 0x3f8000, 0x009000, "file", &events);
  AddMmap(7, 0x700800000, 0x001000, 0x401000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x7003ff000 "
                                 "len:0x402000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, HugePageSplitEvenlyByMlockBetweenTwoSmall) {
  RepeatedPtrField<PerfEvent> events;
  AddMmap(8, 0x8003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(8, 0x800400000, 0x0f8000, 0x001000, "file", &events);
  AddHugepageTextMmap(8, 0x8004f8000, 0x008000, 0x0f9000, "file", &events);
  AddHugepageTextMmap(8, 0x800500000, 0x100000, 0x101000, "file", &events);
  AddMmap(8, 0x800600000, 0x001000, 0x201000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x8003ff000 "
                                 "len:0x202000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, MultipleContiguousHugepages) {
  RepeatedPtrField<PerfEvent> events;
  AddMmap(9, 0x9003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(9, 0x900400000, 0x200000, 0x001000, "file", &events);
  AddHugepageTextMmap(9, 0x900600000, 0x200000, 0x201000, "file", &events);
  AddMmap(9, 0x900800000, 0x001000, 0x401000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x9003ff000 "
                                 "len:0x402000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, MultipleContiguousMlockSplitHugepages) {
  // Think:
  // - hugepage_text 4MiB range
  // - mlock alternating 512-KiB chunks
  RepeatedPtrField<PerfEvent> events;
  AddMmap(10, 0xa003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(10, 0xa00400000, 0x080000, 0x001000, "file", &events);
  AddHugepageTextMmap(10, 0xa00480000, 0x080000, 0x081000, "file", &events);
  AddHugepageTextMmap(10, 0xa00500000, 0x080000, 0x101000, "file", &events);
  AddHugepageTextMmap(10, 0xa00580000, 0x080000, 0x181000, "file", &events);
  AddHugepageTextMmap(10, 0xa00600000, 0x080000, 0x201000, "file", &events);
  AddHugepageTextMmap(10, 0xa00680000, 0x080000, 0x281000, "file", &events);
  AddHugepageTextMmap(10, 0xa00700000, 0x080000, 0x301000, "file", &events);
  AddHugepageTextMmap(10, 0xa00780000, 0x080000, 0x381000, "file", &events);
  AddMmap(10, 0xa00800000, 0x001000, 0x401000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0xa003ff000 "
                                 "len:0x402000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent,
       MultipleContiguousMlockSplitHugepagesTwoPids) {
  // Think:
  // - hugepage_text 4MiB range
  // - mlock alternating 512-KiB chunks
  RepeatedPtrField<PerfEvent> events;
  AddMmap(10, 0xa003ff000, 0x001000, 0, "file", &events);
  AddMmap(20, 0xa003ff000, 0x001000, 0, "file", &events);
  AddHugepageTextMmap(10, 0xa00400000, 0x080000, 0x001000, "file", &events);
  AddHugepageTextMmap(20, 0xa00400000, 0x080000, 0x001000, "file", &events);
  AddHugepageTextMmap(10, 0xa00480000, 0x080000, 0x081000, "file", &events);
  AddHugepageTextMmap(20, 0xa00480000, 0x080000, 0x081000, "file", &events);
  AddHugepageTextMmap(10, 0xa00500000, 0x080000, 0x101000, "file", &events);
  AddHugepageTextMmap(20, 0xa00500000, 0x080000, 0x101000, "file", &events);
  AddHugepageTextMmap(10, 0xa00580000, 0x080000, 0x181000, "file", &events);
  AddHugepageTextMmap(20, 0xa00580000, 0x080000, 0x181000, "file", &events);
  AddHugepageTextMmap(10, 0xa00600000, 0x080000, 0x201000, "file", &events);
  AddHugepageTextMmap(20, 0xa00600000, 0x080000, 0x201000, "file", &events);
  AddHugepageTextMmap(10, 0xa00680000, 0x080000, 0x281000, "file", &events);
  AddHugepageTextMmap(20, 0xa00680000, 0x080000, 0x281000, "file", &events);
  AddHugepageTextMmap(10, 0xa00700000, 0x080000, 0x301000, "file", &events);
  AddHugepageTextMmap(20, 0xa00700000, 0x080000, 0x301000, "file", &events);
  AddHugepageTextMmap(10, 0xa00780000, 0x080000, 0x381000, "file", &events);
  AddHugepageTextMmap(20, 0xa00780000, 0x080000, 0x381000, "file", &events);
  AddMmap(10, 0xa00800000, 0x001000, 0x401000, "file", &events);
  AddMmap(20, 0xa00800000, 0x001000, 0x401000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0xa003ff000 "
                                 "len:0x402000 pgoff: 0 filename: 'file'}",
                                 "mmap_event: { start: 0xa003ff000 "
                                 "len:0x402000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, MultipleWithUnalignedInitialHugePage) {
  // Base on real program
  RepeatedPtrField<PerfEvent> events;

  AddHugepageTextMmap(11, 0x85d32e000, 0x6d2000, 0x0, "file", &events);
  AddHugepageTextMmap(11, 0x85da00000, 0x6a00000, 0x6d2000, "file", &events);
  AddMmap(11, 0x864400000, 0x200000, 0x70d2000, "file", &events);
  AddHugepageTextMmap(11, 0x864600000, 0x200000, 0x72d2000, "file", &events);
  AddMmap(11, 0x864800000, 0x600000, 0x74d2000, "file", &events);
  AddHugepageTextMmap(11, 0x864e00000, 0x200000, 0x7ad2000, "file", &events);
  AddMmap(11, 0x865000000, 0x4a000, 0x7cd2000, "file", &events);
  AddMmap(11, 0x86504a000, 0x1000, 0x7d1c000, "file", &events);
  AddMmap(11, 0xa3d368000, 0x3a96000, 0x0, "file2", &events);
  AddMmap(11, 0xa467cc000, 0x2000, 0x0, "file3", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {
                                    "mmap_event: { start: 0x85d32e000 "
                                    "len:0x7d1d000 pgoff: 0 filename: 'file'}",
                                    "mmap_event: { start: 0xa3d368000 "
                                    "len:0x3a96000 pgoff: 0 filename: 'file2'}",
                                    "mmap_event: { start: 0xa467cc000 "
                                    "len:0x2000,  pgoff: 0 filename: 'file3'}",
                                }));
}

TEST_P(HugepageTextStyleDependent, MultipleWithUnalignedInitialHugePage2) {
  // Base on real program
  RepeatedPtrField<PerfEvent> events;
  AddHugepageTextMmap(12, 0xbcff6000, 0x200000, 0x00000000, "file", &events);
  AddMmap(12, 0xbd1f6000, 0x300a000, 0x200000, "file", &events);
  AddHugepageTextMmap(12, 0xc0200000, 0x2b374000, 0x320a000, "file", &events);
  AddHugepageTextMmap(12, 0xeb574000, 0x514000, 0x2e57e000, "file", &events);
  AddHugepageTextMmap(12, 0xeba88000, 0x1d78000, 0x2ea92000, "file", &events);
  AddMmap(12, 0xed800000, 0x1200000, 0x3080a000, "file", &events);
  AddHugepageTextMmap(12, 0xeea00000, 0x200000, 0x31a0a000, "file", &events);
  AddMmap(12, 0xeec00000, 0x2800000, 0x31c0a000, "file", &events);
  AddHugepageTextMmap(12, 0xf1400000, 0x200000, 0x3440a000, "file", &events);
  AddMmap(12, 0xf1600000, 0x89f000, 0x3460a000, "file", &events);
  AddMmap(12, 0xf1e9f000, 0x1000, 0x34ea9000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0xbcff6000 "
                                 "len:0x34eaa000 pgoff: 0 filename: 'file'}"}));
}

TEST_P(HugepageTextStyleDependent, NoMmaps) {
  RepeatedPtrField<PerfEvent> events;
  events.Add();

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(EqualsProto(), std::vector<PerfEvent>(1)));
}
TEST_P(HugepageTextStyleDependent, MultipleNonMmaps) {
  RepeatedPtrField<PerfEvent> events;
  events.Add();
  events.Add();

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(EqualsProto(), std::vector<PerfEvent>(2)));
}
TEST_P(HugepageTextStyleDependent, NonMmapFirstMmap) {
  RepeatedPtrField<PerfEvent> events;
  events.Add();
  AddHugepageTextMmap(12, 0, 0x200000, 0, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"", "mmap_event: { pgoff: 0 }"}));
}
TEST_P(HugepageTextStyleDependent, NonMmapAfterLastMmap) {
  RepeatedPtrField<PerfEvent> events;
  AddHugepageTextMmap(12, 0, 0x200000, 0, "file", &events);
  events.Add();

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { pgoff: 0 }", ""}));
}

TEST_P(HugepageTextStyleDependent, DisjointAnonMmapBeforeHugePageText) {
  RepeatedPtrField<PerfEvent> events;
  AddMmap(7, 0x500000000, 0x40000000, 0, "//anon", &events);
  AddHugepageTextMmap(7, 0x600000000, 0x400000, 0, "file", &events);
  AddMmap(7, 0x600400000, 0x001000, 0x400000, "file", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events, Pointwise(Partially(EqualsProto()),
                                {"mmap_event: { start: 0x500000000 "
                                 "len:0x40000000 pgoff: 0 filename: '//anon'}",
                                 "mmap_event: { start: 0x600000000 "
                                 "len:0x401000 pgoff: 0 filename: 'file'}"}));
}

INSTANTIATE_TEST_SUITE_P(NoHugepageText, HugepageTextStyleDependent,
                         ::testing::Values(kNoHugepageText));
INSTANTIATE_TEST_SUITE_P(SlashSlashAnon, HugepageTextStyleDependent,
                         ::testing::Values(kSlashSlashAnon));
INSTANTIATE_TEST_SUITE_P(AnonHugepage, HugepageTextStyleDependent,
                         ::testing::Values(kAnonHugepage));
INSTANTIATE_TEST_SUITE_P(AnonHugepageDeleted, HugepageTextStyleDependent,
                         ::testing::Values(kAnonHugepageDeleted));
INSTANTIATE_TEST_SUITE_P(MemFdHugepageText, HugepageTextStyleDependent,
                         ::testing::Values(kMemFdHugePageText));

TEST(HugePageDeducer, MemfsRandomSufix) {
  RepeatedPtrField<PerfEvent> events;

  AddMmapWithoutPid(
      2097152, 69206016, 0,
      "/foo.buildid_c45ff2c1f18928fdd3b79e56ddd15a8f.iWhFLe (deleted)",
      &events);
  AddMmapWithoutPid(
      71303168, 4194304, 69206016,
      "/foo.buildid_c45ff2c1f18928fdd3b79e56ddd15a8f.I4gGvd (deleted)",
      &events);
  AddMmapWithoutPid(75497472, 49078272, 73400320,
                    "/tempfile-nonet5-2293-8851-cec47724674-5b09f37381545",
                    &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(
      events,
      Pointwise(Partially(EqualsProto()),
                {
                    "mmap_event: { pgoff: 0 filename: "
                    "'/tempfile-nonet5-2293-8851-cec47724674-5b09f37381545' }",
                }));
}

TEST(HugePageDeducer, DoesNotChangeVirtuallyContiguousPgoffNonContiguous) {
  // We've seen programs with strange memory layouts having virtually contiguous
  // memory backed by non-contiguous bits of a file.
  RepeatedPtrField<PerfEvent> events;
  AddMmap(758463, 0x2f278000, 0x20000, 0, "lib0.so", &events);
  AddMmap(758463, 0x2f29d000, 0x2000, 0, "shm", &events);
  AddMmap(758463, 0x2f2a2000, 0xa000, 0, "lib1.so", &events);
  AddMmap(758463, 0x3d400000, 0x9ee000, 0, "lib2.so", &events);
  AddMmap(758463, 0x3e000000, 0x16000, 0, "lib3.so", &events);
  AddMmap(758463, 0x3e400000, 0x270000, 0x1a00000, "shm", &events);
  AddMmap(758463, 0x3e670000, 0x10000, 0x1aaac000, "shm", &events);
  AddMmap(758463, 0x3e680000, 0x10000, 0x1b410000, "shm", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(events,
              Pointwise(Partially(EqualsProto()),
                        {
                            "mmap_event: { pgoff: 0 filename: 'lib0.so' }",
                            "mmap_event: { pgoff: 0 filename: 'shm' }",
                            "mmap_event: { pgoff: 0 filename: 'lib1.so' }",
                            "mmap_event: { pgoff: 0 filename: 'lib2.so' }",
                            "mmap_event: { pgoff: 0 filename: 'lib3.so' }",
                            "mmap_event: { pgoff: 0x1a00000 filename: 'shm' }",
                            "mmap_event: { pgoff: 0x1aaac000 filename: 'shm' }",
                            "mmap_event: { pgoff: 0x1b410000 filename: 'shm' }",
                        }));
}

TEST(HugePageDeducer, IgnoresDynamicMmaps) {
  // Now, let's watch a binary hugepage_text itself.
  RepeatedPtrField<PerfEvent> events;
  AddMmap(6531, 0x560d76b25000, 0x24ce000, 0, "main", &events);
  events.rbegin()->set_timestamp(700413232676401);
  AddMmap(6531, 0x7f686a1ec000, 0x24000, 0, "ld.so", &events);
  events.rbegin()->set_timestamp(700413232691935);
  AddMmap(6531, 0x7ffea5dc8000, 0x2000, 0, "[vdso]", &events);
  events.rbegin()->set_timestamp(700413232701418);
  AddMmap(6531, 0x7f686a1e3000, 0x5000, 0, "lib1.so", &events);
  events.rbegin()->set_timestamp(700413232824216);
  AddMmap(6531, 0x7f686a1a8000, 0x3a000, 0, "lib2.so", &events);
  events.rbegin()->set_timestamp(700413232854520);
  AddMmap(6531, 0x7f6869ea7000, 0x5000, 0, "lib3.so", &events);
  events.rbegin()->set_timestamp(700413248827794);
  AddMmap(6531, 0x7f6867e00000, 0x200000, 0, "/anon_hugepage (deleted)",
          &events);
  events.rbegin()->set_timestamp(700413295816043);
  AddMmap(6531, 0x7f6867c00000, 0x200000, 0, "/anon_hugepage (deleted)",
          &events);
  events.rbegin()->set_timestamp(700413305947499);
  AddMmap(6531, 0x7f68663f8000, 0x1e00000, 0x7f68663f8000, "//anon", &events);
  events.rbegin()->set_timestamp(700413306012797);
  AddMmap(6531, 0x7f6866525000, 0x1a00000, 0x7f6866525000, "//anon", &events);
  events.rbegin()->set_timestamp(700413312132909);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(
      events,
      Pointwise(
          Partially(EqualsProto()),
          {
              "mmap_event: { pgoff: 0 filename: 'main' }",
              "mmap_event: { pgoff: 0 filename: 'ld.so' }",
              "mmap_event: { pgoff: 0 filename: '[vdso]' }",
              "mmap_event: { pgoff: 0 filename: 'lib1.so' }",
              "mmap_event: { pgoff: 0 filename: 'lib2.so' }",
              "mmap_event: { pgoff: 0 filename: 'lib3.so' }",
              "mmap_event: { pgoff: 0 filename: '/anon_hugepage (deleted)' }",
              "mmap_event: { pgoff: 0 filename: '/anon_hugepage (deleted)' }",
              "mmap_event: { pgoff: 0x7f68663f8000 filename: '//anon' }",
              "mmap_event: { pgoff: 0x7f6866525000 filename: '//anon' }",
          }));
}

TEST(HugePageDeducer, Regression117238226) {
  RepeatedPtrField<PerfEvent> events;
  AddMmap(10, 0x560334899000, 0x967000, 0, "main", &events);
  AddMmap(10, 0x560335200000, 0xe00000, 0, "/anon_hugepage", &events);
  AddMmap(10, 0x560336000000, 0x3800000, 0x1767000, "main", &events);
  AddMmap(10, 0x560339800000, 0x2600000, 0, "/anon_hugepage", &events);
  AddMmap(10, 0x56033be00000, 0xd3d000, 0x7567000, "main", &events);
  AddMmap(10, 0x7fab5e83e000, 0xb000, 0, "lib1.so", &events);

  DeduceHugePages(&events);
  CombineMappings(&events);

  EXPECT_THAT(
      events,
      Pointwise(Partially(EqualsProto()),
                {
                    "mmap_event: { pgoff: 0 filename: 'main' len: 0x82a4000 }",
                    "mmap_event: { pgoff: 0 filename: 'lib1.so', len: 0xb000 }",
                }));
}

TEST(HugePageDeducer, CombineMappings) {
  RepeatedPtrField<PerfEvent> events;
  // Interchange 2 sets of mmaps that will be combined.
  AddMmap(10, 0x1000, 0x1000, 0, "main1", &events);
  AddMmap(20, 0xA000, 0x2000, 0x1000, "main2", &events);
  AddMmap(10, 0x2000, 0x3000, 0x1000, "main1", &events);
  AddMmap(20, 0xC000, 0x1000, 0x3000, "main2", &events);
  AddMmapWithoutPid(0xD000, 0x1000, 0x3000, "main3", &events);
  AddMmapWithoutPid(0xE000, 0x1000, 0x3000, "main4", &events);
  AddMmap(10, 0x7f0000000000, 0xb000, 0, "lib1.so", &events);
  AddMmap(20, 0x7f0000000000, 0xb000, 0, "lib2.so", &events);

  CombineMappings(&events);

  EXPECT_THAT(events,
              Pointwise(Partially(EqualsProto()),
                        {
                            "mmap_event: { "
                            "pid: 10 start: 0x1000 "
                            "pgoff: 0 filename: 'main1' len: 0x4000 }",
                            "mmap_event: { "
                            "pid: 20 start: 0xA000 "
                            "pgoff: 0x1000 filename: 'main2' len: 0x3000 }",
                            "mmap_event: { "
                            "start: 0xD000 "
                            "pgoff: 0x3000 filename: 'main3', len: 0x1000 }",
                            "mmap_event: { "
                            "start: 0xE000 "
                            "pgoff: 0x3000 filename: 'main4', len: 0x1000 }",
                            "mmap_event: { "
                            "pid: 10 start: 0x7f0000000000 "
                            "pgoff: 0 filename: 'lib1.so', len: 0xb000 }",
                            "mmap_event: { "
                            "pid: 20 start: 0x7f0000000000 "
                            "pgoff: 0 filename: 'lib2.so', len: 0xb000 }",
                        }));
}

TEST(HugePageDeducer, CombineFileBackedAndAnonMappings) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_IP | PERF_SAMPLE_TID,
                                              true /*sample_id_all*/)
      .WriteTo(&input);

  // File backed mapping followed by anon with compatible protection.
  // Flag bits outside the MAP_TYPE mask are not relevant.
  testing::ExampleMmap2Event(10, 0x2000, 0x3000, 0x2000,
                             "/usr/compatible_prot/combinable_file_name",
                             testing::SampleInfo().Tid(10))
      .WithProtFlags(PROT_READ, MAP_PRIVATE | 0x1800)
      .WriteTo(&input);
  testing::ExampleMmap2Event(10, 0x5000, 0x1000, 0, "//anon",
                             testing::SampleInfo().Tid(10))
      .WithProtFlags(PROT_READ | PROT_WRITE, MAP_PRIVATE | 0x1000)
      .WriteTo(&input);

  // File backed mapping followed by anon with incompatible protection.
  testing::ExampleMmap2Event(20, 0x2000, 0x3000, 0x2000,
                             "/usr/incompatible_prot/combinable_file_name",
                             testing::SampleInfo().Tid(20))
      .WithProtFlags(PROT_READ, MAP_PRIVATE)
      .WriteTo(&input);
  testing::ExampleMmap2Event(20, 0x5000, 0x1000, 0, "//anon",
                             testing::SampleInfo().Tid(20))
      .WithProtFlags(PROT_READ | PROT_EXEC, MAP_PRIVATE)
      .WriteTo(&input);

  // File backed mapping followed by anon with execute protection.
  testing::ExampleMmap2Event(30, 0x2000, 0x3000, 0x2000,
                             "/usr/exec_prot/combinable_file_name",
                             testing::SampleInfo().Tid(30))
      .WithProtFlags(PROT_READ | PROT_EXEC, MAP_PRIVATE)
      .WriteTo(&input);
  testing::ExampleMmap2Event(30, 0x5000, 0x1000, 0, "//anon",
                             testing::SampleInfo().Tid(30))
      .WithProtFlags(PROT_READ | PROT_EXEC, MAP_PRIVATE)
      .WriteTo(&input);

  // File backed mapping followed by anon with incompatible sharing flags.
  testing::ExampleMmap2Event(40, 0x2000, 0x3000, 0x2000,
                             "/usr/incompatible_flags/combinable_file_name",
                             testing::SampleInfo().Tid(40))
      .WithProtFlags(PROT_READ | PROT_WRITE, MAP_PRIVATE)
      .WriteTo(&input);
  testing::ExampleMmap2Event(40, 0x5000, 0x1000, 0, "//anon",
                             testing::SampleInfo().Tid(40))
      .WithProtFlags(PROT_READ | PROT_WRITE, MAP_SHARED)
      .WriteTo(&input);

  // Non-combinable file backed mapping followed by anon.
  testing::ExampleMmap2Event(50, 0x2000, 0x3000, 0x2000,
                             "/dev/non_combinable_file_name",
                             testing::SampleInfo().Tid(50))
      .WriteTo(&input);
  testing::ExampleMmap2Event(50, 0x5000, 0x1000, 0, "//anon",
                             testing::SampleInfo().Tid(50))
      .WriteTo(&input);

  // File backed mapping followed by non VMA contiguous anon mapping.
  testing::ExampleMmap2Event(60, 0x2000, 0x3000, 0x2000,
                             "/usr/non_vma_contiguous_file_name",
                             testing::SampleInfo().Tid(60))
      .WriteTo(&input);
  testing::ExampleMmap2Event(60, 0x6000, 0x1000, 0, "//anon",
                             testing::SampleInfo().Tid(60))
      .WriteTo(&input);

  // Parse and combine mappings.
  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));
  EXPECT_EQ(12, reader.events().size());

  CombineMappings(reader.mutable_events());
  EXPECT_EQ(11, reader.events().size());

  EXPECT_THAT(
      reader.events(),
      Pointwise(Partially(EqualsProto()),
                {
                    "mmap_event: { "
                    "pid: 10 start: 0x2000 len: 0x4000 pgoff: 0x2000 "
                    "filename: '/usr/compatible_prot/combinable_file_name' }",
                    "mmap_event: { "
                    "pid: 20 start: 0x2000 len: 0x3000 pgoff: 0x2000 "
                    "filename: '/usr/incompatible_prot/combinable_file_name' }",
                    "mmap_event: { "
                    "pid: 20 start: 0x5000 len: 0x1000 pgoff: 0 "
                    "filename: '//anon' }",
                    "mmap_event: { "
                    "pid: 30 start: 0x2000 len: 0x3000 pgoff: 0x2000 "
                    "filename: '/usr/exec_prot/combinable_file_name' }",
                    "mmap_event: { "
                    "pid: 30 start: 0x5000 len: 0x1000 pgoff: 0 "
                    "filename: '//anon' }",
                    "mmap_event: { "
                    "pid: 40 start: 0x2000 len: 0x3000 pgoff: 0x2000 "
                    "filename: '/usr/incompatible_flags/combinable_file_name' "
                    "}",
                    "mmap_event: { "
                    "pid: 40 start: 0x5000 len: 0x1000 pgoff: 0 "
                    "filename: '//anon' }",
                    "mmap_event: { "
                    "pid: 50 start: 0x2000 len: 0x3000 pgoff: 0x2000 "
                    "filename: '/dev/non_combinable_file_name' }",
                    "mmap_event: { "
                    "pid: 50 start: 0x5000 len: 0x1000 pgoff: 0 "
                    "filename: '//anon' }",
                    "mmap_event: { "
                    "pid: 60 start: 0x2000 len: 0x3000 pgoff: 0x2000 "
                    "filename: '/usr/non_vma_contiguous_file_name' }",
                    "mmap_event: { "
                    "pid: 60 start: 0x6000 len: 0x1000 pgoff: 0 "
                    "filename: '//anon' }",
                }));

  // The following WriteToString command fails if the combined mmap events are
  // not well formed, with the correct size set in the header.
  std::string output;
  ASSERT_TRUE(reader.WriteToString(&output));
}

TEST(HugePageDeducer, CombineAnonAndFileBackedMappings) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_IP | PERF_SAMPLE_TID,
                                              true /*sample_id_all*/)
      .WriteTo(&input);

  // Anon and file backed mappings are not combined even when all address ranges
  // are contiguous and protections match.
  testing::ExampleMmap2Event(10, 0x2000, 0x3000, 0x2000, "//anon",
                             testing::SampleInfo().Tid(10))
      .WriteTo(&input);
  testing::ExampleMmap2Event(10, 0x5000, 0x1000, 0x5000,
                             "/usr/compatible_file_name",
                             testing::SampleInfo().Tid(10))
      .WriteTo(&input);

  // Parse and combine mappings.
  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));
  EXPECT_EQ(2, reader.events().size());

  CombineMappings(reader.mutable_events());
  EXPECT_EQ(2, reader.events().size());

  EXPECT_THAT(reader.events(),
              Pointwise(Partially(EqualsProto()),
                        {
                            "mmap_event: { "
                            "pid: 10 start: 0x2000 len: 0x3000 pgoff: 0x2000 "
                            "filename: '//anon' }",
                            "mmap_event: { "
                            "pid: 10 start: 0x5000 len: 0x1000 pgoff: 0x5000 "
                            "filename: '/usr/compatible_file_name' }",
                        }));

  // The following WriteToString command fails if the combined mmap events are
  // not well formed, with the correct size set in the header.
  std::string output;
  ASSERT_TRUE(reader.WriteToString(&output));
}

TEST(HugePageDeducer, CombineFileBackedMappings) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_IP | PERF_SAMPLE_TID,
                                              true /*sample_id_all*/)
      .WriteTo(&input);

  // Compatible file backed mappings.
  testing::ExampleMmap2Event(10, 0x2000, 0x3000, 0x2000,
                             "/usr/compatible_file_name",
                             testing::SampleInfo().Tid(10))
      .WriteTo(&input);
  testing::ExampleMmap2Event(10, 0x5000, 0x1000, 0x5000,
                             "/usr/compatible_file_name",
                             testing::SampleInfo().Tid(10))
      .WriteTo(&input);

  // File backed mappings with compatible protection.
  testing::ExampleMmap2Event(20, 0x2000, 0x3000, 0x2000,
                             "/usr/compatible_prot/combinable_file_name",
                             testing::SampleInfo().Tid(20))
      .WithProtFlags(PROT_READ, MAP_PRIVATE)
      .WriteTo(&input);
  testing::ExampleMmap2Event(20, 0x5000, 0x1000, 0x5000,
                             "/usr/compatible_prot/combinable_file_name",
                             testing::SampleInfo().Tid(20))
      .WithProtFlags(PROT_READ | PROT_WRITE, MAP_PRIVATE)
      .WriteTo(&input);

  // File backed mappings with incompatible protection.
  testing::ExampleMmap2Event(30, 0x2000, 0x3000, 0x2000,
                             "/usr/incompatible_prot/combinable_file_name",
                             testing::SampleInfo().Tid(30))
      .WithProtFlags(PROT_READ, MAP_PRIVATE)
      .WriteTo(&input);
  testing::ExampleMmap2Event(30, 0x5000, 0x1000, 0x5000,
                             "/usr/incompatible_prot/combinable_file_name",
                             testing::SampleInfo().Tid(30))
      .WithProtFlags(PROT_READ | PROT_EXEC, MAP_PRIVATE)
      .WriteTo(&input);

  // File backed mappings with incompatible sharing flags.
  testing::ExampleMmap2Event(40, 0x2000, 0x3000, 0x2000,
                             "/usr/incompatible_flags/combinable_file_name",
                             testing::SampleInfo().Tid(40))
      .WithProtFlags(PROT_READ | PROT_WRITE, MAP_PRIVATE)
      .WriteTo(&input);
  testing::ExampleMmap2Event(40, 0x5000, 0x1000, 0x5000,
                             "/usr/incompatible_flags/combinable_file_name",
                             testing::SampleInfo().Tid(40))
      .WithProtFlags(PROT_READ | PROT_WRITE, MAP_SHARED)
      .WriteTo(&input);

  // File backed mappings with exec protection and incompatible sharing flags.
  testing::ExampleMmap2Event(45, 0x2000, 0x3000, 0x2000,
                             "/usr/incompatible_flags/exec_prot_mapping",
                             testing::SampleInfo().Tid(40))
      .WithProtFlags(PROT_READ | PROT_EXEC, MAP_PRIVATE)
      .WriteTo(&input);
  testing::ExampleMmap2Event(45, 0x5000, 0x1000, 0x5000,
                             "/usr/incompatible_flags/exec_prot_mapping",
                             testing::SampleInfo().Tid(40))
      .WithProtFlags(PROT_READ | PROT_EXEC, MAP_SHARED)
      .WriteTo(&input);

  // Non-combinable file backed mappings.
  testing::ExampleMmap2Event(50, 0x2000, 0x3000, 0x2000,
                             "/dev/non_combinable_file_name",
                             testing::SampleInfo().Tid(50))
      .WriteTo(&input);
  testing::ExampleMmap2Event(50, 0x5000, 0x1000, 0x5000,
                             "/dev/non_combinable_file_name",
                             testing::SampleInfo().Tid(50))
      .WriteTo(&input);

  // File backed mappings without VMA contiguous addresses.
  testing::ExampleMmap2Event(60, 0x2000, 0x3000, 0x2000,
                             "/usr/non_vma_contiguous_file_name",
                             testing::SampleInfo().Tid(60))
      .WriteTo(&input);
  testing::ExampleMmap2Event(60, 0x6000, 0x1000, 0x5000,
                             "/usr/non_vma_contiguous_file_name",
                             testing::SampleInfo().Tid(60))
      .WriteTo(&input);

  // File backed mappings without file contiguous offsets.
  testing::ExampleMmap2Event(70, 0x2000, 0x3000, 0x2000,
                             "/usr/non_pgoff_contiguous_file_name",
                             testing::SampleInfo().Tid(70))
      .WriteTo(&input);
  testing::ExampleMmap2Event(70, 0x5000, 0x1000, 0x6000,
                             "/usr/non_pgoff_contiguous_file_name",
                             testing::SampleInfo().Tid(70))
      .WriteTo(&input);

  // File backed mappings with incompatible file names.
  testing::ExampleMmap2Event(80, 0x2000, 0x3000, 0x2000,
                             "/usr/incompatible_file_name1",
                             testing::SampleInfo().Tid(80))
      .WriteTo(&input);
  testing::ExampleMmap2Event(80, 0x5000, 0x1000, 0x5000,
                             "/usr/incompatible_file_name2",
                             testing::SampleInfo().Tid(80))
      .WriteTo(&input);

  // Parse and combine mappings.
  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));
  EXPECT_EQ(18, reader.events().size());

  CombineMappings(reader.mutable_events());
  EXPECT_EQ(15, reader.events().size());

  EXPECT_THAT(
      reader.events(),
      Pointwise(Partially(EqualsProto()),
                {
                    "mmap_event: { "
                    "pid: 10 start: 0x2000 len: 0x4000 pgoff: 0x2000 "
                    "filename: '/usr/compatible_file_name' }",
                    "mmap_event: { "
                    "pid: 20 start: 0x2000 len: 0x4000 pgoff: 0x2000 "
                    "filename: '/usr/compatible_prot/combinable_file_name' }",
                    "mmap_event: { "
                    "pid: 30 start: 0x2000 len: 0x3000 pgoff: 0x2000 "
                    "filename: '/usr/incompatible_prot/combinable_file_name' }",
                    "mmap_event: { "
                    "pid: 30 start: 0x5000 len: 0x1000 pgoff: 0x5000 "
                    "filename: '/usr/incompatible_prot/combinable_file_name' }",
                    "mmap_event: { "
                    "pid: 40 start: 0x2000 len: 0x3000 pgoff: 0x2000 "
                    "filename: '/usr/incompatible_flags/combinable_file_name' "
                    "}",
                    "mmap_event: { "
                    "pid: 40 start: 0x5000 len: 0x1000 pgoff: 0x5000 "
                    "filename: '/usr/incompatible_flags/combinable_file_name' "
                    "}",
                    "mmap_event: { "
                    "pid: 45 start: 0x2000 len: 0x4000 pgoff: 0x2000 "
                    "filename: '/usr/incompatible_flags/exec_prot_mapping' "
                    "}",
                    "mmap_event: { "
                    "pid: 50 start: 0x2000 len: 0x3000 pgoff: 0x2000 "
                    "filename: '/dev/non_combinable_file_name' }",
                    "mmap_event: { "
                    "pid: 50 start: 0x5000 len: 0x1000 pgoff: 0x5000 "
                    "filename: '/dev/non_combinable_file_name' }",
                    "mmap_event: { "
                    "pid: 60 start: 0x2000 len: 0x3000 pgoff: 0x2000 "
                    "filename: '/usr/non_vma_contiguous_file_name' }",
                    "mmap_event: { "
                    "pid: 60 start: 0x6000 len: 0x1000 pgoff: 0x5000 "
                    "filename: '/usr/non_vma_contiguous_file_name' }",
                    "mmap_event: { "
                    "pid: 70 start: 0x2000 len: 0x3000 pgoff: 0x2000 "
                    "filename: '/usr/non_pgoff_contiguous_file_name' }",
                    "mmap_event: { "
                    "pid: 70 start: 0x5000 len: 0x1000 pgoff: 0x6000 "
                    "filename: '/usr/non_pgoff_contiguous_file_name' }",
                    "mmap_event: { "
                    "pid: 80 start: 0x2000 len: 0x3000 pgoff: 0x2000 "
                    "filename: '/usr/incompatible_file_name1' }",
                    "mmap_event: { "
                    "pid: 80 start: 0x5000 len: 0x1000 pgoff: 0x5000 "
                    "filename: '/usr/incompatible_file_name2' }",
                }));

  // The following WriteToString command fails if the combined mmap events are
  // not well formed, with the correct size set in the header.
  std::string output;
  ASSERT_TRUE(reader.WriteToString(&output));
}

TEST(HugePageDeducer, CombineAnonMappings) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_IP | PERF_SAMPLE_TID,
                                              true /*sample_id_all*/)
      .WriteTo(&input);

  // Anon mappings are not combined even when all address ranges are contiguous
  // and protections match.
  testing::ExampleMmap2Event(10, 0x2000, 0x3000, 0x2000, "//anon",
                             testing::SampleInfo().Tid(10))
      .WriteTo(&input);
  testing::ExampleMmap2Event(10, 0x5000, 0x1000, 0x5000, "//anon",
                             testing::SampleInfo().Tid(10))
      .WriteTo(&input);

  // Parse and combine mappings.
  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));
  EXPECT_EQ(2, reader.events().size());

  CombineMappings(reader.mutable_events());
  EXPECT_EQ(2, reader.events().size());

  EXPECT_THAT(reader.events(),
              Pointwise(Partially(EqualsProto()),
                        {
                            "mmap_event: { "
                            "pid: 10 start: 0x2000 len: 0x3000 pgoff: 0x2000 "
                            "filename: '//anon' }",
                            "mmap_event: { "
                            "pid: 10 start: 0x5000 len: 0x1000 pgoff: 0x5000 "
                            "filename: '//anon' }",
                        }));

  // The following WriteToString command fails if the combined mmap events are
  // not well formed, with the correct size set in the header.
  std::string output;
  ASSERT_TRUE(reader.WriteToString(&output));
}

}  // namespace
}  // namespace quipper
