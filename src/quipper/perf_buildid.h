// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_PERF_BUILDID_H_
#define CHROMIUMOS_WIDE_PROFILING_PERF_BUILDID_H_

#include <stddef.h>  // for size_t

#include <memory>

namespace quipper {

// Based on kernel/perf_internals.h
const size_t kBuildIDArraySize = 20;
const size_t kBuildIDStringLength = kBuildIDArraySize * 2;

// Makes |build_id| fit the perf format, by either truncating it or adding
// zeroes to the end so that it has length kBuildIDStringLength.
void PerfizeBuildIDString(std::string* build_id);

// Changes |build_id| to the best guess of what the build id was before going
// through perf. Perf will always record a 40 character string for a build ID
// regardless of the format, zero-padding for shorter hash styles. This undoes
// that. Specifically, it keeps removing trailing sequences of four zero bytes
// (or eight '0' characters) until there are no more such sequences, or the
// build id would be empty if the process were repeated.
void TrimZeroesFromBuildIDString(std::string* build_id);

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_PERF_BUILDID_H_
