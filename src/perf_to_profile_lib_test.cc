/*
 * Copyright (c) 2018, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/perf_to_profile_lib.h"

#include "src/quipper/base/logging.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

using ::testing::Eq;

TEST(PerfToProfileTest, ParseArguments) {
  struct Test {
    std::string desc;
    std::vector<const char*> argv;
    std::string expected_input;
    std::string expected_output;
    bool expected_overwrite_output;
    bool allow_unaligned_jit_mappings;
    bool want_error;
  };

  std::vector<Test> tests;
  tests.push_back(Test{
      .desc = "With input, output and overwrite flags",
      .argv = {"<exec>", "-i", "input_perf_file", "-o", "output_profile", "-f"},
      .expected_input = "input_perf_file",
      .expected_output = "output_profile",
      .expected_overwrite_output = true,
      .allow_unaligned_jit_mappings = false,
      .want_error = false});
  tests.push_back(
      Test{.desc = "With input and output flags",
           .argv = {"<exec>", "-i", "input_perf_file", "-o", "output_profile"},
           .expected_input = "input_perf_file",
           .expected_output = "output_profile",
           .expected_overwrite_output = false,
           .allow_unaligned_jit_mappings = false,
           .want_error = false});
  tests.push_back(Test{
      .desc = "With input and output flags and jit-support",
      .argv = {"<exec>", "-j", "-i", "input_perf_file", "-o", "output_profile"},
      .expected_input = "input_perf_file",
      .expected_output = "output_profile",
      .expected_overwrite_output = false,
      .allow_unaligned_jit_mappings = true,
      .want_error = false});
  tests.push_back(Test{.desc = "With only overwrite flag",
                       .argv = {"<exec>", "-f"},
                       .expected_input = "",
                       .expected_output = "",
                       .expected_overwrite_output = false,
                       .allow_unaligned_jit_mappings = false,
                       .want_error = true});
  tests.push_back(Test{
      .desc = "With input, output, and invalid flags",
      .argv = {"<exec>", "-i", "input_perf_file", "-o", "output_profile", "-F"},
      .expected_input = "",
      .expected_output = "",
      .expected_overwrite_output = false,
      .allow_unaligned_jit_mappings = false,
      .want_error = true});
  tests.push_back(Test{.desc = "With an invalid flag",
                       .argv = {"<exec>", "-F"},
                       .expected_input = "",
                       .expected_output = "",
                       .expected_overwrite_output = false,
                       .allow_unaligned_jit_mappings = false,
                       .want_error = true});
  for (auto test : tests) {
    std::string input;
    std::string output;
    bool overwrite_output;
    bool allow_unaligned_jit_mappings;
    LOG(INFO) << "Testing: " << test.desc;
    EXPECT_THAT(
        ParseArguments(test.argv.size(), test.argv.data(), &input, &output,
                       &overwrite_output, &allow_unaligned_jit_mappings),
        Eq(!test.want_error));
    if (!test.want_error) {
      EXPECT_THAT(input, Eq(test.expected_input));
      EXPECT_THAT(output, Eq(test.expected_output));
      EXPECT_THAT(overwrite_output, Eq(test.expected_overwrite_output));
      EXPECT_THAT(allow_unaligned_jit_mappings,
                  Eq(test.allow_unaligned_jit_mappings));
    }
    optind = 1;
  }
}

std::string GetResource(const std::string& relpath) {
  return "src/testdata/" + relpath;
}

TEST(PerfToProfileTest, RawPerfDataStringToProfiles) {
  std::string raw_perf_data_path(
      GetResource("multi-event-single-process.perf.data"));
  const auto profiles = StringToProfiles(ReadFileToString(raw_perf_data_path));
  EXPECT_EQ(profiles.size(), 1);
}

TEST(PerfToProfileTest, PerfDataProtoStringToProfiles) {
  std::string perf_data_proto_path(
      GetResource("multi-event-single-process.perf_data.pb"));
  const auto profiles =
      StringToProfiles(ReadFileToString(perf_data_proto_path));
  EXPECT_EQ(profiles.size(), 1);
}

}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
