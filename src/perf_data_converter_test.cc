/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// Tests converting perf.data files to sets of Profile

#include "src/perf_data_converter.h"

#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/compat/test_compat.h"
#include "src/intervalmap.h"
#include "src/perf_data_handler.h"
#include "src/quipper/perf_parser.h"
#include "src/quipper/perf_reader.h"

using perftools::ProcessProfiles;
using perftools::profiles::Location;
using perftools::profiles::Mapping;
using quipper::PerfDataProto;
using testing::Contains;
using testing::Eq;
using testing::IsEmpty;
using testing::UnorderedPointwise;

namespace {

typedef std::unordered_map<std::string, std::pair<int64_t, int64_t>> MapCounts;

// GetMapCounts returns a map keyed by a location identifier and
// mapping to self and total counts for that location.
MapCounts GetMapCounts(const ProcessProfiles& pps) {
  MapCounts map_counts;
  for (const auto& pp : pps) {
    const auto& profile = pp->data;
    std::unordered_map<uint64_t, const Location*> locations;
    perftools::IntervalMap<const Mapping*> mappings;
    CHECK_GT(profile.mapping_size(), 0);
    const Mapping& main = profile.mapping(0);
    for (const auto& mapping : profile.mapping()) {
      mappings.Set(mapping.memory_start(), mapping.memory_limit(), &mapping);
    }
    for (const auto& location : profile.location()) {
      locations[location.id()] = &location;
    }
    for (int i = 0; i < profile.sample_size(); ++i) {
      const auto& sample = profile.sample(i);
      for (int id_index = 0; id_index < sample.location_id_size(); ++id_index) {
        uint64_t id = sample.location_id(id_index);
        CHECK(locations[id] != nullptr);
        std::stringstream key_stream;
        key_stream << profile.string_table(main.filename()) << ":"
                   << profile.string_table(main.build_id());
        if (locations[id]->mapping_id() != 0) {
          const Mapping* dso;
          uint64_t addr = locations[id]->address();
          CHECK(mappings.Lookup(addr, &dso));
          key_stream << "+" << profile.string_table(dso->filename()) << ":"
                     << profile.string_table(dso->build_id()) << std::hex
                     << (addr - dso->memory_start());
        }
        const auto& key = key_stream.str();
        auto count = map_counts[key];
        if (id_index == 0) {
          // Exclusive.
          ++count.first;
        } else {
          // Inclusive.
          ++count.second;
        }
        map_counts[key] = count;
      }
    }
  }
  return map_counts;
}

std::unordered_set<std::string> AllBuildIDs(const ProcessProfiles& pps) {
  std::unordered_set<std::string> ret;
  for (const auto& pp : pps) {
    for (const auto& it : pp->data.mapping()) {
      ret.insert(pp->data.string_table(it.build_id()));
    }
  }
  return ret;
}

std::unordered_set<std::string> AllComments(const ProcessProfiles& pps) {
  std::unordered_set<std::string> ret;
  for (const auto& pp : pps) {
    for (const auto& it : pp->data.comment()) {
      ret.insert(pp->data.string_table(it));
    }
  }
  return ret;
}

std::unordered_set<std::string> AllMmapsWithBuildIDs(
    const ProcessProfiles& pps) {
  std::unordered_set<std::string> ret;
  for (const auto& pp : pps) {
    for (const auto& it : pp->data.mapping()) {
      if (pp->data.string_table(it.build_id()).empty()) {
        continue;
      }
      ret.insert(pp->data.string_table(it.filename()));
    }
  }
  return ret;
}

class ProfileIndexer {
 public:
  ProfileIndexer(const perftools::profiles::Profile& profile)
      : profile_(profile) {
    for (uint64_t i = 0; i < profile_.location().size(); ++i) {
      addr_to_loc_i_[profile_.location().Get(i).address()] = i;
    }
    for (uint64_t i = 0; i < profile_.mapping().size(); ++i) {
      id_to_mapping_i_[profile_.mapping().Get(i).id()] = i;
    }
  }

  // GetBuildId returns the build ID by the location address in a profile.
  std::string GetBuildId(uint64_t addr) {
    if (addr_to_loc_i_.find(addr) == addr_to_loc_i_.end()) {
      return "";
    }
    const auto& location = profile_.location().Get(addr_to_loc_i_[addr]);
    const auto& mapping =
        profile_.mapping().Get(id_to_mapping_i_[location.mapping_id()]);
    return profile_.string_table(mapping.build_id());
  }

  const perftools::profiles::Profile& profile_;
  std::unordered_map<uint64_t, uint64_t> addr_to_loc_i_;
  std::unordered_map<uint64_t, uint64_t> id_to_mapping_i_;
};
}  // namespace

