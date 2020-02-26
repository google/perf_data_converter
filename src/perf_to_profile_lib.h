/*
 * Copyright (c) 2018, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PERFTOOLS_PERF_TO_PROFILE_LIB_H_
#define PERFTOOLS_PERF_TO_PROFILE_LIB_H_

#include <unistd.h>
#include <fstream>

#include "src/quipper/base/logging.h"
#include "src/compat/string_compat.h"
#include "src/perf_data_converter.h"
#include "src/quipper/perf_data.pb.h"

// Checks and returns whether or not the file at the given |path| already
// exists.
bool FileExists(const std::string& path);

// Reads a file at the given |path| as a string and returns it.
std::string ReadFileToString(const std::string& path);

// Generates profiles from either a raw perf.data string or perf data proto
// string. Returns a vector of process profiles, empty if any error occurs.
perftools::ProcessProfiles StringToProfiles(
    const std::string& data, uint32 sample_labels = perftools::kNoLabels,
    uint32 options = perftools::kNoOptions);

// Creates a file at the given |path|. If |overwriteOutput| is set to true,
// overwrites the file at the given path.
void CreateFile(const std::string& path, std::ofstream* file,
                bool overwriteOutput);

// Parses arguments, stores the results in |input|, |output| and
// |overwriteOutput|, and returns true if arguments parsed successfully and
// false otherwise.
bool ParseArguments(int argc, const char* argv[], std::string* input,
                    std::string* output, bool* overwriteOutput);

// Prints the usage of the tool.
void PrintUsage();

#endif  // PERFTOOLS_PERF_TO_PROFILE_LIB_H_
