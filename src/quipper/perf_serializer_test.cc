// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_serializer.h"

#include <sys/time.h>

#include <cstdint>
#include <map>
#include <sstream>
#include <string>

#include "base/logging.h"
#include "compat/proto.h"
#include "compat/test.h"
#include "file_utils.h"
#include "kernel/perf_event.h"
#include "kernel/perf_internals.h"
#include "perf_buildid.h"
#include "perf_data_structures.h"
#include "perf_data_utils.h"
#include "perf_protobuf_io.h"
#include "perf_reader.h"
#include "perf_test_files.h"
#include "scoped_temp_path.h"
#include "test_perf_data.h"
#include "test_utils.h"

namespace {

// Returns a string representation of an unsigned integer |value|.
std::string UintToString(uint64_t value) {
  std::stringstream ss;
  ss << value;
  return ss.str();
}

}  // namespace

namespace quipper {

using PerfEvent = PerfDataProto_PerfEvent;
using SampleInfo = PerfDataProto_SampleInfo;

namespace {

// Set up some parameterized fixtures for test cases that should run
// against multiple files.
class SerializePerfDataFiles : public ::testing::TestWithParam<const char*> {};
class SerializeAllPerfDataFiles : public ::testing::TestWithParam<const char*> {
};
class SerializePerfDataProtoFiles
    : public ::testing::TestWithParam<const char*> {};

// Gets the timestamp from an event field in PerfDataProto.
uint64_t GetSampleTimestampFromEventProto(
    const PerfDataProto_PerfEvent& event) {
  // Get SampleInfo from the correct type-specific event field for the event.
  if (event.has_mmap_event()) {
    return event.mmap_event().sample_info().sample_time_ns();
  } else if (event.has_sample_event()) {
    return event.sample_event().sample_time_ns();
  } else if (event.has_comm_event()) {
    return event.comm_event().sample_info().sample_time_ns();
  } else if (event.has_fork_event()) {
    return event.fork_event().sample_info().sample_time_ns();
  } else if (event.has_exit_event()) {
    return event.exit_event().sample_info().sample_time_ns();
  } else if (event.has_lost_event()) {
    return event.lost_event().sample_info().sample_time_ns();
  } else if (event.has_throttle_event()) {
    return event.throttle_event().sample_info().sample_time_ns();
  } else if (event.has_aux_event()) {
    return event.aux_event().sample_info().sample_time_ns();
  } else if (event.has_itrace_start_event()) {
    return event.itrace_start_event().sample_info().sample_time_ns();
  } else if (event.has_lost_samples_event()) {
    return event.lost_samples_event().sample_info().sample_time_ns();
  } else if (event.has_context_switch_event()) {
    return event.context_switch_event().sample_info().sample_time_ns();
  } else if (event.has_namespaces_event()) {
    return event.namespaces_event().sample_info().sample_time_ns();
  }
  return 0;
}

// Verifies that |proto|'s events are in chronological order. No event should
// have an earlier timestamp than a preceding event.
void CheckChronologicalOrderOfSerializedEvents(const PerfDataProto& proto) {
  uint64_t prev_time_ns = 0;
  for (int i = 0; i < proto.events_size(); ++i) {
    // Compare each timestamp against the previous event's timestamp.
    uint64_t time_ns = GetSampleTimestampFromEventProto(proto.events(i));
    if (i > 0) {
      EXPECT_GE(time_ns, prev_time_ns);
    }
    prev_time_ns = time_ns;
  }
}

void DeserializeAndSerialize(const PerfDataProto& input,
                             PerfDataProto* output) {
  // Check deserialization.
  PerfReader reader1;
  EXPECT_TRUE(reader1.Deserialize(input));
  std::string buf;
  reader1.WriteToString(&buf);

  PerfReader reader2;
  reader2.ReadFromString(buf);
  ASSERT_TRUE(reader2.Serialize(output));
}

void SerializeAndDeserialize(const std::string& input,
                             const std::string& output, bool do_remap,
                             bool discard_unused_events) {
  PerfDataProto perf_data_proto;
  PerfParserOptions options;
  options.do_remap = do_remap;
  options.deduce_huge_page_mappings = false;
  options.combine_mappings = false;
  options.discard_unused_events = discard_unused_events;
  options.sample_mapping_percentage_threshold = 100.0f;

  ASSERT_TRUE(SerializeFromFileWithOptions(input, options, &perf_data_proto));

  PerfReader reader;
  ASSERT_TRUE(reader.Deserialize(perf_data_proto));

  PerfParser parser(&reader, options);
  ASSERT_TRUE(parser.ParseRawEvents());

  // Check perf event stats.
  const PerfDataProto_PerfEventStats& in_stats = perf_data_proto.stats();
  PerfEventStats out_stats;
  PerfSerializer::DeserializeParserStats(perf_data_proto, &out_stats);

  EXPECT_EQ(in_stats.num_sample_events(), out_stats.num_sample_events);
  EXPECT_EQ(in_stats.num_mmap_events(), out_stats.num_mmap_events);
  EXPECT_EQ(in_stats.num_fork_events(), out_stats.num_fork_events);
  EXPECT_EQ(in_stats.num_exit_events(), out_stats.num_exit_events);
  EXPECT_EQ(in_stats.num_sample_events_mapped(),
            out_stats.num_sample_events_mapped);
  EXPECT_EQ(do_remap, in_stats.did_remap());
  EXPECT_EQ(do_remap, out_stats.did_remap);

  ASSERT_TRUE(reader.WriteFile(output));
}

void SerializeToFileAndBack(const std::string& input,
                            const std::string& output) {
  struct timeval pre_serialize_time;
  gettimeofday(&pre_serialize_time, NULL);

  // Serialize with and without sorting by chronological order.
  PerfDataProto input_perf_data_proto;

  // Serialize with and without sorting by chronological order.
  // PerfSerializer is stateless w/r to Serialize or Deserialize calls so we can
  // use just one.
  PerfParserOptions options;
  options.sort_events_by_time = true;
  options.deduce_huge_page_mappings = false;
  options.combine_mappings = false;
  EXPECT_TRUE(
      SerializeFromFileWithOptions(input, options, &input_perf_data_proto));
  CheckChronologicalOrderOfSerializedEvents(input_perf_data_proto);

  input_perf_data_proto.Clear();
  options.sort_events_by_time = false;
  EXPECT_TRUE(
      SerializeFromFileWithOptions(input, options, &input_perf_data_proto));

  // Make sure the timestamp_sec was properly recorded.
  EXPECT_TRUE(input_perf_data_proto.has_timestamp_sec());
  // Check it against the current time.
  struct timeval post_serialize_time;
  gettimeofday(&post_serialize_time, NULL);
  EXPECT_GE(input_perf_data_proto.timestamp_sec(), pre_serialize_time.tv_sec);
  EXPECT_LE(input_perf_data_proto.timestamp_sec(), post_serialize_time.tv_sec);

  // Now store the protobuf into a file.
  ScopedTempFile input_file;
  EXPECT_FALSE(input_file.path().empty());
  std::string input_filename = input_file.path();
  ScopedTempFile output_file;
  EXPECT_FALSE(output_file.path().empty());
  std::string output_filename = output_file.path();

  EXPECT_TRUE(WriteProtobufToFile(input_perf_data_proto, input_filename));

  PerfDataProto output_perf_data_proto;
  EXPECT_TRUE(ReadProtobufFromFile(&output_perf_data_proto, input_filename));

  EXPECT_TRUE(DeserializeToFile(output_perf_data_proto, output));

  EXPECT_TRUE(WriteProtobufToFile(output_perf_data_proto, output_filename));

  EXPECT_NE(GetFileSize(input_filename), 0);
  ASSERT_TRUE(CompareFileContents(input_filename, output_filename));

  remove(input_filename.c_str());
  remove(output_filename.c_str());
}

}  // namespace

TEST_P(SerializePerfDataFiles, Test1Cycle) {
  ScopedTempDir output_dir;
  ASSERT_FALSE(output_dir.path().empty());
  std::string output_path = output_dir.path();

  // Read perf data using the PerfReader class.
  // Dump it to a protobuf.
  // Read the protobuf, and reconstruct the perf data.
    PerfReader input_perf_reader, output_perf_reader, output_perf_reader1,
        output_perf_reader2;
    PerfDataProto perf_data_proto, perf_data_proto1;

    const std::string test_file = GetParam();
    const std::string input_perf_data = GetTestInputFilePath(test_file);
    const std::string output_perf_data =
        output_path + test_file + ".serialized.out";
    const std::string output_perf_data1 =
        output_path + test_file + ".serialized.1.out";

    LOG(INFO) << "Testing " << input_perf_data;
    ASSERT_TRUE(input_perf_reader.ReadFile(input_perf_data));

    // Discard unused events for a pseudorandom selection of half the test data
    // files. The selection is based on the Md5sum prefix of the file contents,
    // so that the files can be moved around in the |kPerfDataFiles| list or
    // renamed.
    std::vector<char> test_file_data;
    ASSERT_TRUE(FileToBuffer(input_perf_data, &test_file_data));
    bool discard = (Md5Prefix(test_file_data) % 2 == 0);

    SerializeAndDeserialize(input_perf_data, output_perf_data, false, discard);
    output_perf_reader.ReadFile(output_perf_data);
    SerializeAndDeserialize(output_perf_data, output_perf_data1, false,
                            discard);
    output_perf_reader1.ReadFile(output_perf_data1);

    ASSERT_TRUE(CompareFileContents(output_perf_data, output_perf_data1));

    std::string output_perf_data2 = output_path + test_file + ".io.out";
    SerializeToFileAndBack(input_perf_data, output_perf_data2);
    output_perf_reader2.ReadFile(output_perf_data2);

    // Make sure the # of events do not increase.  They can decrease because
    // some unused non-sample events may be discarded.
    if (discard) {
      ASSERT_LE(output_perf_reader.events().size(),
                input_perf_reader.events().size());
    } else {
      ASSERT_EQ(output_perf_reader.events().size(),
                input_perf_reader.events().size());
    }
    ASSERT_EQ(output_perf_reader1.events().size(),
              output_perf_reader.events().size());
    ASSERT_EQ(output_perf_reader2.events().size(),
              input_perf_reader.events().size());

    std::string difference;
    EXPECT_TRUE(CheckPerfDataAgainstBaseline(output_perf_data, "", &difference))
        << difference;
    EXPECT_TRUE(ComparePerfBuildIDLists(input_perf_data, output_perf_data));
    EXPECT_TRUE(
        CheckPerfDataAgainstBaseline(output_perf_data2, "", &difference))
        << difference;
    EXPECT_TRUE(ComparePerfBuildIDLists(output_perf_data, output_perf_data2));
}

TEST_P(SerializeAllPerfDataFiles, TestRemap) {
  ScopedTempDir output_dir;
  ASSERT_FALSE(output_dir.path().empty());
  const std::string output_path = output_dir.path();

  // Read perf data using the PerfReader class with address remapping.
  // Dump it to a protobuf.
  // Read the protobuf, and reconstruct the perf data.
  const std::string test_file = GetParam();
  const std::string input_perf_data = GetTestInputFilePath(test_file);
  LOG(INFO) << "Testing " << input_perf_data;
  const std::string output_perf_data =
      output_path + test_file + ".ser.remap.out";
  SerializeAndDeserialize(input_perf_data, output_perf_data, true, true);
}

TEST_P(SerializePerfDataFiles, TestGetEventSize) {
  const std::string test_file = GetParam();
  const std::string input_perf_data = GetTestInputFilePath(test_file);

  PerfDataProto perf_data_proto;
  EXPECT_TRUE(SerializeFromFile(input_perf_data, &perf_data_proto));
  ASSERT_GT(perf_data_proto.file_attrs().size(), 0U);

  PerfSerializer serializer;
  // Iterate through all attrs and create a SampleInfoReader for each of them.
  for (const auto& stored_attr : perf_data_proto.file_attrs()) {
    PerfFileAttr attr;
    serializer.DeserializePerfFileAttr(stored_attr, &attr);
    EXPECT_TRUE(
        serializer.CreateSampleInfoReader(attr, /*read_cross_endian=*/false));
  }

  for (auto event : perf_data_proto.events()) {
    EXPECT_EQ(event.header().size(), serializer.GetEventSize(event));
  }
}

TEST_P(SerializePerfDataFiles, TestDeserializeWithZeroEventSize) {
  const std::string test_file = GetParam();
  const std::string input_perf_data = GetTestInputFilePath(test_file);

  PerfDataProto perf_data_proto;
  EXPECT_TRUE(SerializeFromFile(input_perf_data, &perf_data_proto));
  ASSERT_GT(perf_data_proto.file_attrs().size(), 0U);

  for (auto& event : *perf_data_proto.mutable_events()) {
    event.mutable_header()->set_size(0);
  }

  ScopedTempDir output_dir;
  ASSERT_FALSE(output_dir.path().empty());
  std::string output_path = output_dir.path();
  const std::string output_perf_data = output_path + test_file + ".ser.out";
  EXPECT_TRUE(DeserializeToFile(perf_data_proto, output_perf_data));
}

TEST_P(SerializePerfDataFiles, TestCommMd5s) {
  ScopedTempDir output_dir;
  ASSERT_FALSE(output_dir.path().empty());
  std::string output_path = output_dir.path();

  // Replace command strings with their Md5sums.  Test size adjustment for
  // command strings.
  const std::string test_file = GetParam();
  const std::string input_perf_data = GetTestInputFilePath(test_file);
  LOG(INFO) << "Testing COMM Md5sum for " << input_perf_data;

  PerfDataProto perf_data_proto;
  EXPECT_TRUE(SerializeFromFile(input_perf_data, &perf_data_proto));

  // Need to get file attrs to construct a SampleInfoReader within
  // |serializer|.
  ASSERT_GT(perf_data_proto.file_attrs().size(), 0U);
  ASSERT_TRUE(perf_data_proto.file_attrs(0).has_attr());
  PerfSerializer serializer;
  PerfFileAttr attr;
  const auto& proto_attr = perf_data_proto.file_attrs(0);
  ASSERT_TRUE(serializer.DeserializePerfFileAttr(proto_attr, &attr));
  ASSERT_TRUE(
      serializer.CreateSampleInfoReader(attr, /*read_cross_endian=*/false));

  for (int j = 0; j < perf_data_proto.events_size(); ++j) {
    PerfDataProto_PerfEvent& event = *perf_data_proto.mutable_events(j);
    if (event.header().type() != PERF_RECORD_COMM) continue;
    CHECK(event.has_comm_event());

    std::string comm_md5_string =
        UintToString(event.comm_event().comm_md5_prefix());
    // Make sure it fits in the comm string array, accounting for the null
    // terminator.
    struct comm_event dummy;
    if (comm_md5_string.size() > arraysize(dummy.comm) - 1)
      comm_md5_string.resize(arraysize(dummy.comm) - 1);
    int64_t string_len_diff =
        GetUint64AlignedStringLength(comm_md5_string.size()) -
        GetUint64AlignedStringLength(event.comm_event().comm().size());
    event.mutable_comm_event()->set_comm(comm_md5_string);

    // Update with the new size.
    event.mutable_header()->set_size(event.header().size() + string_len_diff);
    }

    const std::string output_perf_data =
        output_path + test_file + ".ser.comm.out";
    std::string difference;
    EXPECT_TRUE(DeserializeToFile(perf_data_proto, output_perf_data));
    EXPECT_TRUE(CheckPerfDataAgainstBaseline(output_perf_data, "", &difference))
        << difference;
}

TEST_P(SerializePerfDataFiles, TestMmapMd5s) {
  ScopedTempDir output_dir;
  ASSERT_FALSE(output_dir.path().empty());
  std::string output_path = output_dir.path();

  // Replace MMAP filename strings with their Md5sums.  Test size adjustment for
  // MMAP filename strings.
  const std::string test_file = GetParam();
  const std::string input_perf_data = GetTestInputFilePath(test_file);
  LOG(INFO) << "Testing MMAP Md5sum for " << input_perf_data;

  PerfDataProto perf_data_proto;
  EXPECT_TRUE(SerializeFromFile(input_perf_data, &perf_data_proto));

  // Need to get file attrs to construct a SampleInfoReader within
  // |serializer|.
  ASSERT_GT(perf_data_proto.file_attrs().size(), 0U);
  ASSERT_TRUE(perf_data_proto.file_attrs(0).has_attr());
  PerfSerializer serializer;
  PerfFileAttr attr;
  const auto& proto_attr = perf_data_proto.file_attrs(0);
  ASSERT_TRUE(serializer.DeserializePerfFileAttr(proto_attr, &attr));
  ASSERT_TRUE(
      serializer.CreateSampleInfoReader(attr, /*read_cross_endian=*/false));

  for (int j = 0; j < perf_data_proto.events_size(); ++j) {
    PerfDataProto_PerfEvent& event = *perf_data_proto.mutable_events(j);
    if (event.header().type() != PERF_RECORD_MMAP) continue;
    ASSERT_TRUE(event.has_mmap_event());

    std::string filename_md5_string =
        UintToString(event.mmap_event().filename_md5_prefix());
    struct mmap_event dummy;
    // Make sure the Md5 prefix string can fit in the filename buffer,
    // including the null terminator
    if (filename_md5_string.size() > arraysize(dummy.filename) - 1)
      filename_md5_string.resize(arraysize(dummy.filename) - 1);

    int64_t string_len_diff =
        GetUint64AlignedStringLength(filename_md5_string.size()) -
        GetUint64AlignedStringLength(event.mmap_event().filename().size());
    event.mutable_mmap_event()->set_filename(filename_md5_string);

    // Update with the new size.
    event.mutable_header()->set_size(event.header().size() + string_len_diff);
    }

    const std::string output_perf_data =
        output_path + test_file + ".ser.mmap.out";
    // Make sure the data can be deserialized after replacing the filenames with
    // Md5sum prefixes.  No need to check the output.
    EXPECT_TRUE(DeserializeToFile(perf_data_proto, output_perf_data));
}

TEST_P(SerializePerfDataProtoFiles, TestProtoFiles) {
  const std::string test_file = GetParam();
  std::string perf_data_proto_file = GetTestInputFilePath(test_file);
  LOG(INFO) << "Testing " << perf_data_proto_file;
  std::vector<char> data;
  ASSERT_TRUE(FileToBuffer(perf_data_proto_file, &data));
  std::string text(data.begin(), data.end());

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(TextFormat::ParseFromString(text, &perf_data_proto));

  // Test deserializing.
  PerfReader deserializer;
  EXPECT_TRUE(deserializer.Deserialize(perf_data_proto));
}

TEST_P(SerializePerfDataFiles, TestBuildIDs) {
  const std::string test_file = GetParam();
  std::string perf_data_file = GetTestInputFilePath(test_file);
  LOG(INFO) << "Testing " << perf_data_file;

  // Serialize into a protobuf.
  PerfDataProto perf_data_proto;
  EXPECT_TRUE(SerializeFromFile(perf_data_file, &perf_data_proto));

  // Test a file with build ID filenames removed.
  for (int i = 0; i < perf_data_proto.build_ids_size(); ++i) {
    perf_data_proto.mutable_build_ids(i)->clear_filename();
  }
  PerfReader deserializer;
  EXPECT_TRUE(deserializer.Deserialize(perf_data_proto));
}

TEST(PerfSerializerTest, SerializesAndDeserializesTraceMetadata) {
  std::stringstream input;

  const size_t data_size =
      testing::ExamplePerfSampleEvent_Tracepoint::kEventSize;

  // header
  testing::ExamplePerfDataFileHeader file_header(1 << HEADER_TRACING_DATA);
  file_header.WithAttrCount(1).WithDataSize(data_size);
  file_header.WriteTo(&input);
  const perf_file_header& header = file_header.header();
  // attrs
  testing::ExamplePerfFileAttr_Tracepoint(73).WriteTo(&input);
  // data
  ASSERT_EQ(static_cast<u64>(input.tellp()), header.data.offset);
  testing::ExamplePerfSampleEvent_Tracepoint().WriteTo(&input);
  ASSERT_EQ(input.tellp(), file_header.data_end());
  // metadata
  const unsigned int metadata_count = 1;
  // HEADER_TRACING_DATA
  testing::ExampleTracingMetadata tracing_metadata(
      file_header.data_end() + metadata_count * sizeof(perf_file_section));
  tracing_metadata.index_entry().WriteTo(&input);
  tracing_metadata.data().WriteTo(&input);

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  const std::string& tracing_metadata_str = tracing_metadata.data().value();
  const auto& tracing_data = perf_data_proto.tracing_data();
  EXPECT_EQ(tracing_metadata_str, tracing_data.tracing_data());

  // Deserialize

  PerfReader deserializer;
  EXPECT_TRUE(deserializer.Deserialize(perf_data_proto));
  EXPECT_EQ(tracing_metadata_str, deserializer.tracing_data());
}

TEST(PerfSerializerTest, SerializesAndDeserializesPerfEventAttrEvent) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // no data

  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_IP | PERF_SAMPLE_TID,
                                              true /*sample_id_all*/)
      .WithUseClockid(true)
      .WithContextSwitch(true)
      .WithWriteBackward(true)
      .WithNamespaces(true)
      .WriteTo(&input);

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  EXPECT_EQ(1, perf_data_proto.file_attrs().size());
  {
    const PerfDataProto::PerfFileAttr& file_attr =
        perf_data_proto.file_attrs(0);
    EXPECT_TRUE(file_attr.has_attr());
    const PerfDataProto::PerfEventAttr& attr = file_attr.attr();
    EXPECT_TRUE(attr.use_clockid());
    EXPECT_TRUE(attr.context_switch());
    EXPECT_TRUE(attr.write_backward());
    EXPECT_TRUE(attr.namespaces());
  }

  EXPECT_EQ(0, perf_data_proto.events().size());
}