namespace perftools {

// Reads and returns the content of the file at path, or empty string on error.
std::string GetContents(const std::string& path) {
  std::ifstream ifs(path, std::ifstream::binary);
  if (!ifs) return "";
  std::stringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}

// Gets the string after the last '/' or returns the entire string if there are
// no slashes.
inline std::string Basename(const std::string& path) {
  return path.substr(path.find_last_of('/'));
}

std::string GetResource(const std::string& relpath) {
  return "src/testdata/" + relpath;
}

PerfDataProto ToPerfDataProto(const std::string& raw_perf_data) {
  std::unique_ptr<quipper::PerfReader> reader(new quipper::PerfReader);
  EXPECT_TRUE(reader->ReadFromString(raw_perf_data));

  std::unique_ptr<quipper::PerfParser> parser;
  parser.reset(new quipper::PerfParser(reader.get()));
  EXPECT_TRUE(parser->ParseRawEvents());

  PerfDataProto perf_data_proto;
  EXPECT_TRUE(reader->Serialize(&perf_data_proto));
  return perf_data_proto;
}

class PerfDataConverterTest : public ::testing::Test {
 protected:
  PerfDataConverterTest() {}
};

struct TestCase {
  std::string filename;
  int64_t key_count;
  int64_t total_exclusive;
  int64_t total_inclusive;
};

// Builds a set of counts for each sample in the profile.  This is a
// very high-level test -- major changes in the values should
// be validated via manual inspection of new golden values.
TEST_F(PerfDataConverterTest, Converts) {
  std::string single_profile(
      GetResource("single-event-single-process.perf.data"));
  std::string multi_pid_profile(
      GetResource("single-event-multi-process.perf.data"));
  std::string multi_event_profile(
      GetResource("multi-event-single-process.perf.data"));
  std::string stack_profile(GetResource("with-callchain.perf.data"));

  std::vector<TestCase> cases;
  cases.emplace_back(TestCase{single_profile, 1061, 1061, 0});
  cases.emplace_back(TestCase{multi_pid_profile, 442, 730, 0});
  cases.emplace_back(TestCase{multi_event_profile, 1124, 1124, 0});
  cases.emplace_back(TestCase{stack_profile, 1138, 1210, 2247});

  for (const auto& c : cases) {
    std::string casename = "case " + Basename(c.filename);
    std::string raw_perf_data = GetContents(c.filename);
    ASSERT_FALSE(raw_perf_data.empty()) << c.filename;
    // Test RawPerfData input.
    auto pps = RawPerfDataToProfiles(
        reinterpret_cast<const void*>(raw_perf_data.c_str()),
        raw_perf_data.size(), {}, kNoLabels, kNoOptions);
    // Does not group by PID, Vector should only contain one element
    EXPECT_EQ(pps.size(), 1);
    auto counts = GetMapCounts(pps);
    EXPECT_EQ(c.key_count, counts.size()) << casename;
    int64_t total_exclusive = 0;
    int64_t total_inclusive = 0;
    for (const auto& it : counts) {
      total_exclusive += it.second.first;
      total_inclusive += it.second.second;
    }
    EXPECT_EQ(c.total_exclusive, total_exclusive) << casename;
    EXPECT_EQ(c.total_inclusive, total_inclusive) << casename;

    // Test PerfDataProto input.
    const auto perf_data_proto = ToPerfDataProto(raw_perf_data);
    pps = PerfDataProtoToProfiles(
        &perf_data_proto, kNoLabels, kNoOptions);
    counts = GetMapCounts(pps);
    EXPECT_EQ(c.key_count, counts.size()) << casename;
    total_exclusive = 0;
    total_inclusive = 0;
    for (const auto& it : counts) {
      total_exclusive += it.second.first;
      total_inclusive += it.second.second;
    }
    EXPECT_EQ(c.total_exclusive, total_exclusive) << casename;
    EXPECT_EQ(c.total_inclusive, total_inclusive) << casename;
  }
}

TEST_F(PerfDataConverterTest, ConvertsGroupPid) {
  std::string multiple_profile(
      GetResource("single-event-multi-process.perf.data"));

  // Fetch the stdout_injected result and emit it to a profile.proto.  Group by
  // PIDs so the inner vector will have multiple entries.
  std::string raw_perf_data = GetContents(multiple_profile);
  ASSERT_FALSE(raw_perf_data.empty()) << multiple_profile;
  // Test PerfDataProto input.
  const auto perf_data_proto = ToPerfDataProto(raw_perf_data);
  const auto pps = PerfDataProtoToProfiles(
      &perf_data_proto, kPidAndTidLabels, kGroupByPids);

  uint64_t total_samples = 0;
  // Samples were collected for 6 pids in this case, so the outer vector should
  // contain 6 profiles, one for each pid.
  int pids = 6;
  EXPECT_EQ(pids, pps.size());
  for (const auto& per_thread : pps) {
    for (const auto& sample : per_thread->data.sample()) {
      // Count only samples, which are the even numbers.  Total event counts
      // are the odds.
      for (int x = 0; x < sample.value_size(); x += 2) {
        total_samples += sample.value(x);
      }
    }
  }
  // The perf.data file contained 19989 original samples. Still should.
  EXPECT_EQ(19989, total_samples);
}

TEST_F(PerfDataConverterTest, GroupByThreadTypes) {
  std::string path(
      GetResource("single-event-multi-process-single-ip.textproto"));
  std::string ascii_pb = GetContents(path);
  ASSERT_FALSE(ascii_pb.empty()) << path;
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  std::map<uint32_t, std::string> thread_types;
  thread_types[1084] = "MAIN_THREAD";
  thread_types[1289] = "MAIN_THREAD";
  thread_types[1305] = "COMPOSITOR_THREAD";
  thread_types[5529] = "IO_THREAD";

  const ProcessProfiles pps = PerfDataProtoToProfiles(
      &perf_data_proto, kThreadTypeLabel, kNoOptions, thread_types);

  uint64_t total_samples = 0;
  std::unordered_map<std::string, uint64_t> counts_by_thread_type;
  for (const auto& pp : pps) {
    const auto& profile = pp->data;
    for (const auto& sample : profile.sample()) {
      // Count only samples, which are the even indices. Total event counts
      // are the odds.
      for (int x = 0; x < sample.value_size(); x += 2) {
        total_samples += sample.value(x);
      }
      for (const auto& label : sample.label()) {
        if (profile.string_table(label.key()) == ThreadTypeLabelKey) {
          ++counts_by_thread_type[profile.string_table(label.str())];
        }
      }
    }
  }
  EXPECT_EQ(8, total_samples);

  std::unordered_map<std::string, uint64_t> expected_counts = {
      {"MAIN_THREAD", 2}, {"IO_THREAD", 1}, {"COMPOSITOR_THREAD", 1}};
  EXPECT_EQ(expected_counts.size(), counts_by_thread_type.size());

  for (const auto& expected_count : expected_counts) {
    EXPECT_EQ(expected_count.second,
              counts_by_thread_type[expected_count.first])
        << "Different counts for thread type: " << expected_count.first
        << "(Expected: " << expected_count.second
        << "Actual: " << counts_by_thread_type[expected_count.first] << ")";
  }
}

TEST_F(PerfDataConverterTest, Injects) {
  std::string path = GetResource("with-callchain.perf.data");
  std::string raw_perf_data = GetContents(path);
  ASSERT_FALSE(raw_perf_data.empty()) << path;
  const std::string want_build_id = "abcdabcd";
  std::map<std::string, std::string> build_ids = {
      {"[kernel.kallsyms]", want_build_id}};

  // Test RawPerfData input.
  const ProcessProfiles pps = RawPerfDataToProfiles(
      reinterpret_cast<const void*>(raw_perf_data.c_str()),
      raw_perf_data.size(), build_ids);
  std::unordered_set<std::string> all_build_ids = AllBuildIDs(pps);
  EXPECT_THAT(all_build_ids, Contains(want_build_id));
}

TEST_F(PerfDataConverterTest, PreservesKallsymsRelocation) {
  std::string path = GetResource("with-callchain.perf.data");
  std::string raw_perf_data = GetContents(path);
  ASSERT_FALSE(raw_perf_data.empty()) << path;
  // The full kernel mmap name, including the "_stext" suffix, which signifies
  // if the perf tool used "_text" or "_stext" as the relocation symbol.
  const std::string want_mmap_name = "[kernel.kallsyms]_stext";

  const ProcessProfiles pps = RawPerfDataToProfiles(
      reinterpret_cast<const void*>(raw_perf_data.c_str()),
      raw_perf_data.size(), {});

  std::unordered_set<std::string> mmaps = AllMmapsWithBuildIDs(pps);
  EXPECT_THAT(mmaps, Contains(want_mmap_name));
}

TEST_F(PerfDataConverterTest, HandlesDataAddresses) {
  struct TestCase {
    std::string desc;
    std::string filename;
    uint32_t options;
    size_t want_samples;
    std::vector<size_t> want_frames;  // expected # of frames per sample
  };
  std::vector<TestCase> cases;
  cases.emplace_back(TestCase{"Flat profile without data",
                              "profile-with-data-addresses-flat.textproto",
                              kNoOptions,
                              3,
                              {1, 1, 1}});
  cases.emplace_back(TestCase{"Flat profile with data",
                              "profile-with-data-addresses-flat.textproto",
                              kAddDataAddressFrames,
                              3,
                              {2, 2, 2}});
  cases.emplace_back(TestCase{"Callchain profile without data",
                              "profile-with-data-addresses-callchain.textproto",
                              kNoOptions,
                              3,
                              {4, 4, 3}});
  cases.emplace_back(TestCase{"Callchain profile with data",
                              "profile-with-data-addresses-callchain.textproto",
                              kAddDataAddressFrames,
                              3,
                              {5, 5, 4}});

  for (const auto& c : cases) {
    std::string path(GetResource(c.filename));
    std::string ascii_pb = GetContents(path);
    ASSERT_FALSE(ascii_pb.empty()) << path;
    PerfDataProto perf_data_proto;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto))
        << path;

