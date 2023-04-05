// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_PERF_DATA_UTILS_H_
#define CHROMIUMOS_WIDE_PROFILING_PERF_DATA_UTILS_H_

#include <stddef.h>  // for size_t
#include <stdlib.h>  // for free()

#include <memory>

#include "binary_data_utils.h"  // for Align<T>
#include "compat/string.h"

namespace quipper {

class PerfDataProto_PerfEvent;
class PerfDataProto_SampleInfo;

// Used by malloced_unique_ptr.
struct FreeDeleter {
  inline void operator()(void* pointer) { free(pointer); }
};

// A modified version of std::unique_ptr that holds a pointer allocated by
// malloc or its cousins rather than by new. Calls free() to free the pointer
// when it is destroyed.
template <typename T>
using malloced_unique_ptr = std::unique_ptr<T, FreeDeleter>;

// Allocate |size| bytes of heap memory using calloc, returning the allocated
// memory as the variable-sized type event_t.
event_t* CallocMemoryForEvent(size_t size);

// Reallocate |event| which was previously allocated by CallocMemoryForEvent()
// to memory with a new size |new_size|.
event_t* ReallocMemoryForEvent(event_t* event, size_t new_size);

// Allocate |size| bytes of heap memory using calloc, returning the allocated
// memory as the variable-sized build ID type.
build_id_event* CallocMemoryForBuildID(size_t size);

// Allocate |size| bytes of heap memory using calloc, returning the allocated
// memory as the variable-sized feature event type.
feature_event* CallocMemoryForFeature(size_t size);

// In perf data, strings are packed into the smallest number of 8-byte blocks
// possible, including a null terminator.
// e.g.
//    "0123"                ->  5 bytes -> packed into  8 bytes
//    "0123456"             ->  8 bytes -> packed into  8 bytes
//    "01234567"            ->  9 bytes -> packed into 16 bytes
//    "0123456789abcd"      -> 15 bytes -> packed into 16 bytes
//    "0123456789abcde"     -> 16 bytes -> packed into 16 bytes
//    "0123456789abcdef"    -> 17 bytes -> packed into 24 bytes
//
// Returns the size of the 8-byte-aligned memory for storing a string of the
// |size|.
inline size_t GetUint64AlignedStringLength(size_t size) {
  return Align<uint64_t>(size + 1);
}

// Gets the given |str| length using strnlen. Returns true when the |str| is
// null-terminated before the max_size. Otherwise, returns false.
bool GetStringLength(const char* str, size_t max_size, size_t* size);

// If |event| is not of type PERF_RECORD_SAMPLE, returns the SampleInfo field
// within it. Otherwise returns nullptr.
const PerfDataProto_SampleInfo* GetSampleInfoForEvent(
    const PerfDataProto_PerfEvent& event);

// Returns the correct |sample_time_ns| field of a PerfEvent.
uint64_t GetTimeFromPerfEvent(const PerfDataProto_PerfEvent& event);

// GetSampleIdFromPerfEvent returns a valid sample id if the provided perf event
// has a sample id. Otherwise, returns 0.
uint64_t GetSampleIdFromPerfEvent(const PerfDataProto_PerfEvent& event);

// Returns the metadata name for the given type.
std::string GetMetadataName(uint32_t type);

// Returns the event name for the given type.
std::string GetEventName(uint32_t type);

// For supported event |type|, updates |size| with the event data's fixed
// payload size and returns true. Returns false for unsupported event type.
bool GetEventDataFixedPayloadSize(uint32_t type, size_t* size);

// For supported event |type|, calculates the event data's variable payload
// size, validates it with the |remaining_event_size|, and updates |size| with
// variable payload size. Returns false when the validation with the remaining
// event size fails, when the variable payload size is not uint64_t aligned or
// for unsupported event type.
bool GetEventDataVariablePayloadSize(const event_t& event,
                                     size_t remaining_event_size, size_t* size);
// For supported event |type|, calculates the event data's variable payload
// size, and updates |size| with variable payload size. Returns false for
// unsupported event type.
bool GetEventDataVariablePayloadSize(const PerfDataProto_PerfEvent& event,
                                     size_t* size);

// Returns the event data size not including the sample info data size.
// On error, returns 0.
size_t GetEventDataSize(const event_t& event);
size_t GetEventDataSize(const PerfDataProto_PerfEvent& event);

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_PERF_DATA_UTILS_H_
