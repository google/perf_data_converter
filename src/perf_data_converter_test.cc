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
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/compat/int_compat.h"
#include "src/compat/string_compat.h"
#include "src/compat/test_compat.h"
#include "src/intervalmap.h"
#include "src/quipper/perf_parser.h"
#include "src/quipper/perf_reader.h"

using perftools::ProcessProfiles;
using perftools::profiles::Location;
using perftools::profiles::Mapping;
using quipper::PerfDataProto;
using testing::Contains;

namespace {

typedef std::unordered_map<string, std::pair<int64, int64>> MapCounts;

// GetMapCounts returns a map keyed by a location identifier and
// mapping to self and total counts for that location.
MapCounts GetMapCounts(const ProcessProfiles& pps) {
  MapCounts map_counts;
  for (const auto& pp : pps) {
    const auto& profile = pp->data;
    std::unordered_map<uint64, const Location*> locations;
    perftools::IntervalMap<const Mapping*> mappings;
    if (profile.mapping_size() <= 0) {
      std::cerr << "Invalid mapping size: " << profile.mapping_size()
                << std::endl;
      abort();
    }
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
        uint64 id = sample.location_id(id_index);
        if (!locations[id]) {
          std::cerr << "No location for id: " << id << std::endl;
          abort();
        }

        std::stringstream key_stream;
        key_stream << profile.string_table(main.filename()) << ":"
                   << profile.string_table(main.build_id());
        if (locations[id]->mapping_id() != 0) {
          const Mapping* dso;
          uint64 addr = locations[id]->address();
          if (!mappings.Lookup(addr, &dso)) {
            std::cerr << "no mapping for id: " << std::hex << addr << std::endl;
            abort();
          }
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

std::unordered_set<string> AllBuildIDs(const ProcessProfiles& pps) {
  std::unordered_set<string> ret;
  for (const auto& pp : pps) {
    for (const auto& it : pp->data.mapping()) {
      ret.insert(pp->data.string_table(it.build_id()));
    }
  }
  return ret;
}

std::unordered_set<string> AllComments(const ProcessProfiles& pps) {
  std::unordered_set<string> ret;
  for (const auto& pp : pps) {
    for (const auto& it : pp->data.comment()) {
      ret.insert(pp->data.string_table(it));
    }
  }
  return ret;
}
}  // namespace

namespace perftools {

// Reads and returns the content of the file at path, or empty string on error.
string GetContents(const string& path) {
  std::ifstream ifs(path, std::ifstream::binary);
  if (!ifs) return "";
  std::stringstream ss;
  ss << ifs.rdbuf();
  return ss.str();
}

// Gets the string after the last '/' or returns the entire string if there are
// no slashes.
inline string Basename(const string& path) {
  return path.substr(path.find_last_of("/"));
}

// Assumes relpath does not begin with a '/'
string GetResource(const string& relpath) {
  return "src/" + relpath;
}

PerfDataProto ToPerfDataProto(const string& raw_perf_data) {
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
  string filename;
  int64 key_count;
  int64 total_exclusive;
  int64 total_inclusive;
};

// Builds a set of counts for each sample in the profile.  This is a
// very high-level test -- major changes in the values should
// be validated via manual inspection of new golden values.
TEST_F(PerfDataConverterTest, Converts) {
  string single_profile(
      GetResource("testdata"
                  "/single-event-single-process.perf.data"));
  string multi_pid_profile(
      GetResource("testdata"
                  "/single-event-multi-process.perf.data"));
  string multi_event_profile(
      GetResource("testdata"
                  "/multi-event-single-process.perf.data"));
  string stack_profile(
      GetResource("testdata"
                  "/with-callchain.perf.data"));

  std::vector<TestCase> cases;
  cases.emplace_back(TestCase{single_profile, 1061, 1061, 0});
  cases.emplace_back(TestCase{multi_pid_profile, 442, 730, 0});
  cases.emplace_back(TestCase{multi_event_profile, 1124, 1124, 0});
  cases.emplace_back(TestCase{stack_profile, 1138, 1210, 2247});

  for (const auto& c : cases) {
    string casename = "case " + Basename(c.filename);
    string raw_perf_data = GetContents(c.filename);
    ASSERT_FALSE(raw_perf_data.empty()) << c.filename;
    // Test RawPerfData input.
    auto pps = RawPerfDataToProfiles(
        reinterpret_cast<const void*>(raw_perf_data.c_str()),
        raw_perf_data.size(), {}, kNoLabels, kNoOptions);
    // Does not group by PID, Vector should only contain one element
    EXPECT_EQ(pps.size(), 1);
    auto counts = GetMapCounts(pps);
    EXPECT_EQ(c.key_count, counts.size()) << casename;
    int64 total_exclusive = 0;
    int64 total_inclusive = 0;
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
  string multiple_profile(
      GetResource("testdata"
                  "/single-event-multi-process.perf.data"));

  // Fetch the stdout_injected result and emit it to a profile.proto.  Group by
  // PIDs so the inner vector will have multiple entries.
  string raw_perf_data = GetContents(multiple_profile);
  ASSERT_FALSE(raw_perf_data.empty()) << multiple_profile;
  // Test PerfDataProto input.
  const auto perf_data_proto = ToPerfDataProto(raw_perf_data);
  const auto pps = PerfDataProtoToProfiles(
      &perf_data_proto, kPidAndTidLabels, kGroupByPids);

  uint64 total_samples = 0;
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
  string path(
      GetResource("testdata"
                  "/single-event-multi-process-single-ip.pb_proto"));

  string ascii_pb = GetContents(path);
  ASSERT_FALSE(ascii_pb.empty()) << path;
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));

  std::map<uint32, string> thread_types;
  thread_types[1084] = "MAIN_THREAD";
  thread_types[1289] = "MAIN_THREAD";
  thread_types[1305] = "COMPOSITOR_THREAD";
  thread_types[5529] = "IO_THREAD";

  const ProcessProfiles pps = PerfDataProtoToProfiles(
      &perf_data_proto, kThreadTypeLabel, kNoOptions, thread_types);

  uint64 total_samples = 0;
  std::unordered_map<string, uint64> counts_by_thread_type;
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

  std::unordered_map<string, uint64> expected_counts = {
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
  string path = GetResource("testdata"
                            "/with-callchain.perf.data");
  string raw_perf_data = GetContents(path);
  ASSERT_FALSE(raw_perf_data.empty()) << path;
  const string want_build_id = "abcdabcd";
  std::map<string, string> build_ids = {
      {"[kernel.kallsyms]", want_build_id}};

  // Test RawPerfData input.
  const ProcessProfiles pps = RawPerfDataToProfiles(
      reinterpret_cast<const void*>(raw_perf_data.c_str()),
      raw_perf_data.size(), build_ids);
  std::unordered_set<string> all_build_ids = AllBuildIDs(pps);
  EXPECT_THAT(all_build_ids, Contains(want_build_id));
}

TEST_F(PerfDataConverterTest, HandlesKernelMmapOverlappingUserCode) {
  string path = GetResource("testdata"
                            "/perf-overlapping-kernel-mapping.pb_proto");
  string ascii_pb = GetContents(path);
  ASSERT_FALSE(ascii_pb.empty()) << path;
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto);
  EXPECT_EQ(1, pps.size());
  const auto& profile = pps[0]->data;
  EXPECT_EQ(3, profile.sample_size());

  EXPECT_EQ(2, profile.mapping_size());
  EXPECT_EQ(1000, profile.mapping(0).memory_start());  // user
  int64 user_mapping_id = profile.mapping(0).id();
  EXPECT_EQ(0, profile.mapping(1).memory_start());  // kernel
  int64 kernel_mapping_id = profile.mapping(1).id();

  EXPECT_EQ(3, profile.location_size());
  EXPECT_EQ(kernel_mapping_id, profile.location(0).mapping_id());
  EXPECT_EQ(user_mapping_id, profile.location(1).mapping_id());
  EXPECT_EQ(kernel_mapping_id, profile.location(2).mapping_id());
}

TEST_F(PerfDataConverterTest, HandlesCrOSKernel3_18Mapping) {
  string path = GetResource(
      "testdata"
      "/perf-cros-kernel-3_18-mapping.pb_proto");
  string ascii_pb = GetContents(path);
  ASSERT_FALSE(ascii_pb.empty()) << path;
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto);
  EXPECT_EQ(1, pps.size());
  const auto& profile = pps[0]->data;
  EXPECT_EQ(3, profile.sample_size());

  EXPECT_EQ(2, profile.mapping_size());
  EXPECT_EQ(1000, profile.mapping(0).memory_start());  // user
  int64 user_mapping_id = profile.mapping(0).id();
  EXPECT_EQ(0xffffffff8bc001c8, profile.mapping(1).memory_start());  // kernel
  int64 kernel_mapping_id = profile.mapping(1).id();

  EXPECT_EQ(3, profile.location_size());
  EXPECT_EQ(kernel_mapping_id, profile.location(0).mapping_id());
  EXPECT_EQ(user_mapping_id, profile.location(1).mapping_id());
  EXPECT_EQ(0, profile.location(2).mapping_id());
}

TEST_F(PerfDataConverterTest, HandlesNonExecCommEvents) {
  string path = GetResource(
      "testdata"
      "/perf-non-exec-comm-events.pb_proto");
  string ascii_pb = GetContents(path);
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
  string path = GetResource(
      "testdata"
      "/perf-include-comm-md5-prefix.pb_proto");
  string ascii_pb = GetContents(path);
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
  string path = GetResource(
      "testdata"
      "/perf-unmapped-callchain-ip.pb_proto");
  string ascii_pb = GetContents(path);
  ASSERT_FALSE(ascii_pb.empty()) << path;
  PerfDataProto perf_data_proto;
  for (const auto& event_proto : perf_data_proto.events()) {
    if (event_proto.has_sample_event()) {
      int callchain_depth = 1 + event_proto.sample_event().callchain_size();
      EXPECT_EQ(3, callchain_depth);
    }
  }
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto);
  EXPECT_EQ(1, pps.size());
  const auto& profile = pps[0]->data;

  EXPECT_EQ(1, profile.mapping_size());
  EXPECT_EQ("/usr/lib/systemd/systemd-journald",
            profile.string_table(profile.mapping(0).filename()));
  EXPECT_EQ(1, profile.sample_size());
  EXPECT_EQ(2, profile.location_size());
}

TEST_F(PerfDataConverterTest, GroupsByComm) {
  string path = GetResource(
      "testdata"
      "/perf-comm-and-task-comm.textproto");
  string ascii_pb = GetContents(path);
  ASSERT_FALSE(ascii_pb.empty()) << path;
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(ascii_pb, &perf_data_proto));
  ProcessProfiles pps = PerfDataProtoToProfiles(&perf_data_proto, kCommLabel);
  EXPECT_EQ(2, pps.size());

