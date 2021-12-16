// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERF_DATA_CONVERTER_SRC_QUIPPER_QUIPPER_LIB_H_
#define PERF_DATA_CONVERTER_SRC_QUIPPER_QUIPPER_LIB_H_

#include <vector>

#include "compat/string.h"

// ParseOldPerfArguments parses the given command line arguments i.e., |argc|
// and |argv| for the below quipper CLI usage:
//   <exe> <duration in seconds> <perf command line>
bool ParseOldPerfArguments(int argc, const char* argv[], int* duration,
                           std::vector<std::string>* perf_args);

std::vector<std::string> SplitString(const std::string& str,
                                     const char delimiter);

#endif  // PERF_DATA_CONVERTER_SRC_QUIPPER_QUIPPER_LIB_H_
