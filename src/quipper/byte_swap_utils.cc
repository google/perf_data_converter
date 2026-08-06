// Copyright 2026 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "byte_swap_utils.h"

#include <cstdint>

#include "base/logging.h"              

namespace quipper {

template <class T>
void ByteSwap(T* input) {
  switch (sizeof(T)) {
    case sizeof(uint8_t):
      LOG(WARNING) << "Attempting to byte swap on a single byte.";
      break;
    case sizeof(uint16_t):
      *input = bswap_16(*input);
      break;
    case sizeof(uint32_t):
      *input = bswap_32(*input);
      break;
    case sizeof(uint64_t):
      *input = bswap_64(*input);
      break;
    default:
      LOG(FATAL) << "Invalid size for byte swap: " << sizeof(T) << " bytes";
      break;
  }
}

template void ByteSwap<signed char>(signed char*);
template void ByteSwap<unsigned char>(unsigned char*);
template void ByteSwap<int>(int*);
template void ByteSwap<unsigned int>(unsigned int*);
// For portability we make explicit specialization on all integer types
// instead of using exact-width types.

template void ByteSwap<short>(short*);
template void ByteSwap<unsigned short>(unsigned short*);
template void ByteSwap<long>(long*);
template void ByteSwap<unsigned long>(unsigned long*);
template void ByteSwap<long long>(long long*);
template void ByteSwap<unsigned long long>(unsigned long long*);


}  // namespace quipper
