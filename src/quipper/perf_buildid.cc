// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_buildid.h"

#include <cstdint>
#include <string>

namespace quipper {

void PerfizeBuildIDString(string* build_id) {
  build_id->resize(kBuildIDStringLength, '0');
}

void TrimZeroesFromBuildIDString(string* build_id) {
  const size_t kPaddingSize = 8;
  const string kBuildIDPadding = string(kPaddingSize, '0');

  // Remove kBuildIDPadding from the end of build_id until we cannot remove any
  // more. The build ID string can be reduced down to an empty string. This
  // could happen if the file did not have a build ID but was given a build ID
  // of all zeroes. The empty build ID string would reflect the original lack of
  // build ID.
  while (build_id->size() >= kPaddingSize &&
         build_id->substr(build_id->size() - kPaddingSize) == kBuildIDPadding) {
    build_id->resize(build_id->size() - kPaddingSize);
  }
}

}  // namespace quipper
