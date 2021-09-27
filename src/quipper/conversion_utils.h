// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_CONVERSION_UTILS_H_
#define CHROMIUMOS_WIDE_PROFILING_CONVERSION_UTILS_H_

#include <string>

#include "compat/string.h"

namespace quipper {

// Format string for perf.data.
extern const char kPerfFormat[];

// Format string for protobuf text format.
extern const char kProtoTextFormat[];

// Format string for protobuf binary format.
extern const char kProtoBinaryFormat[];

// Structure to hold the format and file of an input or output.
struct FormatAndFile {
  // The name of the file.
  std::string filename;

  // The format of the file. Options are "perf" for perf data files, "text" for
  // proto text files and "proto" for proto binary files.
  std::string format;
};

// Convert a perf file from one format to another.
bool ConvertFile(const FormatAndFile& input, const FormatAndFile& output);

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_CONVERSION_UTILS_H_
