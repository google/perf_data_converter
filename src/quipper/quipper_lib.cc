// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "quipper_lib.h"

#include "perf_option_parser.h"

namespace {

int StringToInt(const std::string& s) {
  int r = 0;
  std::stringstream ss;
  ss << s;
  ss >> r;
  return r;
}

}  // namespace

bool ParseOldPerfArguments(int argc, const char* argv[], int* duration,
                           std::vector<std::string>* perf_args) {
  if (argc < 3) {
    return false;
  }

  *duration = StringToInt(argv[1]);

  for (int i = 2; i < argc; i++) {
    perf_args->emplace_back(argv[i]);
  }

  return quipper::ValidatePerfCommandLine(*perf_args);
}
