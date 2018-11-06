// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_data_utils.h"

#include "compat/proto.h"
#include "compat/string.h"
#include "compat/test.h"
#include "kernel/perf_event.h"

namespace quipper {

TEST(PerfDataUtilsTest, GetUint64AlignedStringLength) {
  EXPECT_EQ(8, GetUint64AlignedStringLength("012345"));
  EXPECT_EQ(8, GetUint64AlignedStringLength("0123456"));
  EXPECT_EQ(16, GetUint64AlignedStringLength("01234567"));  // Room for '\0'
  EXPECT_EQ(16, GetUint64AlignedStringLength("012345678"));
  EXPECT_EQ(16, GetUint64AlignedStringLength("0123456789abcde"));
  EXPECT_EQ(24, GetUint64AlignedStringLength("0123456789abcdef"));
}

TEST(PerfDataUtilsTest, PerfizeBuildID) {
  string build_id_string = "f";
  PerfizeBuildIDString(&build_id_string);
  EXPECT_EQ("f000000000000000000000000000000000000000", build_id_string);
  PerfizeBuildIDString(&build_id_string);
  EXPECT_EQ("f000000000000000000000000000000000000000", build_id_string);

  build_id_string = "01234567890123456789012345678901234567890";
  PerfizeBuildIDString(&build_id_string);
  EXPECT_EQ("0123456789012345678901234567890123456789", build_id_string);
  PerfizeBuildIDString(&build_id_string);
  EXPECT_EQ("0123456789012345678901234567890123456789", build_id_string);
}

TEST(PerfDataUtilsTest, UnperfizeBuildID) {
  string build_id_string = "f000000000000000000000000000000000000000";
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("f0000000", build_id_string);
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("f0000000", build_id_string);

  build_id_string = "0123456789012345678901234567890123456789";
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("0123456789012345678901234567890123456789", build_id_string);

  build_id_string = "0000000000000000000000000000001000000000";
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("00000000000000000000000000000010", build_id_string);
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("00000000000000000000000000000010", build_id_string);

  build_id_string = "0000000000000000000000000000000000000000";  // 40 zeroes
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("", build_id_string);

  build_id_string = "00000000000000000000000000000000";  // 32 zeroes
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("", build_id_string);

  build_id_string = "00000000";  // 8 zeroes
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("", build_id_string);

  build_id_string = "0000000";  // 7 zeroes
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("0000000", build_id_string);

  build_id_string = "";
  TrimZeroesFromBuildIDString(&build_id_string);
  EXPECT_EQ("", build_id_string);
}

TEST(PerfDataUtilsTest, GetSampleIdFromSampleEventWithoutId) {
  PerfDataProto_PerfEvent event;
  event.mutable_header()->set_type(PERF_RECORD_SAMPLE);
  event.mutable_sample_event();

  uint64_t actual_id = GetSampleIdFromPerfEvent(event);

  EXPECT_EQ(0, actual_id);
}

TEST(PerfDataUtilsTest, GetSampleIdFromSampleEvent) {
  PerfDataProto_PerfEvent event;
  event.mutable_header()->set_type(PERF_RECORD_SAMPLE);

  PerfDataProto_SampleEvent* sample = event.mutable_sample_event();
  sample->set_id(123);

  uint64_t actual_id = GetSampleIdFromPerfEvent(event);

  EXPECT_EQ(123, actual_id);
}

TEST(PerfDataUtilsTest, GetSampleIdFromNonSampleEventWithoutSampleInfo) {
  PerfDataProto_PerfEvent event;
  event.mutable_header()->set_type(PERF_RECORD_MMAP);
  event.mutable_mmap_event();

  uint64_t actual_id = GetSampleIdFromPerfEvent(event);

  EXPECT_EQ(0, actual_id);
}

TEST(PerfDataUtilsTest, GetSampleIdFromNonSampleEventWithoutId) {
  PerfDataProto_PerfEvent event;
  event.mutable_header()->set_type(PERF_RECORD_MMAP);

  PerfDataProto_MMapEvent* mmap = event.mutable_mmap_event();
  mmap->mutable_sample_info();

  uint64_t actual_id = GetSampleIdFromPerfEvent(event);

  EXPECT_EQ(0, actual_id);
}

TEST(PerfDataUtilsTest, GetSampleIdFromNonSampleEvent) {
  PerfDataProto_PerfEvent event;
  event.mutable_header()->set_type(PERF_RECORD_MMAP);

  PerfDataProto_MMapEvent* mmap = event.mutable_mmap_event();
  PerfDataProto_SampleInfo* sample_info = mmap->mutable_sample_info();
  sample_info->set_id(4);

  uint64_t actual_id = GetSampleIdFromPerfEvent(event);

  EXPECT_EQ(4, actual_id);
}

}  // namespace quipper
