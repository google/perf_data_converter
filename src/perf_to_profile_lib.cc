/*
 * Copyright (c) 2018, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/perf_to_profile_lib.h"

#include <sys/stat.h>
#include <sstream>

bool FileExists(const string& path) {
  struct stat file_stat;
  return stat(path.c_str(), &file_stat) != -1;
}

string ReadFileToString(const string& path) {
  std::ifstream perf_file(path);
  if (!perf_file.is_open()) {
    LOG(FATAL) << "Failed to open file: " << path;
  }
  std::ostringstream ss;
  ss << perf_file.rdbuf();
  return ss.str();
}

perftools::ProcessProfiles StringToProfiles(const string& data,
                                            uint32 sample_labels,
                                            uint32 options) {
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

void CreateFile(const string& path, std::ofstream* file,
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
  LOG(INFO) << "perf_to_profile -i <input perf data> -o <output profile> [-f]";
  LOG(INFO) << "If the -f option is given, overwrite the existing output "
            << "profile.";
}

bool ParseArguments(int argc, const char* argv[], string* input, string* output,
                    bool* overwrite_output) {
  *input = "";
  *output = "";
  *overwrite_output = false;
  int opt;
  while ((opt = getopt(argc, const_cast<char* const*>(argv), ":fi:o:")) != -1) {
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
      case ':':
        LOG(ERROR) << "Must provide arguments for flags -i and -o";
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
