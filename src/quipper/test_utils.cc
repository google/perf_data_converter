// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_utils.h"

#include <string.h>

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include <gflags/gflags.h>
#include "base/logging.h"
#include "compat/proto.h"
#include "file_reader.h"
#include "file_utils.h"
#include "perf_protobuf_io.h"
#include "run_command.h"
#include "string_utils.h"

using quipper::PerfDataProto;
using quipper::SplitString;
using quipper::TextFormat;

namespace {

// When not empty, failing tests are written in this folder.
DEFINE_string(new_golden_file_path, "",
              "Folder where to save new golden files");

// This flag enables comparisons of protobufs in serialized format as a faster
// alternative to comparing their human-readable text representations. Set this
// flag to false to compare text representations instead. It's also useful for
// diffing against the old golden files when writing new golden files.
DEFINE_bool(use_protobuf_data_format, true,
"If false, use text format instead of protobuf");

// Newline character.
const char kNewLineDelimiter = '\n';

// Extension of protobuf files in text format.
const char kProtobufTextExtension[] = ".pb_text";

// Extension of protobuf files in serialized format.
const char kProtobufDataExtension[] = ".pb_data";

// Extension of build ID lists.
const char kBuildIDListExtension[] = ".buildids";

enum PerfDataType {
  kPerfDataNormal,  // Perf data is in normal format.
  kPerfDataPiped,   // Perf data is in piped format.
};

// The piped commands above produce comma-separated lines with the following
// fields:
enum {
  PERF_REPORT_OVERHEAD,
  PERF_REPORT_SAMPLES,
  PERF_REPORT_COMMAND,
  PERF_REPORT_SHARED_OBJECT,
  NUM_PERF_REPORT_FIELDS,
};

// Split a char buffer into separate lines.
void SeparateLines(const std::vector<char>& bytes,
                   std::vector<std::string>* lines) {
  if (!bytes.empty())
    SplitString(std::string(&bytes[0], bytes.size()), kNewLineDelimiter, lines);
}

bool ReadExistingProtobufText(const std::string& filename,
                              std::string* output_string) {
  std::vector<char> output_buffer;
  if (!quipper::FileToBuffer(filename, &output_buffer)) {
    LOG(ERROR) << "Could not open file " << filename;
    return false;
  }
  output_string->assign(&output_buffer[0], output_buffer.size());
  return true;
}

// Given a perf data file, return its protobuf representation as a text string
// and/or a serialized data stream.
bool PerfDataToProtoRepresentation(const std::string& filename,
                                   std::string* output_text,
                                   std::string* output_data) {
  PerfDataProto perf_data_proto;
  quipper::PerfParserOptions options = quipper::GetMinimalProcessingOptions();
  if (!SerializeFromFileWithOptions(filename, options, &perf_data_proto)) {
    return false;
  }
  // Reset the timestamp field since it causes reproducability issues when
  // testing.
  perf_data_proto.set_timestamp_sec(0);

  if (output_text && !TextFormat::PrintToString(perf_data_proto, output_text)) {
    return false;
  }
  if (output_data && !perf_data_proto.SerializeToString(output_data))
    return false;

  return true;
}

}  // namespace

