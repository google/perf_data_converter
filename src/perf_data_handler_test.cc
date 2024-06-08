/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/perf_data_handler.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "src/path_matching.h"
#include "src/quipper/arm_spe_decoder.h"
#include "src/quipper/binary_data_utils.h"
#include "src/quipper/kernel/perf_event.h"
#include "src/quipper/kernel/perf_internals.h"
#include "src/quipper/perf_buildid.h"
#include "src/quipper/test_utils.h"

using BranchStackEntry = quipper::PerfDataProto::BranchStackEntry;

namespace perftools {

TEST(PathMatching, DeletedSharedObjectMatching) {
  const std::vector<std::string> paths = {
      "lib.so.v1(deleted)",
      "lib.so.v1(deleted)junk",
      "lib.so (deleted)",
      "lib.so_junk_(deleted)",
      "lib.so   .so junk_(deleted)",
  };
  for (const auto& path : paths) {
    ASSERT_TRUE(IsDeletedSharedObject(path));
  }
}

TEST(PathMatching, DeletedSharedObjectNotMatching) {
  const std::vector<std::string> paths = {
      "abc",
      "lib.so ",
      "lib.so(deleted)",
      ".so (deleted)",
      "lib.sojunk(deleted)",
      "",
  };

  for (const auto& path : paths) {
    ASSERT_FALSE(IsDeletedSharedObject(path));
  }
}

TEST(PathMatching, VersionedSharedObjectMatching) {
  const std::vector<std::string> paths = {
      "lib.so.",
      "lib.so.abc",
      "lib.so.1",
      "lib.so.v1",
  };
  for (const auto& path : paths) {
    ASSERT_TRUE(IsVersionedSharedObject(path));
  }
}

TEST(PathMatching, VersionedSharedObjectNotMatching) {
  const std::vector<std::string> paths = {
      "abc", "lib.so(deleted)", ".so.v1", ".so.", "",
  };
  for (const auto& path : paths) {
    ASSERT_FALSE(IsDeletedSharedObject(path));
  }
}

class PerfDataHandlerTest : public ::testing::Test {
 protected:
  PerfDataHandlerTest() {}
};

class TestPerfDataHandler : public PerfDataHandler {
 public:
  TestPerfDataHandler(std::vector<BranchStackEntry> expected_branch_stack,
                      std::unordered_map<std::string, std::string>
                          expected_filename_to_build_id)
      : expected_branch_stack_(std::move(expected_branch_stack)),
        expected_filename_to_build_id_(
            std::move(expected_filename_to_build_id)) {}
  TestPerfDataHandler(const TestPerfDataHandler&) = delete;
  TestPerfDataHandler& operator=(const TestPerfDataHandler&) = delete;
  ~TestPerfDataHandler() override {}

  // Callbacks for PerfDataHandler
  void Sample(const SampleContext& sample) override {
    seen_sample_events_.push_back(sample.sample);
    if (sample.addr_mapping != nullptr) {
      const Mapping* m = sample.addr_mapping;
      seen_addr_mappings_.push_back(std::unique_ptr<Mapping>(
          new Mapping(m->filename, m->build_id, m->start, m->limit,
                      m->file_offset, m->filename_md5_prefix)));
    } else {
      seen_addr_mappings_.push_back(nullptr);
    }
    EXPECT_EQ(expected_branch_stack_.size(), sample.branch_stack.size());
    for (size_t i = 0; i < sample.branch_stack.size(); i++) {
      CheckBranchEquality(expected_branch_stack_[i], sample.branch_stack[i]);
    }
    if (sample.spe.is_spe) {
      seen_arm_spe_records_.push_back(sample.spe.record);
    }
  }
  void Comm(const CommContext& comm) override {}
  void MMap(const MMapContext& mmap) override {
    std::string actual_build_id = mmap.mapping->build_id.value;
    std::string actual_filename = mmap.mapping->filename;
    const auto expected_build_id_it =
        expected_filename_to_build_id_.find(actual_filename);
    if (expected_build_id_it != expected_filename_to_build_id_.end()) {
      EXPECT_EQ(actual_build_id, expected_build_id_it->second)
          << "Build ID mismatch for the filename " << actual_filename;
      seen_filenames_.insert(actual_filename);
    }
  }

