// Copyright 2026 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "byte_swap_utils.h"

#include <cstdint>

#include "compat/test.h"

namespace quipper {

TEST(ByteSwapUtilsTest, ByteSwap16) {
  uint16_t val = 0x1234;
  ByteSwap(&val);
  EXPECT_EQ(val, 0x3412);
}

TEST(ByteSwapUtilsTest, ByteSwap32) {
  uint32_t val = 0x12345678;
  ByteSwap(&val);
  EXPECT_EQ(val, 0x78563412);
}

TEST(ByteSwapUtilsTest, ByteSwap64) {
  uint64_t val = 0x123456789abcdef0ULL;
  ByteSwap(&val);
  EXPECT_EQ(val, 0xf0debc9a78563412ULL);
}

TEST(ByteSwapUtilsTest, MaybeSwap) {
  uint32_t val = 0x12345678;

  // With swap = false, value should remain the same.
  EXPECT_EQ(MaybeSwap(val, false), 0x12345678);

  // With swap = true, value should be swapped.
  EXPECT_EQ(MaybeSwap(val, true), 0x78563412);
}

}  // namespace quipper