  const int val_idx = 0;
  int total_samples = 0;
  std::unordered_map<string, uint64> counts_by_comm;
  for (const auto& pp : pps) {
    const auto& p = pp->data;
    EXPECT_EQ(p.string_table(p.sample_type(val_idx).type()), "cycles_sample");
    for (const auto& sample : p.sample()) {
      total_samples += sample.value(val_idx);
      string comm;
      for (const auto& label : sample.label()) {
        if (p.string_table(label.key()) == CommLabelKey) {
          comm = p.string_table(label.str());
          break;
        }
      }
      counts_by_comm[comm] += sample.value(val_idx);
    }
  }
  EXPECT_EQ(10, total_samples);
  std::unordered_map<string, uint64> expected_counts = {
      {"pid_2", 1},
      {"tid_3", 2},
      {"tid_4", 3},
      {"pid_5", 4},
  };
  EXPECT_THAT(counts_by_comm, UnorderedPointwise(Eq(), expected_counts));
}

TEST_F(PerfDataConverterTest, PerfInfoSavedInComment) {
  string path = GetResource(
      "testdata"
      "/single-event-single-process.perf.data");
  string raw_perf_data = GetContents(path);
  ASSERT_FALSE(raw_perf_data.empty()) << path;
  const string want_version = "perf-version:3.16.7-ckt20";
  const string want_command = "perf-command:/usr/bin/perf_3.16 record ./a.out";

  // Test RawPerfData input.
  const ProcessProfiles pps = RawPerfDataToProfiles(
      reinterpret_cast<const void*>(raw_perf_data.c_str()),
      raw_perf_data.size(), {});
  std::unordered_set<string> comments = AllComments(pps);
  EXPECT_THAT(comments, Contains(want_version));
  EXPECT_THAT(comments, Contains(want_command));
}

}  // namespace perftools

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
