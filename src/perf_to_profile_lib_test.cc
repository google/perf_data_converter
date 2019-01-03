/*
 * Copyright (c) 2018, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/perf_to_profile_lib.h"
#include "src/quipper/base/logging.h"
#include "src/compat/test_compat.h"

namespace {

using ::testing::Eq;

TEST(PerfToProfileTest, ParseArguments) {
  struct Test {
    string desc;
    std::vector<const char*> argv;
    string expected_input;
    string expected_output;
    bool expected_overwrite_output;
    bool want_error;
  };

  std::vector<Test> tests;
  tests.push_back(Test{
      .desc = "With input, output and overwrite flags",
      .argv = {"<exec>", "-i", "input_perf_file", "-o", "output_profile", "-f"},
      .expected_input = "input_perf_file",
      .expected_output = "output_profile",
      .expected_overwrite_output = true,
      .want_error = false,
  });
  tests.push_back(Test{
      .desc = "With input and output flags",
      .argv = {"<exec>", "-i", "input_perf_file", "-o", "output_profile"},
      .expected_input = "input_perf_file",
      .expected_output = "output_profile",
      .expected_overwrite_output = false,
      .want_error = false,
  });
  tests.push_back(Test{
      .desc = "With only overwrite flag",
      .argv = {"<exec>", "-f"},
      .expected_input = "",
      .expected_output = "",
      .expected_overwrite_output = false,
      .want_error = true,
  });
  tests.push_back(Test{
      .desc = "With input, output, and invalid flags",
      .argv = {"<exec>", "-i", "input_perf_file", "-o", "output_profile", "-F"},
      .expected_input = "",
      .expected_output = "",
      .expected_overwrite_output = false,
      .want_error = true,
  });
  tests.push_back(Test{
      .desc = "With an invalid flag",
      .argv = {"<exec>", "-F"},
      .expected_input = "",
      .expected_output = "",
      .expected_overwrite_output = false,
      .want_error = true,
  });
  for (auto test : tests) {
    string input;
    string output;
    bool overwrite_output;
    LOG(INFO) << "Testing: " << test.desc;
    EXPECT_THAT(ParseArguments(test.argv.size(), test.argv.data(), &input,
                               &output, &overwrite_output),
                Eq(!test.want_error));
    if (!test.want_error) {
      EXPECT_THAT(input, Eq(test.expected_input));
      EXPECT_THAT(output, Eq(test.expected_output));
      EXPECT_THAT(overwrite_output, Eq(test.expected_overwrite_output));
    }
    optind = 1;
  }
}

// Assumes relpath does not begin with a '/'
string GetResource(const string& relpath) {
  return "src/" + relpath;
}

TEST(PerfToProfileTest, RawPerfDataStringToProfiles) {
  string raw_perf_data_path(
      GetResource("testdata"
                  "/multi-event-single-process.perf.data"));
  const auto profiles = StringToProfiles(ReadFileToString(raw_perf_data_path));
  EXPECT_EQ(profiles.size(), 1);
}

TEST(PerfToProfileTest, PerfDataProtoStringToProfiles) {
  string perf_data_proto_path(
      GetResource("testdata"
                  "/multi-event-single-process.perf_data.pb.data"));
  const auto profiles =
      StringToProfiles(ReadFileToString(perf_data_proto_path));
  EXPECT_EQ(profiles.size(), 1);
}

}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
