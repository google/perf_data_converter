// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_STRING_UTILS_H_
#define CHROMIUMOS_WIDE_PROFILING_STRING_UTILS_H_

#include <cstdint>
#include <string>
#include <vector>

namespace quipper {

// Trim leading and trailing whitespace from |str|.
void TrimWhitespace(std::string* str);

// Splits a character array by |delimiter| into a vector of strings tokens.
void SplitString(const std::string& str, char delimiter,
                 std::vector<std::string>* tokens);

// Extract the first two levels of directories of |filename| starting from root.
std::string RootPath(const std::string& filename);

// ParseCPUNumbers takes a range of CPUs, which could be a raw
// number, hyphen separated range or comma separated list/range, and converts
// it into a vector.
// For example, "0-1,3" -> [0, 1, 3]
bool ParseCPUNumbers(const std::string& cpus, std::vector<uint32_t>& result);

// FormatCPUNumbers converts cpus to Linux kernel styled string.
// For example, [0, 1, 3] -> "0-1,3"
std::string FormatCPUNumbers(const std::vector<uint32_t>& cpus);

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_STRING_UTILS_H_
