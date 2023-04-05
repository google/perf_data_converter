// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_DSO_TEST_UTILS_H_
#define CHROMIUMOS_WIDE_PROFILING_DSO_TEST_UTILS_H_

#include <string>
#include <utility>
#include <vector>

namespace quipper {
namespace testing {

void WriteElfWithBuildid(std::string filename, std::string section_name,
                         std::string buildid);
// Note: an ELF with multiple buildid notes is unusual, but useful for testing.
void WriteElfWithMultipleBuildids(
    std::string filename,
    const std::vector<std::pair<std::string, std::string>> section_buildids);

}  // namespace testing
}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_DSO_TEST_UTILS_H_
