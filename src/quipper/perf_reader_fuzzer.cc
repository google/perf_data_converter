// Copyright (c) 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_reader.h"

#include <vector>

#include "base/logging.h"
#include "base/test/fuzzed_data_provider.h"

#include "buffer_reader.h"
#include "compat/string.h"
#include "compat/test.h"

namespace quipper {

namespace {

class Environment {
 public:
  Environment() {
    // The
    // https://chromium.googlesource.com/chromiumos/docs/+/master/fuzzing.md#Write-a-fuzzer
    // doc explicitly says to disable logging during fuzzing in Chromium.
    logging::SetMinLogLevel(logging::LOG_FATAL);  // <- DISABLE LOGGING.
  }
};

}  // namespace

// Entry point for libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static Environment env;
  const char *char_data = reinterpret_cast<const char *>(data);

  PerfReader pr;
  pr.ReadFromPointer(char_data, size);

  string raw_output;
  pr.WriteToString(&raw_output);

  quipper::PerfDataProto proto_output;
  pr.Serialize(&proto_output);

  return 0;
}

}  // namespace quipper