TEST(PerfSerializerTest, SerializesAndDeserializesPerfFileAttr) {
  std::stringstream input;

  // header
  testing::ExamplePerfDataFileHeader file_header((0));
  file_header.WithAttrCount(1).WithDataSize(0);
  file_header.WriteTo(&input);

  // attrs
  ASSERT_EQ(file_header.header().attrs.offset, static_cast<u64>(input.tellp()));
  testing::ExamplePerfFileAttr_Hardware(PERF_SAMPLE_IP | PERF_SAMPLE_TID,
                                        true /*sample_id_all*/)
      .WithUseClockid(true)
      .WithContextSwitch(true)
      .WithWriteBackward(true)
      .WithNamespaces(true)
      .WriteTo(&input);

  // no data

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  EXPECT_EQ(1, perf_data_proto.file_attrs().size());
  {
    const PerfDataProto::PerfFileAttr& file_attr =
        perf_data_proto.file_attrs(0);
    EXPECT_TRUE(file_attr.has_attr());
    const PerfDataProto::PerfEventAttr& attr = file_attr.attr();
    EXPECT_TRUE(attr.use_clockid());
    EXPECT_TRUE(attr.context_switch());
    EXPECT_TRUE(attr.write_backward());
    EXPECT_TRUE(attr.namespaces());
  }

  EXPECT_EQ(0, perf_data_proto.events().size());
}

