// Copyright 2026 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_BYTE_SWAP_UTILS_H_
#define CHROMIUMOS_WIDE_PROFILING_BYTE_SWAP_UTILS_H_

#include <byteswap.h>
#include <stdint.h>

namespace quipper {

// Swaps the byte order of 16-bit, 32-bit, and 64-bit unsigned integers.
template <class T>
void ByteSwap(T* input);

// Swaps byte order of |value| if the |swap| flag is set. This function is
// trivial but it avoids filling code with "if (swap) { ... } " statements.
template <typename T>
T MaybeSwap(T value, bool swap) {
  if (swap) ByteSwap(&value);
  return value;
}

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_BYTE_SWAP_UTILS_H_
