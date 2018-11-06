// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "process_heap_profile.h"

#include "base/logging.h"
#include "heap_profile_parser.h"
#include "perf_parser.h"
#include "perf_reader.h"

namespace quipper {

bool ProcessHeapProfile(const std::string& heap_profile, u32 pid,
                        PerfDataProto* out) {
  HeapProfileParser heap_parser(heap_profile, pid);

  if (!heap_parser.Parse()) {
    LOG(ERROR) << "Couldn't parse the heap profile";
    return false;
  }

  PerfDataProto raw;
  heap_parser.GetProto(&raw);

  // Process the heap profile with the perf_reader and perf_parser to set perf
  // header sizes, inject missing build IDs, remap addresses for security
  // reasons, deduce huge page mappings and discard unused events.
  PerfReader reader;
  if (!reader.Deserialize(raw)) {
    LOG(ERROR) << "Failed to deserialize input proto";
    return false;
  }

  PerfParserOptions options;
  // Make sure to remap address for security reasons.
  options.do_remap = true;
  // Discard unused perf events to reduce the protobuf size.
  options.discard_unused_events = true;
  // Read buildids from the filesystem ourself.
  options.read_missing_buildids = true;
  // Resolve split huge pages mappings.
  options.deduce_huge_page_mappings = true;

  PerfParser perf_parser(&reader, options);
  if (!perf_parser.ParseRawEvents()) {
    LOG(ERROR) << "Failed to parse perf data proto with options";
    return false;
  }

  if (!reader.Serialize(out)) {
    LOG(ERROR) << "Failed to serialize to perf data proto";
    return false;
  }

  // Append parser stats to protobuf.
  PerfSerializer::SerializeParserStats(perf_parser.stats(), out);
  return true;
}

}  // namespace quipper
