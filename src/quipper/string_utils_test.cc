// Copyright (c) 2023 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "string_utils.h"

#include <cstdint>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::ElementsAre;

namespace quipper {

TEST(StringUtilsTest, RootPathExtraction) {
  EXPECT_EQ(RootPath("/aa"), "/");
  EXPECT_EQ(RootPath("//aa"), "//");
  EXPECT_EQ(RootPath("/aa/bb"), "/aa");
  EXPECT_EQ(RootPath("//aa//bb"), "//aa");
  EXPECT_EQ(RootPath("/aa/bb/cc"), "/aa/bb");
  EXPECT_EQ(RootPath("//aa//bb//cc"), "//aa//bb");
  EXPECT_EQ(RootPath("/aa/bb/cc/dd"), "/aa/bb");
  EXPECT_EQ(RootPath("//aa//bb//cc//dd"), "//aa//bb");
  EXPECT_EQ(RootPath("[vdso]"), "");
  EXPECT_EQ(RootPath("[anon:Mem0x20000]"), "[anon:");
  EXPECT_EQ(RootPath("/memfd:temp_file (deleted)"), "/memfd:");
  EXPECT_EQ(RootPath("/tmp/aa)"), "/tmp");
  EXPECT_EQ(RootPath("/tmp/aa/bb)"), "/tmp");
  EXPECT_EQ(RootPath("/tmpdir/aa/bb)"), "/tmpdir/aa");
}

TEST(StringUtilsTest, ParseCPUNumbers) {
  std::vector<uint32_t> result;
  ParseCPUNumbers("0", result);
  EXPECT_THAT(result, ElementsAre(0));
  result.clear();
  ParseCPUNumbers("0,3", result);
  EXPECT_THAT(result, ElementsAre(0, 3));
  result.clear();
  ParseCPUNumbers("0-3", result);
  EXPECT_THAT(result, ElementsAre(0, 1, 2, 3));
  result.clear();
  ParseCPUNumbers("0-3,5-7", result);
  EXPECT_THAT(result, ElementsAre(0, 1, 2, 3, 5, 6, 7));
  result.clear();
  EXPECT_FALSE(ParseCPUNumbers("", result));
  EXPECT_FALSE(ParseCPUNumbers(",", result));
  EXPECT_FALSE(ParseCPUNumbers("a", result));
  EXPECT_FALSE(ParseCPUNumbers("0,1-2-3", result));
}

TEST(StringUtilsTest, FormatCPUNumbers) {
  EXPECT_EQ(FormatCPUNumbers({0}), "0");
  EXPECT_EQ(FormatCPUNumbers({0, 3}), "0,3");
  EXPECT_EQ(FormatCPUNumbers({0, 1, 2, 3}), "0-3");
  EXPECT_EQ(FormatCPUNumbers({0, 1, 2, 3, 5, 6, 7}), "0-3,5-7");
}

}  // namespace quipper
