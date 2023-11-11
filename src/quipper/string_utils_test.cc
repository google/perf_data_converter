// Copyright (c) 2023 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "string_utils.h"

#include "compat/test.h"

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
}

}  // namespace quipper