TEST(PerfSerializerTest, TestHybridTopology) {
  const std::string test_file = "perf.data.hybrid_topology";
  const std::string input_perf_data = GetTestInputFilePath(test_file);

  PerfDataProto perf_data_proto;
  EXPECT_TRUE(SerializeFromFile(input_perf_data, &perf_data_proto));
  ASSERT_FALSE(perf_data_proto.hybrid_topology().empty());
  std::vector<std::string> want_cpus(perf_data_proto.hybrid_topology_size());

  for (int i = 0; i < perf_data_proto.hybrid_topology_size(); ++i) {
    PerfDataProto_PerfHybridTopologyMetadata& ht =
        *perf_data_proto.mutable_hybrid_topology(i);
    ASSERT_FALSE(ht.cpu_list().empty());
    want_cpus[i] = ht.cpus();
    ht.clear_cpus();
    ht.clear_pmu_name();
  }

  PerfDataProto perf_data_proto_2;
  DeserializeAndSerialize(perf_data_proto, &perf_data_proto_2);
  EXPECT_EQ(perf_data_proto_2.hybrid_topology_size(),
            perf_data_proto.hybrid_topology_size());
  for (int i = 0; i < perf_data_proto_2.hybrid_topology_size(); ++i) {
    EXPECT_EQ(perf_data_proto_2.hybrid_topology(i).cpus(), want_cpus[i]);
  }
}