  void CheckSeenFilenames() {
    EXPECT_EQ(expected_filename_to_build_id_.size(), seen_filenames_.size());
    for (auto const& filename : seen_filenames_) {
      EXPECT_TRUE(expected_filename_to_build_id_.find(filename) !=
                  expected_filename_to_build_id_.end());
    }
  }

  void CheckNotEmptyFilenames() {
    EXPECT_EQ(1, seen_filenames_.size());
    for (auto const& filename : seen_filenames_) {
      EXPECT_TRUE(expected_filename_to_build_id_.find(filename) !=
                  expected_filename_to_build_id_.end());
    }
  }

  const std::vector<std::unique_ptr<Mapping>>& SeenAddrMappings() const {
    return seen_addr_mappings_;
  }

  const std::vector<quipper::PerfDataProto::SampleEvent>& SeenSampleEvents()
      const {
    return seen_sample_events_;
  }

  const std::vector<quipper::ArmSpeDecoder::Record>& SeenArmSpeRecords() const {
    return seen_arm_spe_records_;
  }

 private:
  // Ensure necessary information contained in the BranchStackEntry is also
  // present in the resulting profile.
  inline void CheckBranchEquality(BranchStackEntry expected,
                                  BranchStackPair actual) {
    EXPECT_EQ(expected.from_ip(), actual.from.ip);
    EXPECT_EQ(expected.to_ip(), actual.to.ip);
    EXPECT_EQ(expected.mispredicted(), actual.mispredicted);
    EXPECT_EQ(expected.predicted(), actual.predicted);
    EXPECT_EQ(expected.in_transaction(), actual.in_transaction);
    EXPECT_EQ(expected.abort(), actual.abort);
    EXPECT_EQ(expected.cycles(), actual.cycles);
  }
  std::vector<BranchStackEntry> expected_branch_stack_;
  std::unordered_map<std::string, std::string> expected_filename_to_build_id_;
  std::unordered_set<std::string> seen_filenames_;
  std::vector<std::unique_ptr<Mapping>> seen_addr_mappings_;
  std::vector<quipper::PerfDataProto::SampleEvent> seen_sample_events_;
  std::vector<quipper::ArmSpeDecoder::Record> seen_arm_spe_records_;
};

TEST(PerfDataHandlerTest, KernelBuildIdWithDifferentFilename) {
  const int kHexCharsPerByte = 2;
  quipper::PerfDataProto proto;
  // Add a guest kernel build id.
  auto* build_id = proto.add_build_ids();
  build_id->set_misc(quipper::PERF_RECORD_MISC_GUEST_KERNEL);
  build_id->set_pid(-1);
  build_id->set_filename("[guest.kernel.kallsyms]");
  build_id->set_filename_md5_prefix(
      quipper::Md5Prefix("[guest.kernel.kallsyms]"));

  std::string guest_kernel_build_id = "27357e645f";
  uint8_t guest_kernel_build_id_bytes[quipper::kBuildIDArraySize];
  ASSERT_TRUE(quipper::HexStringToRawData(guest_kernel_build_id,
                                          guest_kernel_build_id_bytes,
                                          sizeof(guest_kernel_build_id_bytes)));
  build_id->set_build_id_hash(guest_kernel_build_id_bytes,
                              guest_kernel_build_id.size() / kHexCharsPerByte);

  // Add kernel build id with a different filename.
  build_id = proto.add_build_ids();
  build_id->set_misc(quipper::PERF_RECORD_MISC_KERNEL);
  build_id->set_pid(-1);
  build_id->set_filename("kernel");
  build_id->set_filename_md5_prefix(quipper::Md5Prefix("kernel"));

  std::string kernel_build_id = "17937e648e";
  uint8_t kernel_build_id_bytes[quipper::kBuildIDArraySize];
  ASSERT_TRUE(quipper::HexStringToRawData(
      kernel_build_id, kernel_build_id_bytes, sizeof(kernel_build_id_bytes)));
  build_id->set_build_id_hash(kernel_build_id_bytes,
                              kernel_build_id.size() / kHexCharsPerByte);

  // Add a non-kernel build id.
  build_id = proto.add_build_ids();
  build_id->set_misc(quipper::PERF_RECORD_MISC_USER);
  build_id->set_pid(7568);
  build_id->set_filename("chrome");
  build_id->set_filename_md5_prefix(quipper::Md5Prefix("chrome"));

  std::string chrome_build_id = "cac4b36db4d0";
  uint8_t chrome_build_id_bytes[quipper::kBuildIDArraySize];
  ASSERT_TRUE(quipper::HexStringToRawData(
      chrome_build_id, chrome_build_id_bytes, sizeof(chrome_build_id_bytes)));
  build_id->set_build_id_hash(chrome_build_id_bytes,
                              chrome_build_id.size() / kHexCharsPerByte);

  // Add MMaps for kernel and non-kernel filenames.
  auto* mmap_event = proto.add_events()->mutable_mmap_event();
  mmap_event->set_filename("[kernel.kallsyms]");
  mmap_event->set_pid(100);
  mmap_event->set_tid(100);
  mmap_event->set_start(0);
  mmap_event->set_len(20000);
  mmap_event->set_pgoff(4000);

  mmap_event = proto.add_events()->mutable_mmap_event();
  mmap_event->set_filename("chrome");
  mmap_event->set_pid(100);
  mmap_event->set_tid(100);
  mmap_event->set_start(1000);
  mmap_event->set_len(1000);
  mmap_event->set_pgoff(0);

  mmap_event = proto.add_events()->mutable_mmap_event();
  mmap_event->set_filename("unknown");
  mmap_event->set_pid(3400);
  mmap_event->set_tid(3400);
  mmap_event->set_start(2000);
  mmap_event->set_len(1000);
  mmap_event->set_pgoff(0);

  // Map of expected filenames to buildids. Unknown is expected not to have a
  // build id. Since the guest kernel doesn't have a mapping, it is not expected
  // to appear in the MMap() call.
  std::unordered_map<std::string, std::string> expected_filename_to_build_id;
  expected_filename_to_build_id["[kernel.kallsyms]"] = "17937e648e";
  expected_filename_to_build_id["chrome"] = "cac4b36db4d0";

  TestPerfDataHandler handler(std::vector<BranchStackEntry>(),
                              expected_filename_to_build_id);
  PerfDataHandler::Process(proto, &handler);
  handler.CheckSeenFilenames();
}

TEST(PerfDataHandlerTest, SampleBranchStackMatches) {
  quipper::PerfDataProto proto;

  // File attrs are required for sample event processing.
  uint64_t file_attr_id = 0;
  auto* file_attr = proto.add_file_attrs();
  file_attr->add_ids(file_attr_id);

  auto* sample_event = proto.add_events()->mutable_sample_event();
  sample_event->set_ip(123);
  sample_event->set_pid(5805);
  sample_event->set_tid(5805);
  sample_event->set_sample_time_ns(456);
  sample_event->set_period(1);
  sample_event->set_id(file_attr_id);
  auto* entry = sample_event->add_branch_stack();
  std::vector<BranchStackEntry> branch_stack;

  // Create 2 branch stack entries.
  entry->set_from_ip(101);
  entry->set_to_ip(102);
  entry->set_mispredicted(false);
  entry->set_predicted(true);
  entry->set_in_transaction(false);
  entry->set_abort(false);
  entry->set_cycles(4);
  branch_stack.push_back(*entry);

  entry = sample_event->add_branch_stack();
  entry->set_from_ip(103);
  entry->set_to_ip(104);
  entry->set_mispredicted(false);
  entry->set_predicted(true);
  entry->set_in_transaction(false);
  entry->set_abort(false);
  entry->set_cycles(5);
  branch_stack.push_back(*entry);

  TestPerfDataHandler handler(branch_stack,
                              std::unordered_map<std::string, std::string>());
  PerfDataHandler::Process(proto, &handler);
}

TEST(PerfDataHandlerTest, AddressMappingIsSet) {
  quipper::PerfDataProto proto;

  // File attrs are required for sample event processing.
  uint64_t file_attr_id = 0;
  auto* file_attr = proto.add_file_attrs();
  file_attr->add_ids(file_attr_id);

  // Add a couple of mapping events, one includes the data address.
  auto mmap_event = proto.add_events()->mutable_mmap_event();
  mmap_event->set_filename("/foo/bar");
  mmap_event->set_pid(100);
  mmap_event->set_tid(100);
  mmap_event->set_start(0x1000);
  mmap_event->set_len(0x1000);
  mmap_event->set_pgoff(0);

  mmap_event = proto.add_events()->mutable_mmap_event();
  mmap_event->set_filename("/foo/baz");
  mmap_event->set_pid(100);
  mmap_event->set_tid(100);
  mmap_event->set_start(0x3000);
  mmap_event->set_len(0x1000);
  mmap_event->set_pgoff(0x1000);

  // Add sample events without and with data addresses.
  auto* sample_event = proto.add_events()->mutable_sample_event();
  sample_event->set_ip(123);
  sample_event->set_pid(100);
  sample_event->set_tid(100);
  // This event has no data address.
  sample_event->set_sample_time_ns(456);
  sample_event->set_period(1);
  sample_event->set_id(file_attr_id);

  sample_event = proto.add_events()->mutable_sample_event();
  sample_event->set_ip(300);
  sample_event->set_pid(100);
  sample_event->set_tid(100);
  sample_event->set_addr(0x3100);
  sample_event->set_sample_time_ns(567);
  sample_event->set_period(1);
  sample_event->set_id(file_attr_id);

  TestPerfDataHandler handler(std::vector<BranchStackEntry>{},
                              std::unordered_map<std::string, std::string>{});
  PerfDataHandler::Process(proto, &handler);
  auto& addr_mappings = handler.SeenAddrMappings();
  // We expect two elements, one nullptr and the other with /foo/baz info.
  EXPECT_EQ(2u, addr_mappings.size());
  EXPECT_EQ(nullptr, addr_mappings[0]);
  const PerfDataHandler::Mapping* mapping = addr_mappings[1].get();
  ASSERT_TRUE(mapping != nullptr);
  EXPECT_EQ("/foo/baz", mapping->filename);
  EXPECT_EQ(0x3000, mapping->start);
  EXPECT_EQ(0x4000, mapping->limit);
  EXPECT_EQ(0x1000, mapping->file_offset);
}

TEST(PerfDataHandlerTest, MappingBuildIdAndSourceAreSet) {
  quipper::PerfDataProto proto;

  // File attrs are required for sample event processing.
  uint64_t file_attr_id = 0;
  auto* file_attr = proto.add_file_attrs();
  file_attr->add_ids(file_attr_id);

  // Add a buildid-mmap event.
  auto* mmap_event = proto.add_events()->mutable_mmap_event();
  mmap_event->set_filename("/usr/lib/foo");
  mmap_event->set_pid(100);
  mmap_event->set_tid(100);
  mmap_event->set_start(0x1000);
  mmap_event->set_len(0x500);
  mmap_event->set_pgoff(0);
  mmap_event->set_build_id("abcdef0001");

  // Add a non-buildid-mmap event.
  mmap_event = proto.add_events()->mutable_mmap_event();
  mmap_event->set_filename("/usr/lib/foo");
  mmap_event->set_pid(100);
  mmap_event->set_tid(100);
  mmap_event->set_start(0x3000);
  mmap_event->set_len(0x500);
  mmap_event->set_pgoff(0x1000);

  // Add a sample event whose address is in the range of the first mmap.
  auto* sample_event = proto.add_events()->mutable_sample_event();
  sample_event->set_ip(200);
  sample_event->set_pid(100);
  sample_event->set_tid(100);
  sample_event->set_addr(0x1010);

  // Add a sample event whose address is in the range of the second mmap.
  sample_event = proto.add_events()->mutable_sample_event();
  sample_event->set_ip(200);
  sample_event->set_pid(100);
  sample_event->set_tid(100);
  sample_event->set_addr(0x3010);

  TestPerfDataHandler handler(std::vector<BranchStackEntry>{},
                              std::unordered_map<std::string, std::string>{});
  PerfDataHandler::Process(proto, &handler);
  auto& mappings = handler.SeenAddrMappings();

  // Expect mmap events have their build ID and sources set.
  EXPECT_EQ(mappings[0]->build_id.value, "abcdef0001");
  EXPECT_EQ(mappings[0]->build_id.source, kBuildIdMmapDiffFilename);
  EXPECT_EQ(mappings[1]->build_id.value, "");
  EXPECT_EQ(mappings[1]->build_id.source, kBuildIdMissing);
}

TEST(PerfDataHandlerTest, LostSampleEventsAreHandledInNewerPerf) {
  for (auto perf_version :
       {"7.0.93.934.asdfa.avava", "6.1.456", "6.123-google"}) {
    quipper::PerfDataProto proto;

    // File attrs are required for sample event processing.
    auto* file_attr = proto.add_file_attrs();
    file_attr->add_ids(1);
    file_attr->add_ids(2);
    proto.add_file_attrs();

    // Set the perf_version to a version that supports counting lost samples
    // from lost_samples_event.
    proto.mutable_string_metadata()->mutable_perf_version()->set_value(
        perf_version);

    // Add a lost_samples_event.
    auto* lost_samples_event = proto.add_events()->mutable_lost_samples_event();
    lost_samples_event->mutable_sample_info()->set_id(1);
    lost_samples_event->mutable_sample_info()->set_pid(100);
    lost_samples_event->mutable_sample_info()->set_tid(100);
    lost_samples_event->set_num_lost(5);

    // Add a lost_event, which should not be handled in perf data with newer
    // version.
    auto* lost_event = proto.add_events()->mutable_lost_event();
    lost_event->mutable_sample_info()->set_id(2);
    lost_event->mutable_sample_info()->set_pid(100);
    lost_event->mutable_sample_info()->set_tid(100);
    lost_event->set_lost(10);

    // Add a lost_samples_event, which should not be handled if the sample ID is
    // not in the corresponding file attribute.
    auto* lost_samples_event2 =
        proto.add_events()->mutable_lost_samples_event();
    lost_samples_event2->mutable_sample_info()->set_id(3);
    lost_samples_event2->mutable_sample_info()->set_pid(100);
    lost_samples_event2->mutable_sample_info()->set_tid(100);
    lost_samples_event2->set_num_lost(3);

    TestPerfDataHandler handler(std::vector<BranchStackEntry>{},
                                std::unordered_map<std::string, std::string>{});
    PerfDataHandler::Process(proto, &handler);

    EXPECT_EQ(handler.SeenAddrMappings().size(), 5)
        << " perf_version:" << perf_version;
  }
}

TEST(PerfDataHandlerTest, LostEventsAreHandledInOlderPerf) {
  for (auto perf_version : {"6.0.456", "random-string", ""}) {
    quipper::PerfDataProto proto;

    // File attrs are required for sample event processing.
    auto* file_attr = proto.add_file_attrs();
    file_attr->add_ids(1);
    file_attr->add_ids(2);

    // Set the perf_version to a version that supports counting lost samples
    // from lost_samples_event.
    proto.mutable_string_metadata()->mutable_perf_version()->set_value(
        perf_version);

    // Add a lost_samples_event, which should not be handled in perf data with
    // older version
    auto* lost_samples_event = proto.add_events()->mutable_lost_samples_event();
    lost_samples_event->mutable_sample_info()->set_id(1);
    lost_samples_event->mutable_sample_info()->set_pid(100);
    lost_samples_event->mutable_sample_info()->set_tid(100);
    lost_samples_event->set_num_lost(5);

    // Add a lost_event, which should be handled in perf data with older
    // version.
    auto* lost_event = proto.add_events()->mutable_lost_event();
    lost_event->mutable_sample_info()->set_id(2);
    lost_event->mutable_sample_info()->set_pid(100);
    lost_event->mutable_sample_info()->set_tid(100);
    lost_event->set_lost(10);

    TestPerfDataHandler handler(std::vector<BranchStackEntry>{},
                                std::unordered_map<std::string, std::string>{});
    PerfDataHandler::Process(proto, &handler);

    EXPECT_EQ(handler.SeenAddrMappings().size(), 10)
        << " perf_version: " << perf_version;
  }
}

TEST(PerfDataHandlerTest, SpeAuxtraceIntoSamples) {
  quipper::PerfDataProto proto;

  // File attrs are required for sample event processing.
  uint64_t file_attr_id = 0;
  auto* file_attr = proto.add_file_attrs();
  file_attr->add_ids(file_attr_id);

  // Add a fork and a comm events for tid->pid mapping .
  auto* fork = proto.add_events()->mutable_fork_event();
  fork->set_tid(0x5f80);
  fork->set_pid(0x1);
  auto* comm = proto.add_events()->mutable_comm_event();
  comm->set_tid(0xe);
  comm->set_pid(2);

  // Add an auxtrace info event.
  proto.add_events()->mutable_auxtrace_info_event()->set_type(
      quipper::PERF_AUXTRACE_ARM_SPE);

  // Add an auxtrace event.
  auto* auxtrace_event = proto.add_events()->mutable_auxtrace_event();
  std::string trace_data = quipper::GenerateBinaryTrace({
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
  });
  auxtrace_event->set_trace_data(trace_data);

  TestPerfDataHandler handler(std::vector<BranchStackEntry>{},
                              std::unordered_map<std::string, std::string>{});
  PerfDataHandler::Process(proto, &handler);

  const auto& sample_events = handler.SeenSampleEvents();
  ASSERT_EQ(sample_events.size(), 2);
  // Match to the correct pids.
  EXPECT_EQ(sample_events[0].pid(), 1);
  EXPECT_EQ(sample_events[1].pid(), 2);

  const auto& spe_records = handler.SeenArmSpeRecords();
  ASSERT_EQ(spe_records.size(), 2);
  EXPECT_EQ(spe_records[0].total_lat, 12);
  EXPECT_EQ(spe_records[0].issue_lat, 4);
  EXPECT_EQ(spe_records[0].translation_lat, 1);
  EXPECT_EQ(spe_records[1].total_lat, 17);
  EXPECT_EQ(spe_records[1].issue_lat, 16);
}

TEST(PerfDataHandlerTest, KsymbolIntoMappings) {
  quipper::PerfDataProto proto;
  std::string mock_filename = "bpf_prog_bec4c5629f7c7e2d_netcg_bind4";

  // File attrs are required for sample event processing.
  uint64_t file_attr_id = 0;
  auto* file_attr = proto.add_file_attrs();
  file_attr->add_ids(file_attr_id);

  // Add a ksymbol event.
  auto* ksymbol_event = proto.add_events()->mutable_ksymbol_event();
  ksymbol_event->set_addr(0x1000);
  ksymbol_event->set_len(0x500);
  ksymbol_event->set_ksym_type(quipper::PERF_RECORD_KSYMBOL_TYPE_BPF);
  ksymbol_event->set_flags(0);
  ksymbol_event->set_name(mock_filename);

  std::unordered_map<std::string, std::string> expected_filename_to_build_id;
  expected_filename_to_build_id[mock_filename] = "";
  TestPerfDataHandler handler(std::vector<BranchStackEntry>{},
                              expected_filename_to_build_id);
  PerfDataHandler::Process(proto, &handler);
  handler.CheckSeenFilenames();
}

}  // namespace perftools

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
