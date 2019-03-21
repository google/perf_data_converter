// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>  // for exp
#include <string>

#include "compat/test.h"

#include "binary_data_utils.h"
#include "kernel/perf_event.h"
#include "process_heap_profile.h"
#include "test_utils.h"

namespace quipper {

namespace {

TEST(ProcessHeapProfileTest, HeaderEntryCorrupted) {
  std::string heap_profile(
      "CORRRUPTED GARBAGE INSERTED HERE profile: 1: 2048 [ 1: 2048 ] @ "
      "heap_v2/111222\n"
      "  1:     2048 [     1:     2048] @ 0x617aae951c31 0x617aae95062e\n"
      "\n"
      "MAPPED_LIBRARIES:\n"
      "617aa770f000-617ab0689000 r-xp 00000010 00:00 98734 "
      "/opt/google/chrome/chrome\n");
  PerfDataProto proto;
  ASSERT_FALSE(ProcessHeapProfile(heap_profile, 0, &proto));
}

TEST(ProcessHeapProfileTest, SampleEntryCorrupted) {
  std::string heap_profile(
      "heap profile: 1: 1024 [ 1: 1024 ] @ heap_v2/111222\n"
      "CORRRUPTED:    1024 [    1:    1024] @ 0x617aae951c31 "
      "0x617aae95062e\n"
      "\n"
      "MAPPED_LIBRARIES:\n"
      "617aa770f000-617ab0689000 r-xp 00000010 00:00 98734 "
      "/opt/google/chrome/chrome\n");
  PerfDataProto proto;
  ASSERT_FALSE(ProcessHeapProfile(heap_profile, 0, &proto));
}

TEST(ProcessHeapProfileTest, MMapEntryCorrupted) {
  std::string heap_profile(
      "heap profile: 1: 1024 [ 1: 1024 ] @ heap_v2/111222\n"
      "  1:     1024 [     1:     1024] @ 0x617aae951c31 0x617aae95062e\n"
      "\n"
      "MAPPED_LIBRARIES:\n"
      "CORRRUPTED-617ab0689000 r-xp 00000010 00:00 98734 "
      "/opt/google/chrome/chrome\n");
  PerfDataProto proto;
  ASSERT_FALSE(ProcessHeapProfile(heap_profile, 0, &proto));
}

TEST(ProcessHeapProfileTest, HasMetadataAndPerfFileAttr) {
  std::string heap_profile(
      "heap profile: 2: 1024 [ 2: 1024 ] @ heap_v2/111222\n"
      "  2:     1024 [     2:     1024] @ 0x617aae951c31 0x617aae95062e\n"
      "\n"
      "#Comment \n"
      "MAPPED_LIBRARIES:\n"
      "a71d727f000-a71d7280000 ---p 00000000 00:00 0\n"
      "617aa770f000-617ab0689000 r-xp 00000010 00:00 98734"
      "      /opt/google/chrome/chrome\n");

  PerfDataProto actual, expected;

  expected.add_metadata_mask(1 << HEADER_EVENT_DESC);

  PerfDataProto_PerfFileAttr* file_attr = expected.add_file_attrs();
  PerfDataProto_PerfEventAttr* event_attr = file_attr->mutable_attr();
  event_attr->set_type(PERF_TYPE_SOFTWARE);
  event_attr->set_size(PERF_ATTR_SIZE_VER3);
  event_attr->set_config(PERF_COUNT_SW_DUMMY);
  event_attr->set_sample_id_all(true);
  event_attr->set_sample_period(111222);
  event_attr->set_sample_type(PERF_SAMPLE_IP | PERF_SAMPLE_TID |
                              PERF_SAMPLE_ID | PERF_SAMPLE_PERIOD |
                              PERF_SAMPLE_CALLCHAIN);
  event_attr->set_mmap(true);
  file_attr->add_ids(0);

  PerfDataProto_PerfEventType* event_type = expected.add_event_types();
  event_type->set_id(PERF_COUNT_SW_DUMMY);
  event_type->set_name("heap_cpp_inuse_objects");
  event_type->set_name_md5_prefix(Md5Prefix("heap_cpp_inuse_objects"));

  file_attr = expected.add_file_attrs();
  event_attr = file_attr->mutable_attr();
  event_attr->set_type(PERF_TYPE_SOFTWARE);
  event_attr->set_size(PERF_ATTR_SIZE_VER3);
  event_attr->set_config(PERF_COUNT_SW_DUMMY);
  event_attr->set_sample_id_all(true);
  event_attr->set_sample_period(111222);
  event_attr->set_sample_type(PERF_SAMPLE_IP | PERF_SAMPLE_TID |
                              PERF_SAMPLE_ID | PERF_SAMPLE_PERIOD |
                              PERF_SAMPLE_CALLCHAIN);
  file_attr->add_ids(1);

  event_type = expected.add_event_types();
  event_type->set_id(PERF_COUNT_SW_DUMMY);
  event_type->set_name("heap_cpp_inuse_space");
  event_type->set_name_md5_prefix(Md5Prefix("heap_cpp_inuse_space"));

  ASSERT_TRUE(ProcessHeapProfile(heap_profile, 3456, &actual));

  if (actual.build_ids_size() > 0) {
    // After processing the heap profile through quipper, the actual proto may
    // contain build ids and the metadata mask bit HEADER_BUILD_ID maybe set.
    expected.set_metadata_mask(0,
                               1 << HEADER_EVENT_DESC | 1 << HEADER_BUILD_ID);
  }

  string difference;
  bool matches = PartiallyEqualsProto(actual, expected, &difference);
  EXPECT_TRUE(matches) << difference;
}

TEST(ProcessHeapProfileTest, HasSampleEvent) {
  std::string heap_profile(
      "heap profile: 2: 1024 [ 2: 1024 ] @ heap_v2/111222\n"
      "  2:     1024 [     2:     1024] @ 0x617aae951c31 0x617aae95062e\n"
      "\n"
      "#Comment \n"
      "MAPPED_LIBRARIES:\n"
      "a71d727f000-a71d7280000 ---p 00000000 00:00 0\n"
      "617aa770f000-617ab0689000 r-xp 00000010 00:00 98734"
      "      /opt/google/chrome/chrome\n");

  PerfDataProto actual, expected;

  PerfDataProto_PerfEvent* event = expected.add_events();
  PerfDataProto_EventHeader* header = event->mutable_header();
  header->set_type(PERF_RECORD_SAMPLE);
  header->set_misc(PERF_RECORD_MISC_USER);
  header->set_size(72);

  double scale = 1 / (1 - exp(-(1024.00 / 2.00) / 111222.00));

  PerfDataProto_SampleEvent* sample = event->mutable_sample_event();
  sample->set_ip(119815217);
  sample->set_pid(3456);
  sample->set_tid(3456);
  sample->set_id(0);
  sample->set_period(2 * scale);
  sample->add_callchain(PERF_CONTEXT_USER);
  sample->add_callchain(119815217);
  sample->add_callchain(119809582);

  event = expected.add_events();
  header = event->mutable_header();
  header->set_type(PERF_RECORD_SAMPLE);
  header->set_misc(PERF_RECORD_MISC_USER);
  header->set_size(72);

  sample = event->mutable_sample_event();
  sample->set_ip(119815217);
  sample->set_pid(3456);
  sample->set_tid(3456);
  sample->set_id(1);
  sample->set_period(1024 * scale);
  sample->add_callchain(PERF_CONTEXT_USER);
  sample->add_callchain(119815217);
  sample->add_callchain(119809582);

  ASSERT_TRUE(ProcessHeapProfile(heap_profile, 3456, &actual));
  ASSERT_EQ(actual.events_size(), 3);

  string difference;
  bool matches =
      PartiallyEqualsProto(actual.events(1), expected.events(0), &difference);
  EXPECT_TRUE(matches) << difference;

  matches =
      PartiallyEqualsProto(actual.events(2), expected.events(1), &difference);
  EXPECT_TRUE(matches) << difference;
}

TEST(ProcessHeapProfileTest, HasMMapEvent) {
  std::string heap_profile(
      "heap profile: 2: 1024 [ 2: 1024 ] @ heap_v2/111222\n"
      "  2:     1024 [     2:     1024] @ 0x617aae951c31 0x617aae95062e\n"
      "\n"
      "#Comment \n"
      "MAPPED_LIBRARIES:\n"
      "a71d727f000-a71d7280000 ---p 00000000 00:00 0\n"
      "617aa770f000-617ab0689000 r-xp 00000010 00:00 98734"
      "      /opt/google/chrome/chrome\n");

  PerfDataProto actual, expected;

  PerfDataProto_PerfEvent* event = expected.add_events();
  PerfDataProto_EventHeader* header = event->mutable_header();
  std::string filename = "/opt/google/chrome/chrome";
  header->set_type(PERF_RECORD_MMAP);
  header->set_misc(0);
  header->set_size(88);

  PerfDataProto_MMapEvent* mmap = event->mutable_mmap_event();
  mmap->set_pid(3456);
  mmap->set_tid(3456);
  mmap->set_start(0x1000);
  mmap->set_len(0x617ab0689000 - 0x617aa770f000);
  mmap->set_pgoff(0x10);
  mmap->set_filename(filename);
  mmap->set_filename_md5_prefix(Md5Prefix(filename));

  PerfDataProto_SampleInfo* sample_info = mmap->mutable_sample_info();
  sample_info->set_pid(3456);
  sample_info->set_tid(3456);
  sample_info->set_id(0);

  ASSERT_TRUE(ProcessHeapProfile(heap_profile, 3456, &actual));
  ASSERT_GE(actual.events_size(), 1);

  string difference;
  bool matches =
      PartiallyEqualsProto(actual.events(0), expected.events(0), &difference);
  EXPECT_TRUE(matches) << difference;
}

}  // namespace

}  // namespace quipper
