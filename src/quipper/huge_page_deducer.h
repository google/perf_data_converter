// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERF_DATA_CONVERTER_SRC_QUIPPER_HUGE_PAGE_DEDUCER_H_
#define PERF_DATA_CONVERTER_SRC_QUIPPER_HUGE_PAGE_DEDUCER_H_

#include "compat/proto.h"

namespace quipper {

// Walks through all the perf events in |*events| and deduces correct |pgoff|
// and |filename| values for MMAP events.
//
// This may not correctly handle perf data that has been processed to remove
// MMAPs that contain no sample events, since one or more of the mappings
// necessary to resolve the huge pages mapping could have been discarded. The
// result would be that the huge pages mapping would remain as "//anon" and the
// other mappings would remain unchanged.
void DeduceHugePages(RepeatedPtrField<PerfDataProto::PerfEvent>* events);

// Walks through all the perf events in |*events| and searches for split
// mappings. Combines these split mappings into one and replaces the split
// mapping events. Modifies the events vector stored in |*events|.
void CombineMappings(RepeatedPtrField<PerfDataProto::PerfEvent>* events);

}  // namespace quipper

#endif  // PERF_DATA_CONVERTER_SRC_QUIPPER_HUGE_PAGE_DEDUCER_H_
