/*
 * Copyright (c) 2018, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/perf_to_profile_lib.h"

#include <sys/stat.h>
#include <optional>
#include <string_view>

#include "absl/strings/str_split.h"

namespace {

// Returns the SampleLabels value for the given label name, or std::nullopt if
// unrecognized. Names match the label key constants in perf_data_converter.h.
std::optional<perftools::SampleLabels> LabelNameToValue(
    std::string_view name) {
  if (name == "pid") return perftools::kPidLabel;
  if (name == "tid") return perftools::kTidLabel;
  if (name == "timestamp_ns") return perftools::kTimestampNsLabel;
  if (name == "execution_mode") return perftools::kExecutionModeLabel;
  if (name == "comm") return perftools::kCommLabel;
  if (name == "thread_type") return perftools::kThreadTypeLabel;
  if (name == "thread_comm") return perftools::kThreadCommLabel;
  if (name == "cgroup") return perftools::kCgroupLabel;
  if (name == "code_page_size") return perftools::kCodePageSizeLabel;
  if (name == "data_page_size") return perftools::kDataPageSizeLabel;
  if (name == "cpu") return perftools::kCpuLabel;
  if (name == "cache_latency") return perftools::kCacheLatencyLabel;
  if (name == "data_src") return perftools::kDataSrcLabel;
  if (name == "total_latency") return perftools::kTotalLatencyLabel;
  if (name == "issue_latency") return perftools::kIssueLatencyLabel;
  if (name == "translation_latency") return perftools::kTranslationLatencyLabel;
  return std::nullopt;
}

// Parses a comma-separated list of label names into a bitmask of
// SampleLabels values. Returns false if any name is unrecognized.
bool ParseSampleLabels(const char* arg, uint32_t* sample_labels) {
  *sample_labels = perftools::kNoLabels;
  for (std::string_view name : absl::StrSplit(arg, ',')) {
    auto value = LabelNameToValue(name);
    if (!value.has_value()) {
      LOG(ERROR) << "Unknown sample label: " << name;
      return false;
    }
    *sample_labels |= *value;
  }
  return true;
}

}  // namespace

bool FileExists(const std::string& path) {
  struct stat file_stat;
  return stat(path.c_str(), &file_stat) != -1;
}

std::string ReadFileToString(const std::string& path) {
  std::ifstream perf_file(path);
  if (!perf_file.is_open()) {
    LOG(FATAL) << "Failed to open file: " << path;
  }
  std::ostringstream ss;
  ss << perf_file.rdbuf();
  return ss.str();
}

perftools::ProcessProfiles StringToProfiles(const std::string& data,
                                            uint32_t sample_labels,
                                            uint32_t options) {
  // Try to parse it as a PerfDataProto.
  quipper::PerfDataProto perf_data_proto;
  if (perf_data_proto.ParseFromArray(data.data(), data.length())) {
    return perftools::PerfDataProtoToProfiles(&perf_data_proto, sample_labels,
                                              options);
  }
  // Fallback to reading input as a perf.data file.
  return perftools::RawPerfDataToProfiles(data.data(), data.length(), {},
                                          sample_labels, options);
}

void CreateFile(const std::string& path, std::ofstream* file,
                bool overwrite_output) {
  if (!overwrite_output && FileExists(path)) {
    LOG(FATAL) << "File already exists: " << path;
  }
  file->open(path, std::ios_base::trunc);
  if (!file->is_open()) {
    LOG(FATAL) << "Failed to open file: " << path;
  }
}

void PrintUsage() {
  LOG(INFO) << "Usage:";
  LOG(INFO) << "perf_to_profile -i <input perf data> -o <output profile> [-f] "
            << "[-l <labels>]";
  LOG(INFO) << "If the -f option is given, overwrite the existing output "
            << "profile.";
  LOG(INFO) << "If the -j option is given, allow unaligned MMAP events "
            << "required by perf data from VMs with JITs.";
  LOG(INFO) << "If the -l option is given, add the specified sample labels to "
            << "the output profile. Labels are a comma-separated list of: "
            << "pid, tid, timestamp_ns, execution_mode, comm, thread_type, "
            << "thread_comm, cgroup, code_page_size, data_page_size, cpu, "
            << "cache_latency, data_src, total_latency, issue_latency, "
            << "translation_latency.";
}

bool ParseArguments(int argc, const char* argv[], std::string* input,
                    std::string* output, bool* overwrite_output,
                    bool* allow_unaligned_jit_mappings,
                    uint32_t* sample_labels) {
  *input = "";
  *output = "";
  *overwrite_output = false;
  *allow_unaligned_jit_mappings = false;
  *sample_labels = perftools::kNoLabels;
  int opt;
  while ((opt = getopt(argc, const_cast<char* const*>(argv), ":jfi:o:l:")) !=
         -1) {
    switch (opt) {
      case 'i':
        *input = optarg;
        break;
      case 'o':
        *output = optarg;
        break;
      case 'f':
        *overwrite_output = true;
        break;
      case 'j':
        *allow_unaligned_jit_mappings = true;
        break;
      case 'l':
        if (!ParseSampleLabels(optarg, sample_labels)) {
          return false;
        }
        break;
      case ':':
        LOG(ERROR) << "Must provide arguments for flags -i, -o, and -l";
        return false;
      case '?':
        LOG(ERROR) << "Invalid option: " << static_cast<char>(optopt);
        return false;
      default:
        LOG(ERROR) << "Invalid option: " << static_cast<char>(opt);
        return false;
    }
  }
  return !input->empty() && !output->empty();
}