    const ProcessProfiles pps =
        PerfDataProtoToProfiles(&perf_data_proto, kNoLabels, c.options);

    // Expecting a single profile.
    EXPECT_EQ(1u, pps.size()) << c.desc;
    if (pps.empty()) continue;

    const auto& profile = pps[0]->data;
    ASSERT_EQ(c.want_samples, profile.sample_size()) << c.desc;
    ASSERT_EQ(c.want_samples, c.want_frames.size())
        << c.desc << ": frames vector has unexpected number of entries";
    int i = 0;
    for (const auto& sample : profile.sample()) {
      EXPECT_EQ(c.want_frames[i], sample.location_id_size())
          << c.desc << ": frame " << i;
      i++;
    }
  }
}

TEST_F(PerfDataConverterTest, HandlesKernelMmapOverlappingUserCode) {
  std::string path = GetResource("perf-overlapping-kernel-mapping.textproto");
  std::string ascii_pb = GetContents(path);
  ASSERT_FALSE(ascii_pb.empty()) << path;
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto);
  EXPECT_EQ(1, pps.size());
  const auto& profile = pps[0]->data;
  EXPECT_EQ(3, profile.sample_size());

  EXPECT_EQ(2, profile.mapping_size());
  EXPECT_EQ(1000, profile.mapping(0).memory_start());  // user
  int64_t user_mapping_id = profile.mapping(0).id();
  EXPECT_EQ(0, profile.mapping(1).memory_start());  // kernel
  int64_t kernel_mapping_id = profile.mapping(1).id();

  EXPECT_EQ(3, profile.location_size());
  EXPECT_EQ(kernel_mapping_id, profile.location(0).mapping_id());
  EXPECT_EQ(user_mapping_id, profile.location(1).mapping_id());
  EXPECT_EQ(kernel_mapping_id, profile.location(2).mapping_id());
}

