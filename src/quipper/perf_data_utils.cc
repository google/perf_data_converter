// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_data_utils.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include "base/logging.h"
#include "compat/proto.h"
#include "kernel/perf_event.h"
#include "kernel/perf_internals.h"
namespace quipper {

namespace {

// Gets the string length from the given |str| buffer using the |max_size| as
// the maximum size of the buffer. Returns true and updates the |size| with
// the uint64_t aligned string length on success. Returns false when the |str|
// is not null terimated or the uint64_t aligned string length exceeds the
// |max_size|.
bool GetUint64AlignedStringLength(const char* str, size_t max_size,
                                  const std::string& string_name,
                                  size_t* size) {
  size_t str_size;
  if (!GetStringLength(str, max_size, &str_size)) {
    LOG(ERROR) << "Malformed " << string_name << " of max length " << max_size
               << " is not null-terminated";
    return false;
  }
  // When perf calculates an event size, it uses uint64_t aligned string length.
  // Because of this, the string length obtained using `strnlen` should be
  // uint64_t aligned before validating it against the string length reported in
  // a perf event.
  str_size = quipper::GetUint64AlignedStringLength(str_size);
  if (str_size > max_size) {
    LOG(ERROR) << "uint64_t aligned string length " << str_size << " of "
               << string_name << " exceeds the max string length " << max_size;
    return false;
  }
  *size = str_size;
  return true;
}

}  // namespace

event_t* CallocMemoryForEvent(size_t size) {
  event_t* event = reinterpret_cast<event_t*>(calloc(1, size));
  CHECK(event);
  return event;
}

event_t* ReallocMemoryForEvent(event_t* event, size_t new_size) {
  event_t* new_event = reinterpret_cast<event_t*>(realloc(event, new_size));
  CHECK(new_event);  // NB: event is "leaked" if this CHECK fails.
  return new_event;
}

build_id_event* CallocMemoryForBuildID(size_t size) {
  build_id_event* event = reinterpret_cast<build_id_event*>(calloc(1, size));
  CHECK(event);
  return event;
}

feature_event* CallocMemoryForFeature(size_t size) {
  feature_event* event = reinterpret_cast<feature_event*>(calloc(1, size));
  CHECK(event);
  return event;
}

bool GetStringLength(const char* str, size_t max_size, size_t* size) {
  CHECK(size);
  size_t read_size = strnlen(str, max_size);
  if (read_size == max_size) {
    return false;
  }
  *size = read_size;
  return true;
}

const PerfDataProto_SampleInfo* GetSampleInfoForEvent(
    const PerfDataProto_PerfEvent& event) {
  switch (event.header().type()) {
    case PERF_RECORD_MMAP:
    case PERF_RECORD_MMAP2:
      return &event.mmap_event().sample_info();
    case PERF_RECORD_COMM:
      return &event.comm_event().sample_info();
    case PERF_RECORD_FORK:
      return &event.fork_event().sample_info();
    case PERF_RECORD_EXIT:
      return &event.exit_event().sample_info();
    case PERF_RECORD_LOST:
      return &event.lost_event().sample_info();
    case PERF_RECORD_THROTTLE:
    case PERF_RECORD_UNTHROTTLE:
      return &event.throttle_event().sample_info();
    case PERF_RECORD_AUX:
      return &event.aux_event().sample_info();
    case PERF_RECORD_ITRACE_START:
      return &event.itrace_start_event().sample_info();
    case PERF_RECORD_LOST_SAMPLES:
      return &event.lost_samples_event().sample_info();
    case PERF_RECORD_SWITCH:
    case PERF_RECORD_SWITCH_CPU_WIDE:
      return &event.context_switch_event().sample_info();
    case PERF_RECORD_NAMESPACES:
      return &event.namespaces_event().sample_info();
    case PERF_RECORD_CGROUP:
      return &event.cgroup_event().sample_info();
    case PERF_RECORD_KSYMBOL:
      return &event.ksymbol_event().sample_info();
  }
  return nullptr;
}

// Returns the correct |sample_time_ns| field of a PerfEvent.
uint64_t GetTimeFromPerfEvent(const PerfDataProto_PerfEvent& event) {
  if (event.header().type() == PERF_RECORD_SAMPLE)
    return event.sample_event().sample_time_ns();

  const auto* sample_info = GetSampleInfoForEvent(event);
  if (sample_info) return sample_info->sample_time_ns();

  return 0;
}

uint64_t GetSampleIdFromPerfEvent(const PerfDataProto_PerfEvent& event) {
  if (event.header().type() == PERF_RECORD_SAMPLE &&
      event.sample_event().has_id()) {
    return event.sample_event().id();
  }

  const auto* sample_info = GetSampleInfoForEvent(event);
  if (sample_info && sample_info->has_id()) {
    return sample_info->id();
  }

  return 0;
}

std::string GetMetadataName(uint32_t type) {
  switch (type) {
    case HEADER_TRACING_DATA:
      return "HEADER_TRACING_DATA";
    case HEADER_BUILD_ID:
      return "HEADER_BUILD_ID";
    case HEADER_HOSTNAME:
      return "HEADER_HOSTNAME";
    case HEADER_OSRELEASE:
      return "HEADER_OSRELEASE";
    case HEADER_VERSION:
      return "HEADER_VERSION";
    case HEADER_ARCH:
      return "HEADER_ARCH";
    case HEADER_NRCPUS:
      return "HEADER_NRCPUS";
    case HEADER_CPUDESC:
      return "HEADER_CPUDESC";
    case HEADER_CPUID:
      return "HEADER_CPUID";
    case HEADER_TOTAL_MEM:
      return "HEADER_TOTAL_MEM";
    case HEADER_CMDLINE:
      return "HEADER_CMDLINE";
    case HEADER_EVENT_DESC:
      return "HEADER_EVENT_DESC";
    case HEADER_CPU_TOPOLOGY:
      return "HEADER_CPU_TOPOLOGY";
    case HEADER_NUMA_TOPOLOGY:
      return "HEADER_NUMA_TOPOLOGY";
    case HEADER_BRANCH_STACK:
      return "HEADER_BRANCH_STACK";
    case HEADER_PMU_MAPPINGS:
      return "HEADER_PMU_MAPPINGS";
    case HEADER_GROUP_DESC:
      return "HEADER_GROUP_DESC";
    case HEADER_AUXTRACE:
      return "HEADER_AUXTRACE";
    case HEADER_STAT:
      return "HEADER_STAT";
    case HEADER_CACHE:
      return "HEADER_CACHE";
    case HEADER_SAMPLE_TIME:
      return "HEADER_SAMPLE_TIME";
    case HEADER_MEM_TOPOLOGY:
      return "HEADER_MEM_TOPOLOGY";
    case HEADER_CLOCKID:
      return "HEADER_CLOCKID";
    case HEADER_DIR_FORMAT:
      return "HEADER_DIR_FORMAT";
    case HEADER_BPF_PROG_INFO:
      return "HEADER_BPF_PROG_INFO";
    case HEADER_BPF_BTF:
      return "HEADER_BPF_BTF";
    case HEADER_COMPRESSED:
      return "HEADER_COMPRESSED";
    case HEADER_CPU_PMU_CAPS:
      return "HEADER_PMU_CAPS";
    case HEADER_CLOCK_DATA:
      return "HEADER_CLOCK_DATA";
    case HEADER_HYBRID_TOPOLOGY:
      return "HEADER_HYBRID_TOPOLOGY";
    case HEADER_LAST_FEATURE:
      return "HEADER_LAST_FEATURE";
  }
  return "UNKNOWN_METADATA_" + std::to_string(type);
}

std::string GetEventName(uint32_t type) {
  switch (type) {
    case PERF_RECORD_MMAP:
      return "PERF_RECORD_MMAP";
    case PERF_RECORD_LOST:
      return "PERF_RECORD_LOST";
    case PERF_RECORD_COMM:
      return "PERF_RECORD_COMM";
    case PERF_RECORD_EXIT:
      return "PERF_RECORD_EXIT";
    case PERF_RECORD_THROTTLE:
      return "PERF_RECORD_THROTTLE";
    case PERF_RECORD_UNTHROTTLE:
      return "PERF_RECORD_UNTHROTTLE";
    case PERF_RECORD_FORK:
      return "PERF_RECORD_FORK";
    case PERF_RECORD_READ:
      return "PERF_RECORD_READ";
    case PERF_RECORD_SAMPLE:
      return "PERF_RECORD_SAMPLE";
    case PERF_RECORD_MMAP2:
      return "PERF_RECORD_MMAP2";
    case PERF_RECORD_AUX:
      return "PERF_RECORD_AUX";
    case PERF_RECORD_ITRACE_START:
      return "PERF_RECORD_ITRACE_START";
    case PERF_RECORD_LOST_SAMPLES:
      return "PERF_RECORD_LOST_SAMPLES";
    case PERF_RECORD_SWITCH:
      return "PERF_RECORD_SWITCH";
    case PERF_RECORD_SWITCH_CPU_WIDE:
      return "PERF_RECORD_SWITCH_CPU_WIDE";
    case PERF_RECORD_NAMESPACES:
      return "PERF_RECORD_NAMESPACES";
    case PERF_RECORD_HEADER_ATTR:
      return "PERF_RECORD_HEADER_ATTR";
    case PERF_RECORD_HEADER_EVENT_TYPE:
      return "PERF_RECORD_HEADER_EVENT_TYPE";
    case PERF_RECORD_HEADER_TRACING_DATA:
      return "PERF_RECORD_HEADER_TRACING_DATA";
    case PERF_RECORD_HEADER_BUILD_ID:
      return "PERF_RECORD_HEADER_BUILD_ID";
    case PERF_RECORD_FINISHED_ROUND:
      return "PERF_RECORD_FINISHED_ROUND";
    case PERF_RECORD_ID_INDEX:
      return "PERF_RECORD_ID_INDEX";
    case PERF_RECORD_AUXTRACE_INFO:
      return "PERF_RECORD_AUXTRACE_INFO";
    case PERF_RECORD_AUXTRACE:
      return "PERF_RECORD_AUXTRACE";
    case PERF_RECORD_AUXTRACE_ERROR:
      return "PERF_RECORD_AUXTRACE_ERROR";
    case PERF_RECORD_THREAD_MAP:
      return "PERF_RECORD_THREAD_MAP";
    case PERF_RECORD_CPU_MAP:
      return "PERF_RECORD_CPU_MAP";
    case PERF_RECORD_STAT_CONFIG:
      return "PERF_RECORD_STAT_CONFIG";
    case PERF_RECORD_STAT:
      return "PERF_RECORD_STAT";
    case PERF_RECORD_STAT_ROUND:
      return "PERF_RECORD_STAT_ROUND";
    case PERF_RECORD_EVENT_UPDATE:
      return "PERF_RECORD_EVENT_UPDATE";
    case PERF_RECORD_TIME_CONV:
      return "PERF_RECORD_TIME_CONV";
    case PERF_RECORD_HEADER_FEATURE:
      return "PERF_RECORD_HEADER_FEATURE";
    case PERF_RECORD_CGROUP:
      return "PERF_RECORD_CGROUP";
    case PERF_RECORD_KSYMBOL:
      return "PERF_RECORD_KSYMBOL";
  }
  return "UNKNOWN_EVENT_" + std::to_string(type);
}

bool GetEventDataFixedPayloadSize(uint32_t type, size_t* size) {
  switch (type) {
    case PERF_RECORD_MMAP:
      *size = offsetof(struct mmap_event, filename);
      return true;
    case PERF_RECORD_LOST:
      *size = sizeof(struct lost_event);
      return true;
    case PERF_RECORD_COMM:
      *size = offsetof(struct comm_event, comm);
      return true;
    case PERF_RECORD_EXIT:
    case PERF_RECORD_FORK:
      *size = sizeof(struct fork_event);
      return true;
    case PERF_RECORD_THROTTLE:
    case PERF_RECORD_UNTHROTTLE:
      *size = sizeof(struct throttle_event);
      return true;
    case PERF_RECORD_SAMPLE:
      *size = offsetof(struct sample_event, array);
      return true;
    case PERF_RECORD_MMAP2:
      *size = offsetof(struct mmap2_event, filename);
      return true;
    case PERF_RECORD_AUX:
      *size = sizeof(struct aux_event);
      return true;
    case PERF_RECORD_ITRACE_START:
      *size = sizeof(struct itrace_start_event);
      return true;
    case PERF_RECORD_LOST_SAMPLES:
      *size = sizeof(struct lost_samples_event);
      return true;
    case PERF_RECORD_SWITCH:
      *size = sizeof(struct perf_event_header);
      return true;
    case PERF_RECORD_SWITCH_CPU_WIDE:
      *size = sizeof(struct context_switch_event);
      return true;
    case PERF_RECORD_NAMESPACES:
      *size = offsetof(struct namespaces_event, link_info);
      return true;
    case PERF_RECORD_FINISHED_ROUND:
      *size = sizeof(struct perf_event_header);
      return true;
    case PERF_RECORD_AUXTRACE_INFO:
      *size = offsetof(struct auxtrace_info_event, priv);
      return true;
    case PERF_RECORD_AUXTRACE:
      *size = sizeof(struct auxtrace_event);
      return true;
    case PERF_RECORD_AUXTRACE_ERROR:
      *size = offsetof(struct auxtrace_error_event, msg);
      return true;
    case PERF_RECORD_THREAD_MAP:
      *size = offsetof(struct thread_map_event, entries);
      return true;
    case PERF_RECORD_STAT_CONFIG:
      *size = offsetof(struct stat_config_event, data);
      return true;
    case PERF_RECORD_STAT:
      *size = sizeof(struct stat_event);
      return true;
    case PERF_RECORD_STAT_ROUND:
      *size = sizeof(struct stat_round_event);
      return true;
    case PERF_RECORD_TIME_CONV:
      *size = offsetof(struct time_conv_event, time_cycles);
      return true;
    case PERF_RECORD_CGROUP:
      *size = offsetof(struct cgroup_event, path);
      return true;
    case PERF_RECORD_KSYMBOL:
      *size = offsetof(struct ksymbol_event, name);
      return true;
    default:
      LOG(ERROR) << "Unsupported event " << GetEventName(type);
  }
  return false;
}

bool GetEventDataVariablePayloadSize(const event_t& event,
                                     size_t remaining_event_size,
                                     size_t* size) {
  switch (event.header.type) {
    // The below events have variable payload event data and might have trailing
    // sample info data.
    case PERF_RECORD_MMAP: {
      size_t max_filename_size =
          std::min<size_t>(remaining_event_size, PATH_MAX);
      if (!GetUint64AlignedStringLength(event.mmap.filename, max_filename_size,
                                        "mmap.filename", size)) {
        return false;
      }
      break;
    }
    case PERF_RECORD_COMM: {
      size_t max_comm_size =
          std::min<size_t>(remaining_event_size, kMaxCommSize);
      if (!GetUint64AlignedStringLength(event.comm.comm, max_comm_size,
                                        "comm.comm", size)) {
        return false;
      }
      break;
    }
    case PERF_RECORD_MMAP2: {
      size_t max_filename_size =
          std::min<size_t>(remaining_event_size, PATH_MAX);
      if (!GetUint64AlignedStringLength(event.mmap2.filename, max_filename_size,
                                        "mmap2.filename", size)) {
        return false;
      }
      break;
    }
    case PERF_RECORD_NAMESPACES: {
      size_t max_nr_namespaces =
          remaining_event_size / sizeof(struct perf_ns_link_info);
      if (event.namespaces.nr_namespaces > max_nr_namespaces) {
        LOG(ERROR) << "Number of namespace entries "
                   << event.namespaces.nr_namespaces
                   << " cannot exceed the maximum possible number of namespace"
                   << " entries " << max_nr_namespaces;
        return false;
      }
      *size = event.namespaces.nr_namespaces * sizeof(struct perf_ns_link_info);
      break;
    }
    case PERF_RECORD_CGROUP: {
      size_t max_path_size = std::min<size_t>(remaining_event_size, PATH_MAX);
      if (!GetUint64AlignedStringLength(event.cgroup.path, max_path_size,
                                        "cgroup.path", size)) {
        return false;
      }
      break;
    }
    case PERF_RECORD_KSYMBOL: {
      size_t max_name_size =
          std::min<size_t>(remaining_event_size, kMaxKsymNameLen);
      if (!GetUint64AlignedStringLength(event.ksymbol.name, max_name_size,
                                        "ksymbol.name", size)) {
        return false;
      }
      break;
    }
    // The below events don't have variable payload event data but might have
    // trailing sample info data.
    case PERF_RECORD_LOST:
    case PERF_RECORD_EXIT:
    case PERF_RECORD_FORK:
    case PERF_RECORD_THROTTLE:
    case PERF_RECORD_UNTHROTTLE:
    case PERF_RECORD_SAMPLE:
    case PERF_RECORD_AUX:
    case PERF_RECORD_ITRACE_START:
    case PERF_RECORD_LOST_SAMPLES:
    case PERF_RECORD_SWITCH:
    case PERF_RECORD_SWITCH_CPU_WIDE:
      *size = 0;
      break;
    // The below events have varaible payload event data but don't have trailing
    // sample info data so ensure the variable payload size completely exhausts
    // the remaining_event_size.
    case PERF_RECORD_AUXTRACE_INFO: {
      *size = remaining_event_size;
      break;
    }
    case PERF_RECORD_AUXTRACE_ERROR: {
      size_t msg_size = 0;
      size_t max_msg_size =
          std::min<size_t>(remaining_event_size, MAX_AUXTRACE_ERROR_MSG);
      if (!GetUint64AlignedStringLength(event.auxtrace_error.msg, max_msg_size,
                                        "auxtrace_error.msg", &msg_size)) {
        return false;
      }
      if (msg_size != remaining_event_size) {
        LOG(ERROR) << "Auxtrace error message size " << msg_size
                   << " doesn't match the remaining event size "
                   << remaining_event_size;
        return false;
      }
      *size = msg_size;
      break;
    }
    case PERF_RECORD_THREAD_MAP: {
      size_t nr_thread_maps =
          remaining_event_size / sizeof(struct thread_map_event_entry);
      if (event.thread_map.nr != nr_thread_maps) {
        LOG(ERROR) << "Number of thread map entries " << event.thread_map.nr
                   << " from event doesn't match number of thread map entries "
                   << nr_thread_maps << " derived from remaining event size";
        return false;
      }
      *size = event.thread_map.nr * sizeof(struct thread_map_event_entry);
      if (*size != remaining_event_size) {
        LOG(ERROR) << "Total thread map entries size " << *size
                   << " doesn't match the remaining event size "
                   << remaining_event_size;
        return false;
      }

      // Even though the comm sizes from the thread map entries are not
      // required, calculate them to validate null termination within
      // |kMaxThreadCommSize|.
      for (u64 i = 0; i < event.thread_map.nr; ++i) {
        size_t comm_size = 0;
        if (!GetUint64AlignedStringLength(
                event.thread_map.entries[i].comm, kMaxThreadCommSize,
                "thread_map.entry.comm", &comm_size)) {
          return false;
        }
      }
      break;
    }
    case PERF_RECORD_STAT_CONFIG: {
      size_t nr_stat_config =
          remaining_event_size / sizeof(struct stat_config_event_entry);
      if (event.stat_config.nr != nr_stat_config) {
        LOG(ERROR) << "Number of stat config entries " << event.stat_config.nr
                   << " from event doesn't match number of stat config entries "
                   << nr_stat_config << " derived from remaining event size";
        return false;
      }
      *size = event.stat_config.nr * sizeof(struct stat_config_event_entry);
      if (*size != remaining_event_size) {
        LOG(ERROR) << "Total stat config entries size " << *size
                   << " doesn't match the remaining event size "
                   << remaining_event_size;
        return false;
      }
      break;
    }
    // The below events changed size between kernel versions,
    // so ensure the variable payload size completely exhausts
    // the remaining_event_size.
    case PERF_RECORD_TIME_CONV: {
      if (remaining_event_size == 0 ||
          remaining_event_size ==
              (sizeof(time_conv_event) -
               offsetof(struct time_conv_event, time_cycles))) {
        *size = remaining_event_size;
        break;
      }
      return false;
    }
    // The below events neither have varaible payload event data nor have
    // trailing sample info data so ensure there is no remaining_event_size.
    case PERF_RECORD_FINISHED_ROUND:
    case PERF_RECORD_AUXTRACE:
    case PERF_RECORD_STAT:
    case PERF_RECORD_STAT_ROUND:
      if (remaining_event_size != 0) {
        LOG(ERROR)
            << "Non-zero remaining event size " << remaining_event_size
            << " received for event " << GetEventName(event.header.type)
            << " with no varaible payload event data or sample info data";
        return false;
      }
      *size = 0;
      break;
    default:
      LOG(ERROR) << "Unsupported event " << GetEventName(event.header.type);
      return false;
  }
  if (*size % sizeof(u64) != 0) {
    *size = 0;
    LOG(ERROR) << "Variable payload size " << *size << " of event "
               << GetEventName(event.header.type) << " is not uint64_t aligned";
    return false;
  }
  return true;
}

bool GetEventDataVariablePayloadSize(const PerfDataProto_PerfEvent& event,
                                     size_t* size) {
  switch (event.header().type()) {
    case PERF_RECORD_MMAP:
      *size =
          GetUint64AlignedStringLength(event.mmap_event().filename().size());
      break;
    case PERF_RECORD_COMM:
      *size = GetUint64AlignedStringLength(event.comm_event().comm().size());
      break;
    case PERF_RECORD_MMAP2:
      *size =
          GetUint64AlignedStringLength(event.mmap_event().filename().size());
      break;
    case PERF_RECORD_NAMESPACES:
      *size = event.namespaces_event().link_info_size() *
              sizeof(struct perf_ns_link_info);
      break;
    case PERF_RECORD_AUXTRACE_INFO:
      *size =
          event.auxtrace_info_event().unparsed_binary_blob_priv_data_size() *
          sizeof(u64);
      break;
    case PERF_RECORD_AUXTRACE_ERROR:
      *size = GetUint64AlignedStringLength(
          event.auxtrace_error_event().msg().size());
      break;
    case PERF_RECORD_THREAD_MAP:
      *size = event.thread_map_event().entries_size() *
              sizeof(struct thread_map_event_entry);
      break;
    case PERF_RECORD_STAT_CONFIG:
      *size = event.stat_config_event().data_size() *
              sizeof(struct stat_config_event_entry);
      break;
    case PERF_RECORD_CGROUP:
      *size = GetUint64AlignedStringLength(event.cgroup_event().path().size());
      break;
    case PERF_RECORD_KSYMBOL:
      *size = GetUint64AlignedStringLength(event.ksymbol_event().name().size());
      break;
    // The below event gained new fields in new kernel versions. Return the size
    // difference if any of the new fields are present.
    case PERF_RECORD_TIME_CONV:
      *size = event.time_conv_event().has_time_cycles()
                  ? sizeof(time_conv_event) -
                        offsetof(struct time_conv_event, time_cycles)
                  : 0;
      break;
    // The below supported perf events have no varaible payload event data.
    case PERF_RECORD_LOST:
    case PERF_RECORD_EXIT:
    case PERF_RECORD_FORK:
    case PERF_RECORD_THROTTLE:
    case PERF_RECORD_UNTHROTTLE:
    case PERF_RECORD_SAMPLE:
    case PERF_RECORD_AUX:
    case PERF_RECORD_ITRACE_START:
    case PERF_RECORD_LOST_SAMPLES:
    case PERF_RECORD_SWITCH:
    case PERF_RECORD_SWITCH_CPU_WIDE:
    case PERF_RECORD_FINISHED_ROUND:
    case PERF_RECORD_AUXTRACE:
    case PERF_RECORD_STAT:
    case PERF_RECORD_STAT_ROUND:
      *size = 0;
      break;
    default:
      LOG(ERROR) << "Unsupported event " << GetEventName(event.header().type());
      return false;
  }
  if (*size % sizeof(u64) != 0) {
    *size = 0;
    LOG(ERROR) << "Variable payload size " << *size << " of event "
               << GetEventName(event.header().type())
               << " is not uint64_t aligned";
    return false;
  }
  return true;
}

size_t GetEventDataSize(const event_t& event) {
  u32 type = event.header.type;
  size_t remaining_event_size = event.header.size;
  size_t fixed_payload_size = 0;
  if (!GetEventDataFixedPayloadSize(type, &fixed_payload_size)) {
    LOG(ERROR) << "Couldn't get fixed payload size for event "
               << GetEventName(type);
    return 0L;
  }
  if (fixed_payload_size > remaining_event_size) {
    LOG(ERROR) << "Event data's fixed payload size " << fixed_payload_size
               << " of event " << GetEventName(type)
               << " cannot exceed remaining perf event size "
               << remaining_event_size;
    return 0L;
  }
  remaining_event_size -= fixed_payload_size;

  size_t variable_payload_size = 0;
  if (!GetEventDataVariablePayloadSize(event, remaining_event_size,
                                       &variable_payload_size)) {
    LOG(ERROR) << "Couldn't get variable payload size for event "
               << GetEventName(type);
    return 0L;
  }

  return fixed_payload_size + variable_payload_size;
}

size_t GetEventDataSize(const PerfDataProto_PerfEvent& event) {
  u32 type = event.header().type();

  size_t fixed_payload_size = 0;
  if (!GetEventDataFixedPayloadSize(type, &fixed_payload_size)) {
    LOG(ERROR) << "Couldn't get fixed payload size for event "
               << GetEventName(type);
    return 0L;
  }

  size_t variable_payload_size = 0;
  if (!GetEventDataVariablePayloadSize(event, &variable_payload_size)) {
    LOG(ERROR) << "Couldn't get variable payload size for event "
               << GetEventName(type);
    return 0L;
  }

  size_t size = fixed_payload_size + variable_payload_size;
  // A perf event size cannot exceed the maximum allowed event size in a
  // perf.data file.
  if (size > UINT16_MAX) {
    LOG(ERROR) << "Calculated event size " << size << " for event "
               << GetEventName(type) << " cannot exceed maximum limit "
               << UINT16_MAX;
    return 0L;
  }
  return size;
}

}  // namespace quipper
