// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <sstream>

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

std::vector<std::string> SplitString(const std::string& str,
                                     const char delimiter) {
  std::vector<std::string> result;
  if (str.empty()) return result;

  size_t start = 0;
  while (start != std::string::npos) {
    size_t end = str.find_first_of(delimiter, start);
    string piece;
    if (end == std::string::npos) {
      piece = str.substr(start);
      start = std::string::npos;
    } else {
      piece = str.substr(start, end - start);
      start = end + 1;
    }

    if (!piece.empty()) result.emplace_back(piece);
  }
  return result;
}
