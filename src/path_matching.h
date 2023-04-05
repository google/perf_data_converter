/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PERFTOOLS_PATH_MATCHING_H_
#define PERFTOOLS_PATH_MATCHING_H_

#include <string>

namespace perftools {

// Checks if a file is a .so file which is being used by an executing binary
// but has been deleted.
bool IsDeletedSharedObject(const std::string& path);
// Checks if a file is a .so file with the version appended to it.
bool IsVersionedSharedObject(const std::string& path);

}  // namespace perftools

#endif  // PERFTOOLS_PATH_MATCHING_H_