namespace quipper {

const char* kSupportedMetadata[] = {
    "hostname",
    "os release",
    "perf version",
    "arch",
    "nrcpus online",
    "nrcpus avail",
    "cpudesc",
    "cpuid",
    "total memory",
    "cmdline",
    "event",
    "sibling cores",    // CPU topology.
    "sibling threads",  // CPU topology.
    "node0 meminfo",    // NUMA topology.
    "node0 cpu list",   // NUMA topology.
    "node1 meminfo",    // NUMA topology.
    "node1 cpu list",   // NUMA topology.
    NULL,
};
std::string GetTestInputFilePath(const std::string& filename) {
  #ifdef GITHUB_BAZEL
    return "src/quipper/testdata/" + filename;
  # else
    return "testdata/" + filename;
  #endif
}

std::string GetPerfPath() {
  return "/usr/bin/perf";
}

int64_t GetFileSize(const std::string& filename) {
  FileReader reader(filename);
  if (!reader.IsOpen()) return -1;
  return reader.size();
}

bool CompareFileContents(const std::string& filename1,
                         const std::string& filename2) {
  std::vector<char> file1_contents;
  std::vector<char> file2_contents;
  if (!FileToBuffer(filename1, &file1_contents) ||
      !FileToBuffer(filename2, &file2_contents)) {
    return false;
  }

  return file1_contents == file2_contents;
}

bool GetPerfBuildIDMap(const std::string& filename,
                       std::map<std::string, std::string>* output) {
  // Try reading from a pre-generated report.  If it doesn't exist, call perf
  // buildid-list.
  std::vector<char> buildid_list;
  LOG(INFO) << filename + kBuildIDListExtension;
  if (!quipper::FileToBuffer(filename + kBuildIDListExtension, &buildid_list)) {
    buildid_list.clear();
    if (RunCommand({GetPerfPath(), "buildid-list", "--force", "-i", filename},
                   &buildid_list) != 0) {
      LOG(ERROR) << "Failed to run perf buildid-list";
      return false;
    }
  }
  std::vector<std::string> lines;
  SeparateLines(buildid_list, &lines);

  /* The output now looks like the following:
     cff4586f322eb113d59f54f6e0312767c6746524 [kernel.kallsyms]
     c099914666223ff6403882604c96803f180688f5 /lib64/libc-2.15.so
     7ac2d19f88118a4970adb48a84ed897b963e3fb7 /lib64/libpthread-2.15.so
  */
  output->clear();
  for (std::string line : lines) {
    TrimWhitespace(&line);
    size_t separator = line.find(' ');
    std::string build_id = line.substr(0, separator);
    std::string filename = line.substr(separator + 1);
    (*output)[filename] = build_id;
  }

  return true;
}

bool MaybeWriteGolden(const std::string& protobuf_representation,
                      const std::string& golden_filename) {
  if (std::string(FLAGS_new_golden_file_path).empty()) {
    return true;
  }
  if (protobuf_representation.empty()) {
    LOG(ERROR) << "Must provide a string proto.";
    return false;
  }
  if (golden_filename.empty()) {
    LOG(ERROR) << "Must provide a golden file name.";
    return false;
  }
  std::string new_golden_path =
      std::string(FLAGS_new_golden_file_path) + "/" +
      golden_filename;
  LOG(INFO) << "Writing new golden file: " << new_golden_path;
  if (!BufferToFile(new_golden_path, protobuf_representation)) {
    LOG(ERROR) << "Failed to write new golden file: " << new_golden_path;
    return false;
  }
  return true;
}

bool MaybeWriteGolden(const Message& proto,
                      const std::string& golden_filename) {
  if (std::string(FLAGS_new_golden_file_path).empty()) {
    return true;
  }
  std::string protobuf_representation;
  if (!TextFormat::PrintToString(proto, &protobuf_representation)) {
    LOG(ERROR) << "Failed to serialize new proto to string.";
    return false;
  }
  return MaybeWriteGolden(protobuf_representation, golden_filename);
}

bool CheckPerfDataAgainstBaseline(const std::string& perfdata_filepath,
                                  const std::string& baseline_filename,
                                  std::string* difference) {
  std::string extension = FLAGS_use_protobuf_data_format
                              ? kProtobufDataExtension
                              : kProtobufTextExtension;
  std::string golden_name = baseline_filename;
  if (baseline_filename.empty()) {
    golden_name = basename(perfdata_filepath.c_str());
  }
  std::string golden_path = GetTestInputFilePath(golden_name) + extension;
  if (difference != nullptr) {
    difference->clear();
  }

  bool matches_baseline = false;
  std::string protobuf_representation, baseline;
  if (!ReadExistingProtobufText(golden_path, &baseline)) {
    LOG(ERROR) << "Failed to read existing golden file: " << golden_path;
  }
  if (FLAGS_use_protobuf_data_format) {
    if (!PerfDataToProtoRepresentation(perfdata_filepath, nullptr,
                                       &protobuf_representation)) {
      LOG(ERROR) << "Failed to parse perfdata file: " << perfdata_filepath;
      return false;
    }
    matches_baseline = (baseline == protobuf_representation);
    if (!matches_baseline) {
      MaybeWriteGolden(protobuf_representation, golden_name + extension);
    }
  } else {
    PerfDataProto actual, expected;
    PerfParserOptions options = GetMinimalProcessingOptions();
    if (!SerializeFromFileWithOptions(perfdata_filepath, options, &actual)) {
      LOG(ERROR) << "Failed to parse perfdata file: " << perfdata_filepath;
      return false;
    }
    // Reset the timestamp field because it causes reproducability issues when
    // testing.
    actual.set_timestamp_sec(0);

    if (!TextFormat::ParseFromString(baseline, &expected)) {
      LOG(ERROR) << "Failed to parse proto from golden text proto.";
    }
    matches_baseline = EqualsProto(actual, expected, difference);
    if (!matches_baseline) {
      MaybeWriteGolden(actual, golden_name + extension);
    }
  }
  return matches_baseline;
}

bool ComparePerfBuildIDLists(const std::string& file1,
                             const std::string& file2) {
  std::map<std::string, std::string> output1;
  std::map<std::string, std::string> output2;

  // Generate a build id list for each file.
  CHECK(GetPerfBuildIDMap(file1, &output1));
  CHECK(GetPerfBuildIDMap(file2, &output2));

  // Compare the output strings.
  return output1 == output2;
}

PerfParserOptions GetTestOptions() {
  PerfParserOptions options;
  options.sample_mapping_percentage_threshold = 100.0f;
  return options;
}

PerfParserOptions GetMinimalProcessingOptions() {
  // Explicitly set the most permissive options and disable any heuristic based
  // transformations, regardless of the default option values.
  PerfParserOptions options;
  options.do_remap = false;
  options.discard_unused_events = false;
  options.sample_mapping_percentage_threshold = 0.f;
  options.sort_events_by_time = false;
  options.read_missing_buildids = false;
  options.deduce_huge_page_mappings = false;
  options.combine_mappings = false;
  options.allow_unaligned_jit_mappings = true;
  return options;
}

std::string GenerateBinaryTrace(const std::vector<std::string>& packets) {
  std::string trace("");
  for (const std::string& packet : packets) {
    std::stringstream ss(packet);
    std::string byte_str;
    while (ss >> byte_str) {
      trace += (char)std::strtol(
          byte_str.data(), nullptr,
          16);  
    }
  }
  return trace;
}

}  // namespace quipper