TEST_F(PerfDataConverterTest, HandlesCrOSKernel3_18Mapping) {
  std::string path = GetResource("perf-cros-kernel-3_18-mapping.textproto");
  std::string ascii_pb = GetContents(path);
  ASSERT_FALSE(ascii_pb.empty()) << path;
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto);
  EXPECT_EQ(1, pps.size());
  const auto& profile = pps[0]->data;
  EXPECT_EQ(3, profile.sample_size());

  EXPECT_EQ(2, profile.mapping_size());
  EXPECT_EQ(1000, profile.mapping(0).memory_start());  // user
  int64_t user_mapping_id = profile.mapping(0).id();
  EXPECT_EQ(0xffffffff8bc001c8, profile.mapping(1).memory_start());  // kernel
  int64_t kernel_mapping_id = profile.mapping(1).id();

  EXPECT_EQ(3, profile.location_size());
  EXPECT_EQ(kernel_mapping_id, profile.location(0).mapping_id());
  EXPECT_EQ(user_mapping_id, profile.location(1).mapping_id());
  EXPECT_EQ(0, profile.location(2).mapping_id());
}

TEST_F(PerfDataConverterTest, HandlesNonExecCommEvents) {
  std::string path = GetResource("perf-non-exec-comm-events.textproto");
  std::string ascii_pb = GetContents(path);
  ASSERT_FALSE(ascii_pb.empty()) << path;
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto);
  EXPECT_EQ(2, pps.size());
  const auto& profile1 = pps[0]->data;
  const auto& profile2 = pps[1]->data;

  EXPECT_EQ(2, profile1.mapping_size());
  EXPECT_EQ(2, profile2.mapping_size());
  EXPECT_EQ("/usr/lib/systemd/systemd-journald",
            profile1.string_table(profile1.mapping(0).filename()));
  EXPECT_EQ("/usr/bin/coreutils",
            profile2.string_table(profile2.mapping(0).filename()));
  EXPECT_EQ(2, profile1.sample_size());
  EXPECT_EQ(2, profile2.sample_size());
}

TEST_F(PerfDataConverterTest, HandlesIncludingCommMd5Prefix) {
  std::string path = GetResource("perf-include-comm-md5-prefix.textproto");
  std::string ascii_pb = GetContents(path);
  ASSERT_FALSE(ascii_pb.empty()) << path;
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto);
  EXPECT_EQ(1, pps.size());
  const auto& profile = pps[0]->data;
  EXPECT_EQ(1, profile.sample_size());
  EXPECT_EQ(1, profile.mapping_size());
  EXPECT_EQ("b8c090b480e340b7",
            profile.string_table()[profile.mapping()[0].filename()]);
}

TEST_F(PerfDataConverterTest, HandlesUnmappedCallchainIP) {
  std::string path = GetResource("perf-unmapped-callchain-ip.textproto");
  std::string ascii_pb = GetContents(path);
  ASSERT_FALSE(ascii_pb.empty()) << path;
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  for (const auto& event_proto : perf_data_proto.events()) {
    if (event_proto.has_sample_event()) {
      int callchain_depth = 1 + event_proto.sample_event().callchain_size();
      EXPECT_EQ(4, callchain_depth);
    }
  }
  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto);
  EXPECT_EQ(1, pps.size());
  const auto& profile = pps[0]->data;

  EXPECT_EQ(1, profile.mapping_size());
  EXPECT_EQ("/usr/lib/systemd/systemd-journald",
            profile.string_table(profile.mapping(0).filename()));
  EXPECT_EQ(1, profile.sample_size());
  EXPECT_EQ(2, profile.location_size());
}

TEST_F(PerfDataConverterTest, HandlesUnmappedSampleAndBranchStack) {
  std::string path =
      GetResource("perf-unmapped-sample-and-branch-stack.textproto");
  std::string ascii_pb = GetContents(path);
  ASSERT_FALSE(ascii_pb.empty()) << path;
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  for (const auto& event_proto : perf_data_proto.events()) {
    if (event_proto.has_sample_event()) {
      int branch_stack_size = event_proto.sample_event().branch_stack_size();
      EXPECT_EQ(3, branch_stack_size);
    }
  }
  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto);
  EXPECT_EQ(1, pps.size());
  const auto& profile = pps[0]->data;

  EXPECT_EQ(1, profile.mapping_size());
  EXPECT_EQ("/opt/google/chrome/chrome",
            profile.string_table(profile.mapping(0).filename()));
  EXPECT_EQ(1, profile.sample_size());
  EXPECT_EQ(2, profile.location_size());
}

TEST_F(PerfDataConverterTest, HandlesLostEvents) {
  const std::string path = GetResource("perf-lost-events.textproto");
  const std::string ascii_pb = GetContents(path);
  ASSERT_FALSE(ascii_pb.empty()) << path;
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto);
  EXPECT_EQ(1, pps.size());
  const auto& profile = pps[0]->data;
  // Expecting 2 mappings. An explicit mapping and a mapping for lost samples.
  EXPECT_THAT(profile.mapping(), testing::SizeIs(2));
  EXPECT_EQ("/foo/bar", profile.string_table(profile.mapping(0).filename()));
  EXPECT_EQ(0x100000, profile.mapping(0).memory_start());
  EXPECT_EQ("[lost]", profile.string_table(profile.mapping(1).filename()));
  EXPECT_EQ(0x9ULL << 60, profile.mapping(1).memory_start());

  // Expecting 3 locations: 1 for mapped samples, 1 for unmapped samples, 1 for
  // lost samples.
  EXPECT_THAT(profile.location(), testing::SizeIs(3));
  EXPECT_EQ(1, profile.location(0).mapping_id());
  EXPECT_EQ(0, profile.location(1).mapping_id());
  EXPECT_EQ(2, profile.location(2).mapping_id());

  // Expecting 3 samples: 1 mapped with value 1, 1 unmapped with value 2, and
  // 1 for lost samples with value 10..
  EXPECT_THAT(profile.sample(), testing::SizeIs(3));
  // Mapped samples.
  EXPECT_EQ(1, profile.sample(0).location_id(0));
  EXPECT_EQ(1, profile.sample(0).value(0));
  // Unmapped samples.
  EXPECT_EQ(2, profile.sample(1).location_id(0));
  EXPECT_EQ(2, profile.sample(1).value(0));
  // Lost samples.
  EXPECT_EQ(3, profile.sample(2).location_id(0));
  EXPECT_EQ(10, profile.sample(2).value(0));
}

