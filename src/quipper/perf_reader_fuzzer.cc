// Copyright (c) 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_reader.h"

#include <vector>

#include "base/logging.h"

#include "buffer_reader.h"
#include "compat/string.h"
#include "compat/test.h"

namespace quipper {

// Entry point for libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  const char *char_data = reinterpret_cast<const char *>(data);

  PerfReader pr;
  if (!pr.ReadFromPointer(char_data, size)) {
    // Do not proceed to the serialization tests below if reading the file
    // fails. The reader makes no guarantee about its state in case of failing
    // to read the perf.data file so proceeding would be undefined behavior.
    return 0;
  }

  std::string raw_output;
  pr.WriteToString(&raw_output);

  quipper::PerfDataProto proto_output;
  pr.Serialize(&proto_output);

  return 0;
}

}  // namespace quipper
