// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer_reader.h"

#include <string.h>

#include <cstdint>

#include "base/logging.h"

namespace quipper {

bool BufferReader::SeekSet(size_t offset) {
  if (offset > size_) {
    LOG(ERROR) << "Illegal offset " << offset << " in file of size " << size_;
    return false;
  }
  offset_ = offset;
  return true;
}

bool BufferReader::ReadData(const size_t size, void* dest) {
  if (offset_ > SIZE_MAX - size || offset_ + size > size_) return false;

  memcpy(dest, buffer_ + offset_, size);
  offset_ += size;
  return true;
}

bool BufferReader::ReadString(size_t size, std::string* str) {
  if (offset_ > SIZE_MAX - size || offset_ + size > size_) return false;

  size_t actual_length = strnlen(buffer_ + offset_, size);
  *str = std::string(buffer_ + offset_, actual_length);
  offset_ += size;
  return true;
}

}  // namespace quipper