TEST(PerfSerializerTest, SerializesAndDeserializesMmapEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data

  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_IP | PERF_SAMPLE_TID,
                                              true /*sample_id_all*/)
      .WriteTo(&input);

  // PERF_RECORD_MMAP
  testing::ExampleMmapEvent(1001, 0x1c1000, 0x1000, 0, "/usr/lib/foo.so",
                            testing::SampleInfo().Tid(1001))
      .WriteTo(&input);

  // PERF_RECORD_MMAP2
  testing::ExampleMmap2Event(1002, 0x2c1000, 0x2000, 0x3000, "/usr/lib/bar.so",
                             testing::SampleInfo().Tid(1002))
      .WriteTo(&input);

  // PERF_RECORD_MMAP2 with buildid-mmap field
  std::vector<u8> build_id = {0x0,  0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                              0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xef, 0x0};
  testing::ExampleMmap2Event(1003, 0x2c1000, 0x2000, 0x3000, "/usr/lib/bar.so",
                             testing::SampleInfo().Tid(1003))
      .WithMisc(PERF_RECORD_MISC_MMAP_BUILD_ID)
      .WithBuildId(build_id.data(), build_id.size())
      .WriteTo(&input);

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  EXPECT_EQ(3, perf_data_proto.events().size());

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_MMAP, event.header().type());
    EXPECT_TRUE(event.has_mmap_event());
    const PerfDataProto::MMapEvent& mmap = event.mmap_event();
    EXPECT_EQ(1001, mmap.pid());
    EXPECT_EQ(1001, mmap.tid());
    EXPECT_EQ(0x1c1000, mmap.start());
    EXPECT_EQ(0x1000, mmap.len());
    EXPECT_EQ(0, mmap.pgoff());
    EXPECT_EQ("/usr/lib/foo.so", mmap.filename());
  }

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(1);
    EXPECT_EQ(PERF_RECORD_MMAP2, event.header().type());
    EXPECT_TRUE(event.has_mmap_event());
    const PerfDataProto::MMapEvent& mmap = event.mmap_event();
    EXPECT_EQ(1002, mmap.pid());
    EXPECT_EQ(1002, mmap.tid());
    EXPECT_EQ(0x2c1000, mmap.start());
    EXPECT_EQ(0x2000, mmap.len());
    EXPECT_EQ(0x3000, mmap.pgoff());
    EXPECT_EQ("/usr/lib/bar.so", mmap.filename());
    // These values are hard-coded in ExampleMmap2Event:
    EXPECT_EQ(6, mmap.maj());
    EXPECT_EQ(7, mmap.min());
    EXPECT_EQ(8, mmap.ino());
    EXPECT_EQ(9, mmap.ino_generation());
    EXPECT_EQ(1 | 2, mmap.prot());
    EXPECT_EQ(2, mmap.flags());
  }

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(2);
    EXPECT_EQ(PERF_RECORD_MMAP2, event.header().type());
    EXPECT_TRUE(event.has_mmap_event());
    const PerfDataProto::MMapEvent& mmap = event.mmap_event();
    EXPECT_EQ(1003, mmap.pid());
    EXPECT_EQ(1003, mmap.tid());
    EXPECT_EQ(0x2c1000, mmap.start());
    EXPECT_EQ(0x2000, mmap.len());
    EXPECT_EQ(0x3000, mmap.pgoff());
    EXPECT_EQ("/usr/lib/bar.so", mmap.filename());
    EXPECT_EQ(mmap.build_id(), "00112233445566778899aabbccddef00");
  }

  {
    // Check deserialization
    PerfDataProto perf_data_proto_2;
    DeserializeAndSerialize(perf_data_proto, &perf_data_proto_2);

    std::string difference;
    bool matches = EqualsProto(perf_data_proto_2, perf_data_proto, &difference);
    EXPECT_TRUE(matches) << difference;
  }

  {
    // Deserialization should fail when the build ID is not a hex string.
    perf_data_proto.mutable_events(2)->mutable_mmap_event()->set_build_id(
        "not a hex string");
    PerfDataProto perf_data_proto_2;
    DeserializeAndSerialize(perf_data_proto, &perf_data_proto_2);

    EXPECT_NE(perf_data_proto_2.events().size(),
              perf_data_proto.events().size());
  }

  {
    // Deserialization should fail when the build ID is not a complete hex str.
    // E.g. "abc", where "ab" is a byte, but that single "c" is not.
    perf_data_proto.mutable_events(2)->mutable_mmap_event()->set_build_id(
        "abc");
    PerfDataProto perf_data_proto_2;
    DeserializeAndSerialize(perf_data_proto, &perf_data_proto_2);

    EXPECT_NE(perf_data_proto_2.events().size(),
              perf_data_proto.events().size());
  }

  {
    // Deserialization should fail when the build ID is larger than the
    // maximum value of the build ID field of mmap2 perf event.
    // https://source.corp.google.com/piper///depot/google3/third_party/linux_tools/src/tools/lib/perf/include/perf/event.h;l=39;bpv=1;bpt=0;rcl=483414731
    perf_data_proto.mutable_events(2)->mutable_mmap_event()->set_build_id(
        std::string(kMaxBuildIdSize * 2 + 2, 'a'));
    PerfDataProto perf_data_proto_2;
    DeserializeAndSerialize(perf_data_proto, &perf_data_proto_2);

    EXPECT_NE(perf_data_proto_2.events().size(),
              perf_data_proto.events().size());
  }

  {
    // Serialization should fail when the build_id_size field is larger than the
    // maximum value of the build ID field of mmap2 perf event.
    std::vector<u8> build_id = {0x0,  0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xef, 0x0};
    testing::ExampleMmap2Event(1003, 0x2c1000, 0x2000, 0x3000,
                               "/usr/lib/bar.so",
                               testing::SampleInfo().Tid(1003))
        .WithMisc(PERF_RECORD_MISC_MMAP_BUILD_ID)
        .WithBuildId(build_id.data(), build_id.size())
        .WithBuildIdSize(kMaxBuildIdSize + 1)
        .WriteTo(&input);
    PerfReader reader;
    ASSERT_FALSE(reader.ReadFromString(input.str()));
  }
}

TEST(PerfSerializerTest, SerializesAndDeserializesAuxtraceInfoEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data
  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_TID,
                                              /*sample_id_all=*/true)
      .WriteTo(&input);

  std::vector<u64> priv;
  priv.push_back(67548);
  priv.push_back(320945);

  // PERF_RECORD_AUXTRACE_INFO
  testing::ExampleAuxtraceInfoEvent(1, priv).WriteTo(&input);

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  EXPECT_EQ(1, perf_data_proto.events().size());

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_AUXTRACE_INFO, event.header().type());
    EXPECT_TRUE(event.has_auxtrace_info_event());
    const PerfDataProto::AuxtraceInfoEvent& auxtrace_info_event =
        event.auxtrace_info_event();
    EXPECT_EQ(1, auxtrace_info_event.type());
    EXPECT_EQ(2, auxtrace_info_event.unparsed_binary_blob_priv_data_size());
    EXPECT_EQ(67548, auxtrace_info_event.unparsed_binary_blob_priv_data(0));
    EXPECT_EQ(320945, auxtrace_info_event.unparsed_binary_blob_priv_data(1));
  }
}

TEST(PerfSerializerTest, SerializesAndDeserializesAuxtraceEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data

  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_IP,
                                              true /*sample_id_all*/)
      .WriteTo(&input);

  // PERF_RECORD_MMAP
  testing::ExampleAuxtraceEvent(9, 0x2000, 7, 3, 0x68d, 4, 0, "/dev/zero")
      .WriteTo(&input);

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  EXPECT_EQ(1, perf_data_proto.events().size());

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_AUXTRACE, event.header().type());
    EXPECT_TRUE(event.has_auxtrace_event());
    const PerfDataProto::AuxtraceEvent& auxtrace_event = event.auxtrace_event();
    EXPECT_EQ(9, auxtrace_event.size());
    EXPECT_EQ(0x2000, auxtrace_event.offset());
    EXPECT_EQ(7, auxtrace_event.reference());
    EXPECT_EQ(3, auxtrace_event.idx());
    EXPECT_EQ(0x68d, auxtrace_event.tid());
    EXPECT_EQ("/dev/zero", auxtrace_event.trace_data());
  }
}

TEST(PerfSerializerTest, SerializesAndDeserializesAuxtraceErrorEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data
  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_TID,
                                              /*sample_id_all=*/true)
      .WriteTo(&input);

  std::string auxtrace_error_msg =
      "000001111122222333334444455555666667777788888999990000011111222";

  // PERF_RECORD_AUXTRACE_ERROR
  testing::ExampleAuxtraceErrorEvent(1, 9876, 10, 4365, 4365, 86758,
                                     auxtrace_error_msg)
      .WriteTo(&input);

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  EXPECT_EQ(1, perf_data_proto.events().size());

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_AUXTRACE_ERROR, event.header().type());
    EXPECT_TRUE(event.has_auxtrace_error_event());
    const PerfDataProto::AuxtraceErrorEvent& auxtrace_error_event =
        event.auxtrace_error_event();
    EXPECT_EQ(1, auxtrace_error_event.type());
    EXPECT_EQ(9876, auxtrace_error_event.code());
    EXPECT_EQ(10, auxtrace_error_event.cpu());
    EXPECT_EQ(4365, auxtrace_error_event.pid());
    EXPECT_EQ(4365, auxtrace_error_event.tid());
    EXPECT_EQ(86758, auxtrace_error_event.ip());
    EXPECT_EQ(auxtrace_error_msg, auxtrace_error_event.msg());
    EXPECT_EQ(Md5Prefix(auxtrace_error_msg),
              auxtrace_error_event.msg_md5_prefix());
  }

  // Check deserialization.
  PerfReader out_reader;
  EXPECT_TRUE(out_reader.Deserialize(perf_data_proto));

  PerfDataProto perf_data_proto_2;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto_2));

  std::string difference;
  bool matches = EqualsProto(perf_data_proto_2, perf_data_proto, &difference);
  EXPECT_TRUE(matches) << difference;
}

