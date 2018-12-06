// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_reader.h"

#include <string.h>

#include <cstdint>
#include <memory>

namespace quipper {

FileReader::FileReader(const string& filename) {
  infile_ = fopen(filename.c_str(), "rb");
  if (!IsOpen()) {
    size_ = 0;
    return;
  }
  // Determine the size of the file.
  fseek(infile_, 0, SEEK_END);
  size_ = ftell(infile_);

  // Reset to the beginning of the file.
  fseek(infile_, 0, SEEK_SET);
}

FileReader::~FileReader() {
  if (IsOpen()) {
    fclose(infile_);
  }
}

bool FileReader::ReadData(const size_t size, void* dest) {
  long tell = ftell(infile_);  
  if (tell < 0) {
    return false;
  }
  size_t offset = static_cast<size_t>(tell);
  if (offset > SIZE_MAX - size || offset + size > size_) {
    return false;
  }
  if (fread(dest, 1, size, infile_) < size) {
    return false;
  }
  return true;
}

bool FileReader::ReadString(const size_t size, string* str) {
  if (!ReadDataString(size, str)) return false;

  // Truncate anything after a terminating null.
  size_t actual_length = strnlen(str->data(), size);
  str->resize(actual_length);
  return true;
}

}  // namespace quipper
