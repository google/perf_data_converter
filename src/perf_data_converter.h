/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PERFTOOLS_PERF_DATA_CONVERTER_H_
#define PERFTOOLS_PERF_DATA_CONVERTER_H_

#include <memory>
#include <vector>

#include "src/profile.pb.h"
#include "src/perf_data_handler.h"

namespace quipper {
class PerfDataProto;
}  // namespace quipper

namespace perftools {

// Sample label options.
enum SampleLabels {
  kNoLabels = 0,
  // Adds label with key PidLabelKey and number value set to the process ID.
  kPidLabel = 1 << 0,
  // Adds label with key TidLabelKey and number value set to the thread ID.
  kTidLabel = 1 << 1,
  // Equivalent to kPidLabel | kTidLabel
  kPidAndTidLabels = 3,
  // Adds label with key TimestampNsLabelKey and number value set to the number
  // of nanoseconds since the system boot that this sample was taken.
  kTimestampNsLabel = 1 << 2,
  // Adds label with key ExecutionModeLabelKey and string value set to one of
  // the ExecutionMode* values.
  kExecutionModeLabel = 1 << 3,
  // Adds a label with key CommLabelKey and string value set to the sample's
  // process's command (that is, /proc/[pid]/comm). If no command is known, no
  // label is added.
  kCommLabel = 1 << 4,
  // Adds a label with key ThreadTypeLabelKey and string value set to the thread
  // type of the sample's thread ID. If the sample doesn't have a thread ID or
  // the sample's thread ID doesn't have a thread type, no label is added.
  kThreadTypeLabel = 1 << 5,
  // Adds a label with key ThreadCommLabelKey and string value set to the
  // sample's thread's command (that is, thread name, or
  // /proc/[pid]/task/[tid]/comm). If no command is known, no label is added.
  kThreadCommLabel = 1 << 6,
  // Adds a label with CgroupLabelKey and number value set to the cgroup id.
  // If the sample doesn't have a cgroup ID, no label is added.
  kCgroupLabel = 1 << 7,
  // Adds a label with CodePageSizeKey and number value set to the
  // code_page_size. If the sample doesn't have a code page size, no label is
  // added.
  kCodePageSizeLabel = 1 << 8,
  // Adds a label with DataPageSizeKey and number value set to the
  // data_page_size. If the sample doesn't have a code page size, no label is
  // added.
  kDataPageSizeLabel = 1 << 9,
  // Adds label with key CpuLabelKey and number value set to the CPU number.
  kCpuLabel = 1 << 10,
  // Adds a label with CacheLatencyLabelKey and number value set to the cache
  // latency.
  kCacheLatencyLabel = 1 << 11,
  // Adds a label with DataSrcLabelKey and string value set to the level of
  // caches.
  kDataSrcLabel = 1 << 12,
};

// Sample label key names.
const char PidLabelKey[] = "pid";
const char TidLabelKey[] = "tid";
const char TimestampNsLabelKey[] = "timestamp_ns";
const char ExecutionModeLabelKey[] = "execution_mode";
const char CommLabelKey[] = "comm";
const char ThreadTypeLabelKey[] = "thread_type";
const char ThreadCommLabelKey[] = "thread_comm";
const char CgroupLabelKey[] = "cgroup";
const char CodePageSizeLabelKey[] = "code_page_size";
const char DataPageSizeLabelKey[] = "data_page_size";
const char CpuLabelKey[] = "cpu";
const char CacheLatencyLabelKey[] = "cache_latency";
const char DataSrcLabelKey[] = "data_src";
const char SnoopStatusLabelKey[] = "snoop_status";

// Execution mode label values.
const char ExecutionModeHostKernel[] = "Host Kernel";
const char ExecutionModeHostUser[] = "Host User";
const char ExecutionModeGuestKernel[] = "Guest Kernel";
const char ExecutionModeGuestUser[] = "Guest User";
const char ExecutionModeHypervisor[] = "Hypervisor";

// Perf data conversion options.
enum ConversionOptions {
  // Default options.
  kNoOptions = 0,
  // Whether to produce multiple, per-process profiles from the single input
  // perf data file. If not set, a single profile will be produced ((but you do
  // still get a list of profiles back; it just has only one entry).
  kGroupByPids = 1,
  // Whether the conversion should fail if there is a detected mismatch between
  // the main mapping in the sample data vs. mapping data.
  kFailOnMainMappingMismatch = 2,
  // Whether to support unaligned MMAP events created by VMs using the JITs
  // which dynamically generate small code objects that are not page aligned.
  kAllowUnalignedJitMappings = 4,
  // Whether to add sampled data addresses as leaf frames for converted
  // profiles.
  kAddDataAddressFrames = 8,
};

struct ProcessProfile {
  // Process PID or 0 if no process grouping was requested.
  // PIDs can duplicate if there was a PID reuse during the profiling session.
  uint32_t pid = 0;
  // Profile proto data.
  perftools::profiles::Profile data;
  // Min timestamp of a sample, in nanoseconds since boot, or 0 if unknown.
  int64_t min_sample_time_ns = 0;
  // Max timestamp of a sample, in nanoseconds since boot, or 0 if unknown.
  int64_t max_sample_time_ns = 0;
  // Number of frames + IPs belonging to the given source of the build ID,
  // see go/gwp-buildid-mmap. The sum in the map is always exactly
  // equal to the total number of frames + IP in the profile, weighted by
  // sample count.
  BuildIdStats build_id_stats;
};

// Type alias for a random access sequence of owned ProcessProfile objects.
using ProcessProfiles = std::vector<std::unique_ptr<ProcessProfile>>;

// Converts raw Linux perf data to a vector of process profiles.
//
// sample_labels is the OR-product of all SampleLabels desired in the output
// profiles. options governs other conversion options such as whether per-PID
// profiles should be returned or all processes should be merged into the same
// profile.
//
// thread_types is map of TIDs to opaque strings of the caller's choosing.
// When sample_labels & kThreadTypeLabel != 0, any sample generated by a TID
// in |thread_types| will be labelled with
// {key=ThreadTypeLabelKey, value=thread_types[$tid]}.
// If sample_labels doesn't include ThreadTypeLabelKey *or* the TID is not in
// |thread_types|, no ThreadTypeLabelKey will be applied to the sample.
//
// Returns a vector of process profiles, empty if any error occurs.
extern ProcessProfiles RawPerfDataToProfiles(
    const void* raw, uint64_t raw_size,
    const std::map<std::string, std::string>& build_ids,
    uint32_t sample_labels = kNoLabels, uint32_t options = kGroupByPids,
    const std::map<uint32_t, std::string>& thread_types = {});

// Converts a PerfDataProto to a vector of process profiles.
extern ProcessProfiles PerfDataProtoToProfiles(
    const quipper::PerfDataProto* perf_data, uint32_t sample_labels = kNoLabels,
    uint32_t options = kGroupByPids,
    const std::map<uint32_t, std::string>& thread_types = {});

}  // namespace perftools

#endif  // PERFTOOLS_PERF_DATA_CONVERTER_H_