TEST_F(PerfDataConverterTest, GroupsByComm) {
  std::string path = GetResource("perf-comm-and-task-comm.textproto");
  std::string ascii_pb = GetContents(path);
  ASSERT_FALSE(ascii_pb.empty()) << path;
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  ProcessProfiles pps =
      PerfDataProtoToProfiles(&perf_data_proto, kCommLabel | kThreadCommLabel);
  EXPECT_EQ(2, pps.size());

  const int val_idx = 0;
  int total_samples = 0;
  std::unordered_map<std::string, uint64_t> counts_by_comm,
      counts_by_thread_comm;
  for (const auto& pp : pps) {
    const auto& p = pp->data;
    EXPECT_EQ(p.string_table(p.sample_type(val_idx).type()), "cycles_sample");
    for (const auto& sample : p.sample()) {
      total_samples += sample.value(val_idx);
      std::string comm, thread_comm;
      for (const auto& label : sample.label()) {
        if (p.string_table(label.key()) == CommLabelKey) {
          comm = p.string_table(label.str());
        }
        if (p.string_table(label.key()) == ThreadCommLabelKey) {
          thread_comm = p.string_table(label.str());
        }
      }
      counts_by_comm[comm] += sample.value(val_idx);
      counts_by_thread_comm[thread_comm] += sample.value(val_idx);
    }
  }
  EXPECT_EQ(10, total_samples);
  std::unordered_map<std::string, uint64_t> expected_comm_counts = {
      {"pid_2", 6},
      {"1234567812345678", 4},
  };
  EXPECT_THAT(counts_by_comm, UnorderedPointwise(Eq(), expected_comm_counts));
  std::unordered_map<std::string, uint64_t> expected_thread_comm_counts = {
      {"pid_2", 1},
      {"tid_3", 2},
      {"0", 3},
      {"1234567812345678", 4},
  };
  EXPECT_THAT(counts_by_thread_comm,
              UnorderedPointwise(Eq(), expected_thread_comm_counts));
}

