// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_STRING_UTILS_H_
#define CHROMIUMOS_WIDE_PROFILING_STRING_UTILS_H_

#include <sstream>
#include <vector>

namespace quipper {

// Trim leading and trailing whitespace from |str|.
void TrimWhitespace(std::string* str);

// Splits a character array by |delimiter| into a vector of strings tokens.
void SplitString(const std::string& str, char delimiter,
                 std::vector<std::string>* tokens);

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_STRING_UTILS_H_