TEST(PerfSerializerTest, SerializesAndDeserializesThreadMapEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data
  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_TID,
                                              /*sample_id_all=*/true)
      .WriteTo(&input);

  // PERF_RECORD_THREAD_MAP
  testing::ExampleThreadMapEvent()
      .WithEntry(1234, "comm1")
      .WithEntry(223344, "comm2")
      .WriteTo(&input);

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  EXPECT_EQ(1, perf_data_proto.events().size());

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_THREAD_MAP, event.header().type());
    EXPECT_TRUE(event.has_thread_map_event());
    const PerfDataProto::ThreadMapEvent& thread_map_event =
        event.thread_map_event();
    EXPECT_EQ(2, thread_map_event.entries_size());
    EXPECT_EQ(1234, thread_map_event.entries(0).pid());
    EXPECT_EQ("comm1", thread_map_event.entries(0).comm());
    EXPECT_EQ(Md5Prefix("comm1"),
              thread_map_event.entries(0).comm_md5_prefix());
    EXPECT_EQ(223344, thread_map_event.entries(1).pid());
    EXPECT_EQ("comm2", thread_map_event.entries(1).comm());
    EXPECT_EQ(Md5Prefix("comm2"),
              thread_map_event.entries(1).comm_md5_prefix());
  }
}

TEST(PerfSerializerTest, SerializesAndDeserializesStatConfigEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data
  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_TID,
                                              /*sample_id_all=*/true)
      .WriteTo(&input);

  std::vector<struct stat_config_event_entry> data;
  struct stat_config_event_entry entry1 = {
      .tag = 0,
      .val = 3,
  };
  struct stat_config_event_entry entry2 = {
      .tag = 1,
      .val = 20030,
  };
  struct stat_config_event_entry entry3 = {
      .tag = 2,
      .val = 1,
  };

  data.push_back(entry1);
  data.push_back(entry2);
  data.push_back(entry3);

  // PERF_RECORD_STAT_CONFIG
  testing::ExampleStatConfigEvent(data).WriteTo(&input);

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  EXPECT_EQ(1, perf_data_proto.events().size());

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_STAT_CONFIG, event.header().type());
    EXPECT_TRUE(event.has_stat_config_event());
    const PerfDataProto::StatConfigEvent& stat_config_event =
        event.stat_config_event();
    EXPECT_EQ(3, stat_config_event.data_size());
    EXPECT_EQ(0, stat_config_event.data(0).tag());
    EXPECT_EQ(3, stat_config_event.data(0).val());
    EXPECT_EQ(1, stat_config_event.data(1).tag());
    EXPECT_EQ(20030, stat_config_event.data(1).val());
    EXPECT_EQ(2, stat_config_event.data(2).tag());
    EXPECT_EQ(1, stat_config_event.data(2).val());
  }

  // Check deserialization.
  PerfReader out_reader;
  EXPECT_TRUE(out_reader.Deserialize(perf_data_proto));

  PerfDataProto perf_data_proto_2;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto_2));

  std::string difference;
  bool matches = EqualsProto(perf_data_proto_2, perf_data_proto, &difference);
  EXPECT_TRUE(matches) << difference;
}

TEST(PerfSerializerTest, SerializesAndDeserializesStatEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data
  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_TID,
                                              /*sample_id_all=*/true)
      .WriteTo(&input);

  // PERF_RECORD_STAT
  testing::ExampleStatEvent(10, 2, 3454, 8967584, 123423, 123056)
      .WriteTo(&input);

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  EXPECT_EQ(1, perf_data_proto.events().size());

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_STAT, event.header().type());
    EXPECT_TRUE(event.has_stat_event());
    const PerfDataProto::StatEvent& stat_event = event.stat_event();
    EXPECT_EQ(10, stat_event.id());
    EXPECT_EQ(2, stat_event.cpu());
    EXPECT_EQ(3454, stat_event.thread());
    EXPECT_EQ(8967584, stat_event.value());
    EXPECT_EQ(123423, stat_event.enabled());
    EXPECT_EQ(123056, stat_event.running());
  }

  // Check deserialization.
  PerfReader out_reader;
  EXPECT_TRUE(out_reader.Deserialize(perf_data_proto));

  PerfDataProto perf_data_proto_2;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto_2));

  std::string difference;
  bool matches = EqualsProto(perf_data_proto_2, perf_data_proto, &difference);
  EXPECT_TRUE(matches) << difference;
}

TEST(PerfSerializerTest, SerializesAndDeserializesStatRoundEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data
  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_TID,
                                              /*sample_id_all=*/true)
      .WriteTo(&input);

  // PERF_RECORD_STAT_ROUND
  testing::ExampleStatRoundEvent(PERF_STAT_ROUND_TYPE__FINAL, 134256)
      .WriteTo(&input);

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  EXPECT_EQ(1, perf_data_proto.events().size());

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_STAT_ROUND, event.header().type());
    EXPECT_TRUE(event.has_stat_round_event());
    const PerfDataProto::StatRoundEvent& stat_round_event =
        event.stat_round_event();
    EXPECT_EQ(PERF_STAT_ROUND_TYPE__FINAL, stat_round_event.type());
    EXPECT_EQ(134256, stat_round_event.time());
  }

  // Check deserialization.
  PerfReader out_reader;
  EXPECT_TRUE(out_reader.Deserialize(perf_data_proto));

  PerfDataProto perf_data_proto_2;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto_2));

  std::string difference;
  bool matches = EqualsProto(perf_data_proto_2, perf_data_proto, &difference);
  EXPECT_TRUE(matches) << difference;
}

TEST(PerfSerializerTest, SerializesAndDeserializesTimeConvEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data
  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_TID,
                                              true /*sample_id_all*/)
      .WriteTo(&input);

  // PERF_RECORD_TIME_CONV
  testing::ExampleTimeConvEventSmall(5656, 4, 234321).WriteTo(&input);

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  EXPECT_EQ(1, perf_data_proto.events().size());

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_TIME_CONV, event.header().type());
    EXPECT_TRUE(event.has_time_conv_event());
    const PerfDataProto::TimeConvEvent& time_conv_event =
        event.time_conv_event();
    EXPECT_EQ(5656, time_conv_event.time_shift());
    EXPECT_EQ(4, time_conv_event.time_mult());
    EXPECT_EQ(234321, time_conv_event.time_zero());
    EXPECT_FALSE(time_conv_event.has_time_cycles());
    EXPECT_FALSE(time_conv_event.has_time_mask());
    EXPECT_FALSE(time_conv_event.has_cap_user_time_zero());
    EXPECT_FALSE(time_conv_event.has_cap_user_time_short());
  }
}

// Kernel 5.10 added new fields to the time_conv event.
TEST(PerfSerializerTest, SerializesAndDeserializesLargeTimeConvEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data
  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_TID,
                                              true /*sample_id_all*/)
      .WriteTo(&input);

  // PERF_RECORD_TIME_CONV
  testing::ExampleTimeConvEvent(5656, 4, 234321, 9876, 1234, true, false)
      .WriteTo(&input);

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  EXPECT_EQ(1, perf_data_proto.events().size());

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_TIME_CONV, event.header().type());
    EXPECT_TRUE(event.has_time_conv_event());
    const PerfDataProto::TimeConvEvent& time_conv_event =
        event.time_conv_event();
    EXPECT_EQ(5656, time_conv_event.time_shift());
    EXPECT_EQ(4, time_conv_event.time_mult());
    EXPECT_EQ(234321, time_conv_event.time_zero());
    EXPECT_EQ(9876, time_conv_event.time_cycles());
    EXPECT_EQ(1234, time_conv_event.time_mask());
    EXPECT_EQ(true, time_conv_event.cap_user_time_zero());
    EXPECT_EQ(false, time_conv_event.cap_user_time_short());
  }
}

