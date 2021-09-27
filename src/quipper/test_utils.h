// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_TEST_UTILS_H_
#define CHROMIUMOS_WIDE_PROFILING_TEST_UTILS_H_

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "compat/string.h"
#include "compat/test.h"
#include "file_utils.h"
#include "perf_parser.h"

namespace quipper {

extern const char* kSupportedMetadata[];

// Container for all the metadata from one perf report.  The key is the metadata
// type, as shown in |kSupportedMetadata|.  The value is a vector of all the
// occurrences of that type.  For some types, there is only one occurrence.
typedef std::map<std::string, std::vector<std::string> > MetadataSet;

// Path to the perf executable.
std::string GetPerfPath();

// Converts a perf data filename to the full path.
std::string GetTestInputFilePath(const std::string& filename);

// Returns the size of a file in bytes.
int64_t GetFileSize(const std::string& filename);

// Returns true if the contents of the two files are the same, false otherwise.
bool CompareFileContents(const std::string& filename1,
                         const std::string& filename2);

// MaybeWriteGolden writes the given golden file, which can be provided either
// as a string protobuf representation or as a proto.
bool MaybeWriteGolden(const std::string& protobuf_representation,
                      const std::string& golden_filename);
bool MaybeWriteGolden(const Message& proto, const std::string& golden_filename);

template <typename T>
void CompareProtoFiles(const std::string& format, const std::string& actual,
                       const std::string& expected,
                       const std::string& golden_filename = "") {
  std::vector<char> actual_contents;
  std::vector<char> expected_contents;
  ASSERT_TRUE(FileToBuffer(actual, &actual_contents)) << actual;
  ASSERT_TRUE(FileToBuffer(expected, &expected_contents)) << expected;

  ArrayInputStream actual_arr(actual_contents.data(), actual_contents.size());
  ArrayInputStream expected_arr(expected_contents.data(),
                                expected_contents.size());

  T actual_proto, expected_proto;
  if (format == "text") {
    ASSERT_TRUE(TextFormat::Parse(&actual_arr, &actual_proto));
    ASSERT_TRUE(TextFormat::Parse(&expected_arr, &expected_proto));
  } else if (format == "proto") {
    ASSERT_TRUE(actual_proto.ParseFromZeroCopyStream(&actual_arr));
    ASSERT_TRUE(expected_proto.ParseFromZeroCopyStream(&expected_arr));
  } else {
    FAIL() << format;
  }

  std::string difference;
  bool matches_baseline =
      EqualsProto(actual_proto, expected_proto, &difference);
  if (!matches_baseline) {
    if (format == "text") {
      MaybeWriteGolden(actual_proto, golden_filename);
    } else if (format == "proto") {
      MaybeWriteGolden(actual_proto.SerializeAsString(), golden_filename);
    } else {
      FAIL() << format;
    }
  }
  EXPECT_TRUE(matches_baseline) << difference;
}

// Given a perf data file, get the list of build ids and create a map from
// filenames to build ids.
bool GetPerfBuildIDMap(const std::string& filename,
                       std::map<std::string, std::string>* output);

// Checks the given perf.data against the golden file. Provide baseline filename
// only for the perf.data files that have different golden filenames.
bool CheckPerfDataAgainstBaseline(const std::string& perfdata_filepath,
                                  const std::string& baseline_filename = "",
                                  std::string* difference = nullptr);

// Returns true if the perf buildid-lists are the same.
bool ComparePerfBuildIDLists(const std::string& file1,
                             const std::string& file2);

// Returns options suitable for correctness tests.
PerfParserOptions GetTestOptions();

// Returns options suitable for validatig the parser output. Avoid any optional
// transformations on the parsed perf data file.
PerfParserOptions GetMinimalProcessingOptions();

template <typename T>
bool EqualsProto(T actual, T expected, std::string* difference = nullptr) {
  std::unique_ptr<MessageDifferencer> differencer(new MessageDifferencer);
  return CompareProto(std::move(differencer), actual, expected, difference);
}

template <typename T>
bool PartiallyEqualsProto(T actual, T expected,
                          std::string* difference = nullptr) {
  std::unique_ptr<MessageDifferencer> differencer(new MessageDifferencer);
  differencer->set_scope(MessageDifferencer::PARTIAL);
  return CompareProto(std::move(differencer), actual, expected, difference);
}

template <typename T>
bool CompareProto(std::unique_ptr<MessageDifferencer> differencer, T actual,
                  T expected, std::string* difference) {
  differencer->set_message_field_comparison(MessageDifferencer::EQUAL);
  if (difference != nullptr) {
    difference->clear();
  }
  return differencer->Compare(expected, actual);
}

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_TEST_UTILS_H_
