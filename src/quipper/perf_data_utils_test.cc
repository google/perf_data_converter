// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_data_utils.h"

#include "compat/proto.h"
#include "compat/test.h"
#include "kernel/perf_event.h"

namespace quipper {

TEST(PerfDataUtilsTest, GetUint64AlignedStringLength) {
  EXPECT_EQ(8, GetUint64AlignedStringLength(6));
  EXPECT_EQ(8, GetUint64AlignedStringLength(7));
  EXPECT_EQ(16, GetUint64AlignedStringLength(8));  // Room for '\0'
  EXPECT_EQ(16, GetUint64AlignedStringLength(9));
  EXPECT_EQ(16, GetUint64AlignedStringLength(15));
  EXPECT_EQ(24, GetUint64AlignedStringLength(16));
}

TEST(PerfDataUtilsTest, GetStringLengthForValidString) {
  char str[] = "abcd";
  size_t len = 0;
  EXPECT_TRUE(GetStringLength(str, 10, &len));
  EXPECT_EQ(4, len);
}

TEST(PerfDataUtilsTest, GetStringLengthForInvalidString) {
  char str[] = "abcdefgh12345";
  size_t len = 0;
  EXPECT_FALSE(GetStringLength(str, 10, &len));
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

TEST(PerfDataUtilsTest, GetEventDataSize) {
  std::string filename = "/usr/lib/foo.so";
  size_t event_size = offsetof(struct mmap_event, filename) +
                      GetUint64AlignedStringLength(filename.size());
  malloced_unique_ptr<event_t> event_ptr(CallocMemoryForEvent(event_size));
  event_t* event = event_ptr.get();
  event->header.type = PERF_RECORD_MMAP;
  event->header.misc = 0;
  event->header.size = event_size;
  memcpy(&event->mmap.filename, filename.data(), filename.size());

  EXPECT_EQ(event_size, GetEventDataSize(*event));
}

TEST(PerfDataUtilsTest, GetEventDataSizeWithSmallerHeaderSize) {
  size_t event_size = sizeof(struct perf_event_header);
  malloced_unique_ptr<event_t> event_ptr(CallocMemoryForEvent(event_size));
  event_t* event = event_ptr.get();
  event->header.type = PERF_RECORD_MMAP;
  event->header.misc = 0;
  event->header.size = event_size;

  EXPECT_EQ(0, GetEventDataSize(*event));
}

TEST(PerfDataUtilsTest, GetEventDataSizeForUnsupportedEvent) {
  size_t event_size = sizeof(struct perf_event_header);
  malloced_unique_ptr<event_t> event_ptr(CallocMemoryForEvent(event_size));
  event_t* event = event_ptr.get();
  event->header.type = PERF_RECORD_MAX;
  event->header.misc = 0;
  event->header.size = event_size;

  EXPECT_EQ(0, GetEventDataSize(*event));
}

TEST(PerfDataUtilsTest, GetEventDataSizeWithProto) {
  std::string filename = "/usr/lib/foo.so";
  size_t event_size = offsetof(struct mmap_event, filename) +
                      GetUint64AlignedStringLength(filename.size());
  PerfDataProto_PerfEvent event;
  event.mutable_header()->set_type(PERF_RECORD_MMAP);
  event.mutable_header()->set_size(event_size);
  event.mutable_mmap_event()->set_filename(filename);

  EXPECT_EQ(event_size, GetEventDataSize(event));
}

TEST(PerfDataUtilsTest, GetEventDataSizeWithProtoForUnsupportedEvent) {
  PerfDataProto_PerfEvent event;
  event.mutable_header()->set_type(PERF_RECORD_MAX);
  event.mutable_header()->set_size(sizeof(struct perf_event_header));

  EXPECT_EQ(0, GetEventDataSize(event));
}

}  // namespace quipper