// Regression test for http://crbug.com/501004.
TEST(PerfSerializerTest, SerializesAndDeserializesBuildIDs) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // no data

  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(
      PERF_SAMPLE_TID | PERF_SAMPLE_TIME, true /*sample_id_all*/)
      .WriteTo(&input);

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  std::map<std::string, std::string> build_id_map;
  build_id_map["file1"] = "0123456789abcdef0123456789abcdef01234567";
  build_id_map["file2"] = "0123456789abcdef0123456789abcdef01230000";
  build_id_map["file3"] = "0123456789abcdef0123456789abcdef00000000";
  build_id_map["file4"] = "0123456789abcdef0123456789abcdef0000";
  build_id_map["file5"] = "0123456789abcdef0123456789abcdef";
  build_id_map["file6"] = "0123456789abcdef0123456789ab0000";
  build_id_map["file7"] = "0123456789abcdef012345670000";
  build_id_map["file8"] = "0123456789abcdef01234567";
  build_id_map["file9"] = "00000000";
  reader.InjectBuildIDs(build_id_map);

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  // Verify that the build ID info was properly injected.
  EXPECT_EQ(9, perf_data_proto.build_ids_size());
  for (int i = 0; i < perf_data_proto.build_ids_size(); ++i) {
    EXPECT_TRUE(perf_data_proto.build_ids(i).has_filename());
    EXPECT_TRUE(perf_data_proto.build_ids(i).has_build_id_hash());
    EXPECT_TRUE(perf_data_proto.build_ids(i).is_injected());
  }

  // Verify that the serialized build IDs have had their trailing zeroes
  // trimmed.
  EXPECT_EQ("file1", perf_data_proto.build_ids(0).filename());
  EXPECT_EQ("0123456789abcdef0123456789abcdef01234567",
            RawDataToHexString(perf_data_proto.build_ids(0).build_id_hash()));

  EXPECT_EQ("file2", perf_data_proto.build_ids(1).filename());
  EXPECT_EQ("0123456789abcdef0123456789abcdef01230000",
            RawDataToHexString(perf_data_proto.build_ids(1).build_id_hash()));

  EXPECT_EQ("file3", perf_data_proto.build_ids(2).filename());
  EXPECT_EQ("0123456789abcdef0123456789abcdef",
            RawDataToHexString(perf_data_proto.build_ids(2).build_id_hash()));

  EXPECT_EQ("file4", perf_data_proto.build_ids(3).filename());
  EXPECT_EQ("0123456789abcdef0123456789abcdef",
            RawDataToHexString(perf_data_proto.build_ids(3).build_id_hash()));

  EXPECT_EQ("file5", perf_data_proto.build_ids(4).filename());
  EXPECT_EQ("0123456789abcdef0123456789abcdef",
            RawDataToHexString(perf_data_proto.build_ids(4).build_id_hash()));

  EXPECT_EQ("file6", perf_data_proto.build_ids(5).filename());
  EXPECT_EQ("0123456789abcdef0123456789ab0000",
            RawDataToHexString(perf_data_proto.build_ids(5).build_id_hash()));

  EXPECT_EQ("file7", perf_data_proto.build_ids(6).filename());
  EXPECT_EQ("0123456789abcdef01234567",
            RawDataToHexString(perf_data_proto.build_ids(6).build_id_hash()));

  EXPECT_EQ("file8", perf_data_proto.build_ids(7).filename());
  EXPECT_EQ("0123456789abcdef01234567",
            RawDataToHexString(perf_data_proto.build_ids(7).build_id_hash()));

  EXPECT_EQ("file9", perf_data_proto.build_ids(8).filename());
  EXPECT_EQ("",
            RawDataToHexString(perf_data_proto.build_ids(8).build_id_hash()));

  // Check deserialization.
  PerfReader out_reader;
  EXPECT_TRUE(out_reader.Deserialize(perf_data_proto));
  const auto& build_ids = out_reader.build_ids();
  ASSERT_EQ(9, build_ids.size());

  std::vector<malloced_unique_ptr<build_id_event>> raw_build_ids(
      build_ids.size());

  // Convert the build IDs back to raw build ID events.
  PerfSerializer serializer;
  for (int i = 0; i < build_ids.size(); ++i) {
    ASSERT_TRUE(serializer.DeserializeBuildIDEvent(build_ids.Get(i),
                                                   &raw_build_ids[i]));
  }

  // All trimmed build IDs should be padded to the full 20 byte length.
  EXPECT_EQ(std::string("file1"), raw_build_ids[0]->filename);
  EXPECT_EQ("0123456789abcdef0123456789abcdef01234567",
            RawDataToHexString(raw_build_ids[0]->build_id, kBuildIDArraySize));

  EXPECT_EQ(std::string("file2"), raw_build_ids[1]->filename);
  EXPECT_EQ("0123456789abcdef0123456789abcdef01230000",
            RawDataToHexString(raw_build_ids[1]->build_id, kBuildIDArraySize));

  EXPECT_EQ(std::string("file3"), raw_build_ids[2]->filename);
  EXPECT_EQ("0123456789abcdef0123456789abcdef00000000",
            RawDataToHexString(raw_build_ids[2]->build_id, kBuildIDArraySize));

  EXPECT_EQ(std::string("file4"), raw_build_ids[3]->filename);
  EXPECT_EQ("0123456789abcdef0123456789abcdef00000000",
            RawDataToHexString(raw_build_ids[3]->build_id, kBuildIDArraySize));

  EXPECT_EQ(std::string("file5"), raw_build_ids[4]->filename);
  EXPECT_EQ("0123456789abcdef0123456789abcdef00000000",
            RawDataToHexString(raw_build_ids[4]->build_id, kBuildIDArraySize));

  EXPECT_EQ(std::string("file6"), raw_build_ids[5]->filename);
  EXPECT_EQ("0123456789abcdef0123456789ab000000000000",
            RawDataToHexString(raw_build_ids[5]->build_id, kBuildIDArraySize));

  EXPECT_EQ(std::string("file7"), raw_build_ids[6]->filename);
  EXPECT_EQ("0123456789abcdef012345670000000000000000",
            RawDataToHexString(raw_build_ids[6]->build_id, kBuildIDArraySize));

  EXPECT_EQ(std::string("file8"), raw_build_ids[7]->filename);
  EXPECT_EQ("0123456789abcdef012345670000000000000000",
            RawDataToHexString(raw_build_ids[7]->build_id, kBuildIDArraySize));

  EXPECT_EQ(std::string("file9"), raw_build_ids[8]->filename);
  EXPECT_EQ("0000000000000000000000000000000000000000",
            RawDataToHexString(raw_build_ids[8]->build_id, kBuildIDArraySize));
}

// Regression test for http://crbug.com/500746.
TEST(PerfSerializerTest, SerializesAndDeserializesForkAndExitEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data

  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(
      PERF_SAMPLE_TID | PERF_SAMPLE_TIME, true /*sample_id_all*/)
      .WriteTo(&input);

  // PERF_RECORD_FORK
  testing::ExampleForkEvent(
      1010, 1020, 1030, 1040, 355ULL * 1000000000,
      testing::SampleInfo().Tid(2010, 2020).Time(356ULL * 1000000000))
      .WriteTo(&input);

  // PERF_RECORD_EXIT
  testing::ExampleExitEvent(
      3010, 3020, 3030, 3040, 432ULL * 1000000000,
      testing::SampleInfo().Tid(4010, 4020).Time(433ULL * 1000000000))
      .WriteTo(&input);

  // Parse and serialize.
  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  ASSERT_EQ(2, perf_data_proto.events_size());

  {
    const PerfDataProto_PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_FORK, event.header().type());
    EXPECT_TRUE(event.has_fork_event());
    EXPECT_FALSE(event.has_exit_event());

    EXPECT_EQ(1010, event.fork_event().pid());
    EXPECT_EQ(1020, event.fork_event().ppid());
    EXPECT_EQ(1030, event.fork_event().tid());
    EXPECT_EQ(1040, event.fork_event().ptid());
    EXPECT_EQ(355ULL * 1000000000, event.fork_event().fork_time_ns());

    EXPECT_EQ(2010, event.fork_event().sample_info().pid());
    EXPECT_EQ(2020, event.fork_event().sample_info().tid());
    EXPECT_EQ(356ULL * 1000000000,
              event.fork_event().sample_info().sample_time_ns());
  }

  {
    const PerfDataProto_PerfEvent& event = perf_data_proto.events(1);
    EXPECT_EQ(PERF_RECORD_EXIT, event.header().type());
    EXPECT_FALSE(event.has_fork_event());
    EXPECT_TRUE(event.has_exit_event());

    EXPECT_EQ(3010, event.exit_event().pid());
    EXPECT_EQ(3020, event.exit_event().ppid());
    EXPECT_EQ(3030, event.exit_event().tid());
    EXPECT_EQ(3040, event.exit_event().ptid());
    EXPECT_EQ(432ULL * 1000000000, event.exit_event().fork_time_ns());

    EXPECT_EQ(4010, event.exit_event().sample_info().pid());
    EXPECT_EQ(4020, event.exit_event().sample_info().tid());
    EXPECT_EQ(433ULL * 1000000000,
              event.exit_event().sample_info().sample_time_ns());
  }

  // Deserialize and verify events.
  PerfReader out_reader;
  ASSERT_TRUE(out_reader.Deserialize(perf_data_proto));

  EXPECT_EQ(2, out_reader.events().size());

  {
    const PerfEvent& event = out_reader.events().Get(0);
    EXPECT_EQ(PERF_RECORD_FORK, event.header().type());

    EXPECT_EQ(1010, event.fork_event().pid());
    EXPECT_EQ(1020, event.fork_event().ppid());
    EXPECT_EQ(1030, event.fork_event().tid());
    EXPECT_EQ(1040, event.fork_event().ptid());
    EXPECT_EQ(355ULL * 1000000000, event.fork_event().fork_time_ns());

    const SampleInfo& sample_info = event.fork_event().sample_info();
    EXPECT_EQ(2010, sample_info.pid());
    EXPECT_EQ(2020, sample_info.tid());
    EXPECT_EQ(356ULL * 1000000000, sample_info.sample_time_ns());
  }

  {
    const PerfEvent& event = out_reader.events().Get(1);
    EXPECT_EQ(PERF_RECORD_EXIT, event.header().type());

    EXPECT_EQ(3010, event.exit_event().pid());
    EXPECT_EQ(3020, event.exit_event().ppid());
    EXPECT_EQ(3030, event.exit_event().tid());
    EXPECT_EQ(3040, event.exit_event().ptid());
    EXPECT_EQ(432ULL * 1000000000, event.exit_event().fork_time_ns());

    const SampleInfo& sample_info = event.exit_event().sample_info();
    EXPECT_EQ(4010, sample_info.pid());
    EXPECT_EQ(4020, sample_info.tid());
    EXPECT_EQ(433ULL * 1000000000, sample_info.sample_time_ns());
  }
}

