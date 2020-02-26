/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/perf_to_profile_lib.h"

#include "src/quipper/base/logging.h"
#include "src/perf_data_converter.h"

int main(int argc, char** argv) {
  std::string input, output;
  bool overwriteOutput = false;
  if (!ParseArguments(argc, const_cast<const char**>(argv), &input, &output,
                      &overwriteOutput)) {
    PrintUsage();
    return EXIT_FAILURE;
  }

  const auto profiles = StringToProfiles(ReadFileToString(input));

  // With kNoOptions, all of the PID profiles should be merged into a
  // single one.
  if (profiles.size() != 1) {
    LOG(FATAL) << "Expected profile vector to have one element.";
  }
  const auto& profile = profiles[0]->data;
  std::ofstream outFile;
  CreateFile(output, &outFile, overwriteOutput);
  profile.SerializeToOstream(&outFile);
  return EXIT_SUCCESS;
}
