// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "conversion_utils.h"

#include "base/logging.h"

#include "compat/string.h"
#include "compat/test.h"
#include "perf_test_files.h"
#include "scoped_temp_path.h"
#include "test_utils.h"

namespace quipper {

class PerfFile : public ::testing::TestWithParam<const char*> {};

TEST_P(PerfFile, TextOutput) {
  ScopedTempDir output_dir;
  ASSERT_FALSE(output_dir.path().empty());
  const std::string output_path = output_dir.path();
  const std::string test_file = GetParam();

  FormatAndFile input, output;

  input.filename = GetTestInputFilePath(test_file);
  input.format = kPerfFormat;
  output.filename = output_path + test_file + ".pb_text";
  output.format = kProtoTextFormat;
  EXPECT_TRUE(ConvertFile(input, output));

  std::string golden_file =
      GetTestInputFilePath(std::string(test_file) + ".pb_text");
  LOG(INFO) << "golden: " << golden_file;
  LOG(INFO) << "output: " << output.filename;

  CompareProtoFiles<PerfDataProto>(kProtoTextFormat, output.filename,
                                   golden_file,
                                   basename(output.filename.c_str()));
}

TEST_P(PerfFile, BinaryOutput) {
  ScopedTempDir output_dir;
  ASSERT_FALSE(output_dir.path().empty());
  const std::string output_path = output_dir.path();
  const std::string test_file = GetParam();

  FormatAndFile input, output;

  input.filename = GetTestInputFilePath(test_file);
  input.format = kPerfFormat;
  output.filename = output_path + test_file + ".pb_data";
  output.format = kProtoBinaryFormat;
  EXPECT_TRUE(ConvertFile(input, output));

  std::string golden_file =
      GetTestInputFilePath(std::string(test_file) + ".pb_data");
  LOG(INFO) << "golden: " << golden_file;
  LOG(INFO) << "output: " << output.filename;

  CompareProtoFiles<PerfDataProto>(kProtoBinaryFormat, output.filename,
                                   golden_file,
                                   basename(output.filename.c_str()));
}

INSTANTIATE_TEST_SUITE_P(
    ConversionUtilsTest, PerfFile,
    ::testing::ValuesIn(perf_test_files::GetPerfDataFiles()));
}  // namespace quipper