TEST(PerfSerializerTest, SerializesAndDeserializesContextSwitchEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data

  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_TID,
                                              true /*sample_id_all*/)
      .WriteTo(&input);

  // PERF_RECORD_SWITCH
  testing::ExampleContextSwitchEvent(true, testing::SampleInfo().Tid(1001))
      .WriteTo(&input);
  testing::ExampleContextSwitchEvent(false, testing::SampleInfo().Tid(1001))
      .WriteTo(&input);

  // PERF_RECORD_SWITCH_CPU_WIDE
  testing::ExampleContextSwitchEvent(true, 5656, 5656,
                                     testing::SampleInfo().Tid(1001))
      .WriteTo(&input);
  testing::ExampleContextSwitchEvent(false, 1001, 1001,
                                     testing::SampleInfo().Tid(5656))
      .WriteTo(&input);

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  EXPECT_EQ(4, perf_data_proto.events().size());

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_SWITCH, event.header().type());
    EXPECT_TRUE(event.has_context_switch_event());
    const PerfDataProto::ContextSwitchEvent& context_switch_event =
        event.context_switch_event();
    EXPECT_EQ(PERF_RECORD_MISC_SWITCH_OUT, event.header().misc());
    EXPECT_EQ(1001, context_switch_event.sample_info().pid());
    EXPECT_EQ(1001, context_switch_event.sample_info().tid());
  }

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(1);
    EXPECT_EQ(PERF_RECORD_SWITCH, event.header().type());
    EXPECT_TRUE(event.has_context_switch_event());
    const PerfDataProto::ContextSwitchEvent& context_switch_event =
        event.context_switch_event();
    EXPECT_EQ(0, event.header().misc());
    EXPECT_EQ(1001, context_switch_event.sample_info().pid());
    EXPECT_EQ(1001, context_switch_event.sample_info().tid());
  }

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(2);
    EXPECT_EQ(PERF_RECORD_SWITCH_CPU_WIDE, event.header().type());
    EXPECT_TRUE(event.has_context_switch_event());
    const PerfDataProto::ContextSwitchEvent& context_switch_event =
        event.context_switch_event();
    EXPECT_EQ(PERF_RECORD_MISC_SWITCH_OUT, event.header().misc());
    EXPECT_EQ(5656, context_switch_event.next_prev_pid());
    EXPECT_EQ(5656, context_switch_event.next_prev_tid());
    EXPECT_EQ(1001, context_switch_event.sample_info().pid());
    EXPECT_EQ(1001, context_switch_event.sample_info().tid());
  }

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(3);
    EXPECT_EQ(PERF_RECORD_SWITCH_CPU_WIDE, event.header().type());
    EXPECT_TRUE(event.has_context_switch_event());
    const PerfDataProto::ContextSwitchEvent& context_switch_event =
        event.context_switch_event();
    EXPECT_EQ(0, event.header().misc());
    EXPECT_EQ(1001, context_switch_event.next_prev_pid());
    EXPECT_EQ(1001, context_switch_event.next_prev_tid());
    EXPECT_EQ(5656, context_switch_event.sample_info().pid());
    EXPECT_EQ(5656, context_switch_event.sample_info().tid());
  }
}

TEST(PerfSerializerTest, SerializesAndDeserializesNamespacesEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data
  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_TID,
                                              true /*sample_id_all*/)
      .WriteTo(&input);

  std::vector<struct perf_ns_link_info> link_infos;
  struct perf_ns_link_info link_info1 = {
      .dev = 1234,
      .ino = 5678,
  };
  struct perf_ns_link_info link_info2 = {
      .dev = 223344,
      .ino = 556677,
  };

  link_infos.push_back(link_info1);
  link_infos.push_back(link_info2);

  // PERF_RECORD_NAMESPACES
  testing::ExampleNamespacesEvent(5656, 5656, link_infos,
                                  testing::SampleInfo().Tid(1001))
      .WriteTo(&input);

  // Parse and Serialize

  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  EXPECT_EQ(1, perf_data_proto.events().size());

  {
    const PerfDataProto::PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_NAMESPACES, event.header().type());
    EXPECT_TRUE(event.has_namespaces_event());
    const PerfDataProto::NamespacesEvent& namespaces_event =
        event.namespaces_event();
    EXPECT_EQ(5656, namespaces_event.pid());
    EXPECT_EQ(5656, namespaces_event.tid());
    EXPECT_EQ(2, namespaces_event.link_info_size());
    EXPECT_EQ(1234, namespaces_event.link_info(0).dev());
    EXPECT_EQ(5678, namespaces_event.link_info(0).ino());
    EXPECT_EQ(223344, namespaces_event.link_info(1).dev());
    EXPECT_EQ(556677, namespaces_event.link_info(1).ino());
  }
}

// Regression test for http://crbug.com/500746.
TEST(PerfSerializerTest, DeserializeLegacyExitEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data

  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(
      PERF_SAMPLE_TID | PERF_SAMPLE_TIME, true /*sample_id_all*/)
      .WriteTo(&input);

  // PERF_RECORD_EXIT
  testing::ExampleExitEvent(
      3010, 3020, 3030, 3040, 432ULL * 1000000000,
      testing::SampleInfo().Tid(4010, 4020).Time(433ULL * 1000000000))
      .WriteTo(&input);

  // Parse and serialize.
  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto proto;
  ASSERT_TRUE(reader.Serialize(&proto));

  ASSERT_EQ(1, proto.events_size());
  ASSERT_TRUE(proto.events(0).has_exit_event());
  ASSERT_FALSE(proto.events(0).has_fork_event());

  // Modify the protobuf to store the exit event in the |fork_event| field
  // instead.
  PerfDataProto_ForkEvent ex;
  ex.CopyFrom(proto.events(0).exit_event());
  proto.mutable_events(0)->clear_exit_event();
  proto.mutable_events(0)->mutable_fork_event()->CopyFrom(ex);

  PerfReader out_reader;
  ASSERT_TRUE(out_reader.Deserialize(proto));

  EXPECT_EQ(1U, out_reader.events().size());

  const PerfEvent& event = out_reader.events().Get(0);
  EXPECT_EQ(PERF_RECORD_EXIT, event.header().type());
  EXPECT_EQ(3010, event.fork_event().pid());
  EXPECT_EQ(3020, event.fork_event().ppid());
  EXPECT_EQ(3030, event.fork_event().tid());
  EXPECT_EQ(3040, event.fork_event().ptid());
  EXPECT_EQ(432ULL * 1000000000, event.fork_event().fork_time_ns());

  const SampleInfo& sample_info = event.fork_event().sample_info();
  EXPECT_EQ(4010, sample_info.pid());
  EXPECT_EQ(4020, sample_info.tid());
  EXPECT_EQ(433ULL * 1000000000, sample_info.sample_time_ns());
}

