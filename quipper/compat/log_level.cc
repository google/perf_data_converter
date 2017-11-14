// Copyright (c) 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "log_level.h"

#include "base/logging.h"

namespace quipper {

void SetVerbosityLevel(int level) {
  // The internal scale used by base/logging.h is inverted.
  logging::SetMinLogLevel(-level);
}

}  // namespace quipper
