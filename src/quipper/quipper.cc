// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <string>

#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>
#include "base/logging.h"
#include "compat/string.h"
#include "file_utils.h"
#include "perf_protobuf_io.h"
#include "perf_recorder.h"
#include "quipper_lib.h"

namespace {

DEFINE_int64(duration, 0, "Duration to run perf in seconds");
DEFINE_string(perf_path, "", "Path to perf");
DEFINE_string(output_file, "/dev/stdout",
              "Path to store the output perf_data.pb.data");

bool ParsePerfArguments(int argc, const char* argv[], int* duration,
                        std::vector<string>* perf_args, string* output_file) {
  if (argc < 2) {
    return false;
  }

  *duration = FLAGS_duration;
  if (*duration <= 0) return false;

  string perf_path = FLAGS_perf_path;
  if (perf_path.empty()) return false;

  perf_args->emplace_back(perf_path);

  for (int i = 1; i < argc; i++) {
    perf_args->emplace_back(argv[i]);
  }

  *output_file = FLAGS_output_file;
  if (output_file->empty()) return false;

  return true;
}

bool RecordPerf(int perf_duration, const std::vector<string>& perf_args,
                const string& output_file) {
  quipper::PerfRecorder perf_recorder;
  string output_string;
  if (!perf_recorder.RunCommandAndGetSerializedOutput(perf_args, perf_duration,
                                                      &output_string)) {
    LOG(ERROR) << "Couldn't record perf";
    return false;
  }
  if (!quipper::BufferToFile(output_file, output_string)) {
    LOG(ERROR) << "Couldn't write perf_data.pb.data at " << output_file;
    return false;
  }
  return true;
}


}  // namespace

// Usage is:
// To run perf using quipper and generate a perf_data.pb.data:
//   <exe> --duration <duration in seconds>
//         --perf_path <path to perf>
//         --output_file <path to store the output perf_data.pb.data>
//         --
//         <perf arguments>
//  or the old way, this is temporarily supported, without any flags:
//   <exe> <duration in seconds> <perf command line>

int main(int argc, char* argv[]) {
  std::vector<string> perf_args;
  int perf_duration = 0;

  if (ParseOldPerfArguments(argc, const_cast<const char**>(argv),
                            &perf_duration, &perf_args)) {
    return RecordPerf(perf_duration, perf_args, "/dev/stdout") ? EXIT_SUCCESS
                                                               : EXIT_FAILURE;
  }

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  string output_file;
  perf_args.clear();

  if (ParsePerfArguments(argc, const_cast<const char**>(argv), &perf_duration,
                         &perf_args, &output_file)) {
    return RecordPerf(perf_duration, perf_args, output_file) ? EXIT_SUCCESS
                                                             : EXIT_FAILURE;
  }

  LOG(ERROR) << "Invalid command line.";
  LOG(ERROR) << "Usage: " << argv[0] << " --duration <duration in seconds>"
             << " --perf_path <path to perf>"
             << " --output_file <path to store the output perf_data.pb.data>"
             << " -- <perf arguments>"
             << "\nor\n"
             << argv[0] << " <duration in seconds>"
             << " <path to perf>"
             << " <perf arguments>";
  return EXIT_FAILURE;
}