TEST(PerfSerializerTest, SerializesAndDeserializesSampleEventRead) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data

  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(
      PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_READ,
      /*sample_id_all=*/true)
      .WithId(401)
      .WithReadFormat(PERF_FORMAT_ID | PERF_FORMAT_LOST)
      .WriteTo(&input);

  // PERF_RECORD_SAMPLE
  testing::ExamplePerfSampleEvent(
      testing::SampleInfo()
          .Ip(0x00000000001c1000)
          .Tid(401)
          .ReadInfo_readvalue(/*value=*/0x103c5d0, /*id=*/402, /*lost=*/808))
      .WriteTo(&input);

  // Parse and serialize.
  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));
  ASSERT_EQ(1, perf_data_proto.events_size());
  {
    const PerfDataProto_PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_SAMPLE, event.header().type());
    ASSERT_TRUE(event.has_sample_event());
    ASSERT_TRUE(event.sample_event().has_read_info());
    const PerfDataProto_ReadInfo_ReadValue& read_value =
        event.sample_event().read_info().read_value(0);
    EXPECT_EQ(402, read_value.id());
    EXPECT_EQ(808, read_value.lost());
    EXPECT_EQ(0x103c5d0, read_value.value());
  }

  // Deserialize and verify events.
  PerfReader out_reader;
  ASSERT_TRUE(out_reader.Deserialize(perf_data_proto));
  EXPECT_EQ(1, out_reader.events().size());
  {
    const PerfEvent& event = out_reader.events().Get(0);
    EXPECT_EQ(PERF_RECORD_SAMPLE, event.header().type());
    ASSERT_TRUE(event.has_sample_event());
    ASSERT_TRUE(event.sample_event().has_read_info());
    const PerfDataProto_ReadInfo_ReadValue& read_value =
        event.sample_event().read_info().read_value(0);
    EXPECT_EQ(402, read_value.id());
    EXPECT_EQ(808, read_value.lost());
    EXPECT_EQ(0x103c5d0, read_value.value());
  }
}

TEST(PerfSerializerTest, SerializesAndDeserializesSampleEventReadWithGroup) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data

  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(
      PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_READ,
      /*sample_id_all=*/true)
      .WithId(401)
      .WithReadFormat(PERF_FORMAT_GROUP | PERF_FORMAT_ID | PERF_FORMAT_LOST)
      .WriteTo(&input);

  // PERF_RECORD_SAMPLE
  testing::ExamplePerfSampleEvent(
      testing::SampleInfo()
          .Ip(0x00000000001c1000)
          .Tid(401)
          .ReadInfo_nr(2)
          .ReadInfo_readvalue(/*value=*/0x103c5d0, /*id=*/402, /*lost=*/808)
          .ReadInfo_readvalue(/*value=*/0x103c5e0, /*id=*/502, /*lost=*/908))
      .WriteTo(&input);

  // Parse and serialize.
  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));
  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));
  ASSERT_EQ(1, perf_data_proto.events_size());
  {
    const PerfDataProto_PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_SAMPLE, event.header().type());
    ASSERT_TRUE(event.has_sample_event());
    ASSERT_TRUE(event.sample_event().has_read_info());
    const PerfDataProto_ReadInfo_ReadValue& read_value_1 =
        event.sample_event().read_info().read_value(0);
    EXPECT_EQ(402, read_value_1.id());
    EXPECT_EQ(808, read_value_1.lost());
    EXPECT_EQ(0x103c5d0, read_value_1.value());
    const PerfDataProto_ReadInfo_ReadValue& read_value_2 =
        event.sample_event().read_info().read_value(1);
    EXPECT_EQ(502, read_value_2.id());
    EXPECT_EQ(908, read_value_2.lost());
    EXPECT_EQ(0x103c5e0, read_value_2.value());
  }

  // Deserialize and verify events.
  PerfReader out_reader;
  ASSERT_TRUE(out_reader.Deserialize(perf_data_proto));
  EXPECT_EQ(1, out_reader.events().size());
  {
    const PerfEvent& event = out_reader.events().Get(0);
    EXPECT_EQ(PERF_RECORD_SAMPLE, event.header().type());
    ASSERT_TRUE(event.has_sample_event());
    ASSERT_TRUE(event.sample_event().has_read_info());
    const PerfDataProto_ReadInfo_ReadValue& read_value_1 =
        event.sample_event().read_info().read_value(0);
    EXPECT_EQ(402, read_value_1.id());
    EXPECT_EQ(808, read_value_1.lost());
    EXPECT_EQ(0x103c5d0, read_value_1.value());
    const PerfDataProto_ReadInfo_ReadValue& read_value_2 =
        event.sample_event().read_info().read_value(1);
    EXPECT_EQ(502, read_value_2.id());
    EXPECT_EQ(908, read_value_2.lost());
    EXPECT_EQ(0x103c5e0, read_value_2.value());
  }
}

TEST(PerfSerializerTest, SerializesAndDeserializesIdIndexEvents) {
  std::stringstream input;

  // header
  testing::ExamplePipedPerfDataFileHeader().WriteTo(&input);

  // data

  // PERF_RECORD_HEADER_ATTR
  testing::ExamplePerfEventAttrEvent_Hardware(PERF_SAMPLE_TID,
                                              /*sample_id_all=*/true)
      .WriteTo(&input);

  // PERF_RECORD_ID_INDEX
  testing::ExampleIdIndexEvent()
      .WithEntry(/*id=*/1234, /*idx=*/4321, /*cpu=*/1, /*tid=*/2)
      .WithEntry(/*id=*/5678, /*idx=*/8765, /*cpu=*/3, /*tid=*/4)
      .WriteTo(&input);

  // Parse and serialize.
  PerfReader reader;
  ASSERT_TRUE(reader.ReadFromString(input.str()));

  PerfDataProto perf_data_proto;
  ASSERT_TRUE(reader.Serialize(&perf_data_proto));

  ASSERT_EQ(1, perf_data_proto.events_size());
  {
    const PerfDataProto_PerfEvent& event = perf_data_proto.events(0);
    EXPECT_EQ(PERF_RECORD_ID_INDEX, event.header().type());
    ASSERT_TRUE(event.has_id_index_event());
    const PerfDataProto::IdIndexEvent& id_index_event = event.id_index_event();
    EXPECT_EQ(2, id_index_event.entries_size());
    EXPECT_EQ(1234, id_index_event.entries(0).id());
    EXPECT_EQ(4321, id_index_event.entries(0).idx());
    EXPECT_EQ(1, id_index_event.entries(0).cpu());
    EXPECT_EQ(2, id_index_event.entries(0).tid());
    EXPECT_EQ(5678, id_index_event.entries(1).id());
    EXPECT_EQ(8765, id_index_event.entries(1).idx());
    EXPECT_EQ(3, id_index_event.entries(1).cpu());
    EXPECT_EQ(4, id_index_event.entries(1).tid());
  }

  // Deserialize and verify events.
  PerfReader out_reader;
  ASSERT_TRUE(out_reader.Deserialize(perf_data_proto));
  EXPECT_EQ(1, out_reader.events().size());
  {
    const PerfEvent& event = out_reader.events().Get(0);
    EXPECT_EQ(PERF_RECORD_ID_INDEX, event.header().type());
    ASSERT_TRUE(event.has_id_index_event());
    const PerfDataProto::IdIndexEvent& id_index_event = event.id_index_event();
    EXPECT_EQ(2, id_index_event.entries_size());
    EXPECT_EQ(1234, id_index_event.entries(0).id());
    EXPECT_EQ(4321, id_index_event.entries(0).idx());
    EXPECT_EQ(1, id_index_event.entries(0).cpu());
    EXPECT_EQ(2, id_index_event.entries(0).tid());
    EXPECT_EQ(5678, id_index_event.entries(1).id());
    EXPECT_EQ(8765, id_index_event.entries(1).idx());
    EXPECT_EQ(3, id_index_event.entries(1).cpu());
    EXPECT_EQ(4, id_index_event.entries(1).tid());
  }
}

namespace {
std::vector<const char*> AllPerfData() {
  const auto& files = perf_test_files::GetPerfDataFiles();
  const auto& piped = perf_test_files::GetPerfPipedDataFiles();

  std::vector<const char*> ret(std::begin(files), std::end(files));
  ret.insert(std::end(ret), std::begin(piped), std::end(piped));
  return ret;
}
}  // namespace

INSTANTIATE_TEST_SUITE_P(
    PerfSerializerTest, SerializePerfDataFiles,
    ::testing::ValuesIn(perf_test_files::GetPerfDataFiles()));
INSTANTIATE_TEST_SUITE_P(PerfSerializerTest, SerializeAllPerfDataFiles,
                         ::testing::ValuesIn(AllPerfData()));
INSTANTIATE_TEST_SUITE_P(
    PerfSerializerTest, SerializePerfDataProtoFiles,
    ::testing::ValuesIn(perf_test_files::GetPerfDataProtoFiles()));
}  // namespace quipper