TEST_F(PerfDataConverterTest, SkipsFirstCallchainIPPebs) {
  std::string ascii_pb(
      GetContents(GetResource("perf-callchain-pebs.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto);
  EXPECT_EQ(1, pps.size());
  const auto& profile = pps[0]->data;

  EXPECT_EQ(2, profile.location_size());
  EXPECT_EQ(0x562ceea5309c, profile.location(0).address());
  EXPECT_EQ(0x562cee9fc163 - 1, profile.location(1).address());
}

TEST_F(PerfDataConverterTest, SkipsFirstCallchainIPNonPebs) {
  std::string ascii_pb(
      GetContents(GetResource("perf-callchain-non-pebs.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto);
  EXPECT_EQ(1, pps.size());
  const auto& profile = pps[0]->data;

  EXPECT_EQ(2, profile.location_size());
  EXPECT_EQ(0x2a7fd3f6, profile.location(0).address());
  EXPECT_EQ(0x2a2726e7 - 1, profile.location(1).address());
}

TEST_F(PerfDataConverterTest, IgnoresClassesJsaAsMainMapping) {
  std::string ascii_pb(
      GetContents(GetResource("perf-java-classes-jsa.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto);
  EXPECT_EQ(pps.size(), 1);
  const auto& p = pps[0]->data;

  EXPECT_EQ(p.mapping_size(), 2);
  EXPECT_GT(p.string_table_size(), 0);
  EXPECT_EQ(p.string_table(p.mapping(0).filename()), "/export/package/App.jar");
}

TEST_F(PerfDataConverterTest, HandlesKernelSampleAfterExecBeforeMmap) {
  std::string ascii_pb(
      GetContents(GetResource("perf-kernel-sample-before-mmap.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto);
  ASSERT_EQ(pps.size(), 1);
  const auto& p = pps[0]->data;

  ASSERT_EQ(p.mapping_size(), 2);
  ASSERT_GE(p.string_table_size(), 2);
  // Main mapping - not referenced by any locations or samples.
  const auto& m0 = p.mapping(0);
  EXPECT_EQ(p.string_table(m0.filename()), "foo");
  EXPECT_EQ(m0.id(), 1);
  // The binary mapping, referenced by the second sample.
  const auto& m1 = p.mapping(1);
  EXPECT_EQ(p.string_table(m1.filename()), "/usr/bin/foo");
  EXPECT_EQ(m1.id(), 2);

  ASSERT_EQ(p.sample_size(), 2);
  // The first sample is unmapped.
  const auto& s0 = p.sample(0);
  EXPECT_EQ(s0.location_id_size(), 1);
  EXPECT_EQ(p.location(s0.location_id(0) - 1).mapping_id(), 0 /*no mapping*/);
  // The second sample points to the binary.
  const auto& s1 = p.sample(1);
  EXPECT_EQ(s1.location_id_size(), 1);
  EXPECT_EQ(p.location(s1.location_id(0) - 1).mapping_id(), m1.id());
}

TEST_F(PerfDataConverterTest, PerfInfoSavedInComment) {
  std::string path = GetResource("single-event-single-process.perf.data");
  std::string raw_perf_data = GetContents(path);
  ASSERT_FALSE(raw_perf_data.empty()) << path;
  const std::string want_version = "perf-version:3.16.7-ckt20";
  const std::string want_command =
      "perf-command:/usr/bin/perf_3.16 record ./a.out";

  // Test RawPerfData input.
  const ProcessProfiles pps = RawPerfDataToProfiles(
      reinterpret_cast<const void*>(raw_perf_data.c_str()),
      raw_perf_data.size(), {});
  std::unordered_set<std::string> comments = AllComments(pps);
  EXPECT_THAT(comments, Contains(want_version));
  EXPECT_THAT(comments, Contains(want_command));
}

TEST_F(PerfDataConverterTest, ConvertsCgroup) {
  const std::string ascii_pb(
      GetContents(GetResource("perf-cgroup-events.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  const ProcessProfiles pps =
      PerfDataProtoToProfiles(&perf_data_proto, kCgroupLabel);
  ASSERT_EQ(pps.size(), 2);

  const int val_idx = 0;
  int total_samples = 0;
  std::unordered_map<std::string, uint64_t> counts_by_cgroup;
  for (const auto& pp : pps) {
    const auto& p = pp->data;
    EXPECT_EQ(p.string_table(p.sample_type(val_idx).type()), "cycles_sample");
    for (const auto& sample : p.sample()) {
      total_samples += sample.value(val_idx);
      for (const auto& label : sample.label()) {
        if (p.string_table(label.key()) == CgroupLabelKey) {
          counts_by_cgroup[p.string_table(label.str())] +=
              sample.value(val_idx);
        }
      }
    }
  }
  EXPECT_EQ(total_samples, 10);

  const std::unordered_map<std::string, uint64_t> expected_counts{
      {"/", 1},
      {"/abc", 5},
      {"/XYZ", 4},
  };
  EXPECT_EQ(expected_counts.size(), counts_by_cgroup.size());
  for (const auto& expected_count : expected_counts) {
    EXPECT_EQ(expected_count.second, counts_by_cgroup[expected_count.first])
        << "Different counts for cgroup: " << expected_count.first << std::endl
        << "Expected: " << expected_count.second << std::endl
        << "Actual: " << counts_by_cgroup[expected_count.first] << std::endl;
  }
}

std::pair<int, std::unordered_map<uint64_t, uint64_t>> ExtractCounts(
    const ProcessProfiles& pps, std::string key_name) {
  std::unordered_map<uint64_t, uint64_t> counts;
  int total_samples = 0;
  const int val_idx = 0;

  for (const auto& pp : pps) {
    const auto& p = pp->data;
    EXPECT_EQ(p.string_table(p.sample_type(val_idx).type()), "cycles_sample");
    for (const auto& sample : p.sample()) {
      total_samples += sample.value(val_idx);
      for (const auto& label : sample.label()) {
        if (p.string_table(label.key()) == key_name) {
          counts[label.num()] += sample.value(val_idx);
        }
      }
    }
  }
  return {total_samples, counts};
}

TEST_F(PerfDataConverterTest, ConvertsCodePageSize) {
  const std::string ascii_pb(
      GetContents(GetResource("perf-code-data-page-sizes.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  const ProcessProfiles pps =
      PerfDataProtoToProfiles(&perf_data_proto, kCodePageSizeLabel);
  ASSERT_EQ(pps.size(), 1);

  const auto counts_pair = ExtractCounts(pps, CodePageSizeLabelKey);
  const auto total_samples = std::get<0>(counts_pair);
  const auto& counts = std::get<1>(counts_pair);
  const std::unordered_map<uint64_t, uint64_t> expected_counts{
      {4096, 2},
      {2097152, 2},
      {1073741824, 1},
  };
  EXPECT_EQ(total_samples, 5);
  EXPECT_THAT(counts, UnorderedPointwise(Eq(), expected_counts));
}

TEST_F(PerfDataConverterTest, ConvertsDataPageSize) {
  const std::string ascii_pb(
      GetContents(GetResource("perf-code-data-page-sizes.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  const ProcessProfiles pps =
      PerfDataProtoToProfiles(&perf_data_proto, kDataPageSizeLabel);
  ASSERT_EQ(pps.size(), 1);

  const auto counts_pair = ExtractCounts(pps, DataPageSizeLabelKey);
  const auto total_samples = std::get<0>(counts_pair);
  const auto& counts = std::get<1>(counts_pair);
  const std::unordered_map<uint64_t, uint64_t> expected_counts{
      {4096, 2},
      {2097152, 2},
      {1073741824, 1},
  };
  EXPECT_EQ(total_samples, 5);
  EXPECT_THAT(counts, UnorderedPointwise(Eq(), expected_counts));
}

TEST_F(PerfDataConverterTest, ConvertsCpu) {
  const std::string ascii_pb(GetContents(GetResource("perf-cpu.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  const ProcessProfiles pps =
      PerfDataProtoToProfiles(&perf_data_proto, kCpuLabel);
  ASSERT_EQ(pps.size(), 1);

  const auto counts_pair = ExtractCounts(pps, CpuLabelKey);
  const auto total_samples = std::get<0>(counts_pair);
  const auto& counts = std::get<1>(counts_pair);
  const std::unordered_map<uint64_t, uint64_t> expected_counts{
      {0, 1},
      {3, 2},
      {8, 1},
  };
  EXPECT_EQ(total_samples, 4);
  EXPECT_THAT(counts, UnorderedPointwise(Eq(), expected_counts));
}

// Test with a perf data doesn't have page size info.
TEST_F(PerfDataConverterTest, ConvertsNoCodeDataPageSize) {
  const std::string ascii_pb(
      GetContents(GetResource("perf-cgroup-events.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  const ProcessProfiles pps = PerfDataProtoToProfiles(
      &perf_data_proto, kCodePageSizeLabel | kDataPageSizeLabel);
  ASSERT_EQ(pps.size(), 2);

  auto counts_pair1 = ExtractCounts(pps, CodePageSizeLabelKey);
  const auto total_samples1 = std::get<0>(counts_pair1);
  const auto& code_counts = std::get<1>(counts_pair1);
  EXPECT_EQ(total_samples1, 10);
  EXPECT_THAT(code_counts, IsEmpty());

  auto counts_pair2 = ExtractCounts(pps, DataPageSizeLabelKey);
  const auto total_samples2 = std::get<0>(counts_pair2);
  const auto& data_counts = std::get<1>(counts_pair2);
  EXPECT_EQ(total_samples2, 10);
  EXPECT_THAT(data_counts, IsEmpty());
}

TEST_F(PerfDataConverterTest, WeightBitSet) {
  const std::string ascii_pb(GetContents(GetResource("perf-weight.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  // Round-trip deserialize/serialize, so we can make sure the test case
  // represent a valid perf.data file.
  std::string str;
  quipper::PerfReader reader;
  ASSERT_TRUE(reader.Deserialize(perf_data_proto));
  ASSERT_TRUE(reader.WriteToString(&str));
}

TEST_F(PerfDataConverterTest, WeightStructBitSet) {
  const std::string ascii_pb(
      GetContents(GetResource("perf-weight-struct.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  // Round-trip deserialize/serialize, so we can make sure the test case
  // represents a valid perf.data file.
  std::string str;
  quipper::PerfReader reader;
  ASSERT_TRUE(reader.Deserialize(perf_data_proto));
  ASSERT_TRUE(reader.WriteToString(&str));
}

TEST_F(PerfDataConverterTest, ConvertsWeight) {
  const std::string ascii_pb(GetContents(GetResource("perf-weight.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  const ProcessProfiles pps =
      PerfDataProtoToProfiles(&perf_data_proto, kCacheLatencyLabel);
  ASSERT_EQ(pps.size(), 1);

  const auto counts_pair = ExtractCounts(pps, CacheLatencyLabelKey);
  const auto total_samples = std::get<0>(counts_pair);
  const auto& counts = std::get<1>(counts_pair);
  const std::unordered_map<uint64_t, uint64_t> expected_counts{
      {146, 1},
      {352, 2},
  };
  EXPECT_EQ(total_samples, 3);
  EXPECT_THAT(counts, UnorderedPointwise(Eq(), expected_counts));
}

TEST_F(PerfDataConverterTest, ConvertsWeightStruct) {
  const std::string ascii_pb(
      GetContents(GetResource("perf-weight-struct.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  const ProcessProfiles pps =
      PerfDataProtoToProfiles(&perf_data_proto, kCacheLatencyLabel);
  ASSERT_EQ(pps.size(), 1);

  const auto counts_pair = ExtractCounts(pps, CacheLatencyLabelKey);
  const auto total_samples = std::get<0>(counts_pair);
  const auto& counts = std::get<1>(counts_pair);
  const std::unordered_map<uint64_t, uint64_t> expected_counts{
      {146, 1},
      {352, 2},
  };
  EXPECT_EQ(total_samples, 4);
  EXPECT_THAT(counts, UnorderedPointwise(Eq(), expected_counts));
}

// Test with a perf data doesn't have weight info.
TEST_F(PerfDataConverterTest, ConvertsNoWeight) {
  const std::string ascii_pb(
      GetContents(GetResource("perf-code-data-page-sizes.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  const ProcessProfiles pps =
      PerfDataProtoToProfiles(&perf_data_proto, kCacheLatencyLabel);
  ASSERT_EQ(pps.size(), 1);

  auto counts_pair1 = ExtractCounts(pps, CacheLatencyLabelKey);
  const auto total_samples1 = std::get<0>(counts_pair1);
  const auto& weight_counts = std::get<1>(counts_pair1);
  EXPECT_EQ(total_samples1, 5);
  EXPECT_THAT(weight_counts, IsEmpty());
}

TEST_F(PerfDataConverterTest, DataSrcBitSet) {
  const std::string ascii_pb(
      GetContents(GetResource("perf-datasrc.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  // Round-trip deserialize/serialize, so we can make sure the test case
  // represents a valid perf.data file.
  std::string str;
  quipper::PerfReader reader;
  ASSERT_TRUE(reader.Deserialize(perf_data_proto));
  ASSERT_TRUE(reader.WriteToString(&str));
}

TEST_F(PerfDataConverterTest, ConvertsDataSrc) {
  const std::string ascii_pb(
      GetContents(GetResource("perf-datasrc.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  const ProcessProfiles pps =
      PerfDataProtoToProfiles(&perf_data_proto, kDataSrcLabel);
  ASSERT_EQ(pps.size(), 1);

  std::unordered_map<std::string, uint64_t> counts_by_datasrc;
  const auto& p = pps[0]->data;
  for (const auto& sample : p.sample()) {
    std::string datasrc;
    for (const auto& label : sample.label()) {
      if (p.string_table(label.key()) == DataSrcLabelKey) {
        datasrc = p.string_table(label.str());
        counts_by_datasrc[datasrc]++;
      }
    }
  }
  const std::unordered_map<std::string, uint64_t> expected_counts{
      {"L1", 1},
      {"L2", 1},
      {"L3", 1},
  };
  EXPECT_THAT(counts_by_datasrc, UnorderedPointwise(Eq(), expected_counts));
}

TEST_F(PerfDataConverterTest, ConvertsSnoop) {
  const std::string ascii_pb(
      GetContents(GetResource("perf-datasrc.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  const ProcessProfiles pps =
      PerfDataProtoToProfiles(&perf_data_proto, kDataSrcLabel);
  ASSERT_EQ(pps.size(), 1);

  std::unordered_map<std::string, uint64_t> counts_by_snoop;
  const auto& p = pps[0]->data;
  for (const auto& sample : p.sample()) {
    std::string snoop_status;
    for (const auto& label : sample.label()) {
      if (p.string_table(label.key()) == SnoopStatusLabelKey) {
        snoop_status = p.string_table(label.str());
        counts_by_snoop[snoop_status]++;
      }
    }
  }
  const std::unordered_map<std::string, uint64_t> expected_counts{
      {"Hit", 1},
      {"Miss", 1},
      {"HitM", 1},
  };
  EXPECT_THAT(counts_by_snoop, UnorderedPointwise(Eq(), expected_counts));
}

TEST_F(PerfDataConverterTest, HandlesAlternateKernelNames) {
  std::string ascii_pb =
      GetContents(GetResource("perf-kernel-mapping-by-name.textproto"));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  // Round-trip deserialize/serialize, so we can test RawPerfDataToProfiles(),
  // which only takes a "raw" serialized perf.data.
  std::string str;
  quipper::PerfReader reader;
  ASSERT_TRUE(reader.Deserialize(perf_data_proto));
  ASSERT_TRUE(reader.WriteToString(&str));

  ProcessProfiles pps = RawPerfDataToProfiles(str.data(), str.size(), {});

  EXPECT_EQ(1, pps.size());
  const auto& profile = pps[0]->data;

  EXPECT_EQ(2, profile.mapping_size());
  // Both mappings in the test case should be matched to a build ID. The kernel
  // one specifically doesn't have PERF_RECORD_MISC_KERNEL, so we can test
  // that the matching of the MMAP kernel filename ([kernel.kallsyms]_text) to
  // the build_id kernel filename ([kernel.kallsyms]) works as expected.
  for (const auto& mapping : profile.mapping()) {
    EXPECT_NE(mapping.build_id(), 0) << mapping.DebugString();
  }
}

TEST_F(PerfDataConverterTest, BuildIdFromMmapEvents) {
  std::string ascii_pb(
      GetContents(GetResource("perf-buildid-mmap-events.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  // Expect that with buildid-mmap events, the samples will be assigned to the
  // corresponding different build IDs, even the mmap events have the same file
  // name.
  {
    // Write the proto perf.data to a string so that we can call the
    // RawPerfDataToProfiles API.
    std::string str;
    quipper::PerfReader reader;
    ASSERT_TRUE(reader.Deserialize(perf_data_proto));
    ASSERT_TRUE(reader.WriteToString(&str));
    ProcessProfiles pps = RawPerfDataToProfiles(
        str.data(), str.size(), {{"/usr/lib/bar", "abcdef0024"}});
    EXPECT_EQ(1, pps.size());
    const auto& pp = pps[0];

    ProfileIndexer indexer(pp->data);
    EXPECT_EQ(indexer.GetBuildId(0x2010), "abcdef0020");
    EXPECT_EQ(indexer.GetBuildId(0x2020), "abcdef0021");
  }

  // If we change buildid-mmap events back to normal mmap events, then we will
  // see thoes two samples being assigned the same build ID (the injected one).
  {
    for (size_t i = 0; i < perf_data_proto.events().size(); ++i) {
      auto event = perf_data_proto.mutable_events()->Mutable(i);
      if (event->has_mmap_event()) {
        event->mutable_header()->set_misc(0x2);
        event->mutable_mmap_event()->clear_build_id();
      }
    }
    std::string str;
    quipper::PerfReader reader;
    ASSERT_TRUE(reader.Deserialize(perf_data_proto));
    ASSERT_TRUE(reader.WriteToString(&str));
    ProcessProfiles pps = RawPerfDataToProfiles(
        str.data(), str.size(), {{"/usr/lib/bar", "abcdef0024"}});
    EXPECT_EQ(1, pps.size());
    const auto& pp = pps[0];

    ProfileIndexer indexer(pp->data);
    EXPECT_EQ(indexer.GetBuildId(0x2010), "abcdef0024");
    EXPECT_EQ(indexer.GetBuildId(0x2020), "abcdef0024");
  }
}

TEST_F(PerfDataConverterTest, BuildIdStats) {
  std::string ascii_pb(
      GetContents(GetResource("perf-buildid-stats.textproto")));
  ASSERT_FALSE(ascii_pb.empty());
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  std::string str;
  quipper::PerfReader reader;
  ASSERT_TRUE(reader.Deserialize(perf_data_proto));
  ASSERT_TRUE(reader.WriteToString(&str));
  ProcessProfiles pps = RawPerfDataToProfiles(
      str.data(), str.size(),
      {{"/usr/lib/bar", "abcdef0024"}, {"/usr/lib/qux", "abcdef0040"}});
  EXPECT_EQ(2, pps.size());

  // Expect that each Build ID source is counted correctly.
  EXPECT_EQ(pps[0]->build_id_stats.at(kBuildIdMmapSameFilename), 2);
  EXPECT_EQ(pps[0]->build_id_stats.at(kBuildIdMmapDiffFilename), 2);
  EXPECT_EQ(pps[0]->build_id_stats.at(kBuildIdFilename), 1);
  EXPECT_EQ(pps[0]->build_id_stats.at(kBuildIdFilenameInjected), 1);
  EXPECT_EQ(pps[0]->build_id_stats.at(kBuildIdFilenameAmbiguous), 1);
  EXPECT_EQ(pps[0]->build_id_stats.at(kBuildIdKernelPrefix), 1);
  EXPECT_EQ(pps[0]->build_id_stats.at(kBuildIdMissing), 1);
  EXPECT_EQ(pps[1]->build_id_stats.at(kBuildIdNoMmap), 1);
}

}  // namespace perftools

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
