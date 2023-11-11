// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "string_utils.h"

namespace quipper {

void TrimWhitespace(std::string* str) {
  const char kWhitespaceCharacters[] = " \t\n\r";
  size_t end = str->find_last_not_of(kWhitespaceCharacters);
  if (end != std::string::npos) {
    size_t start = str->find_first_not_of(kWhitespaceCharacters);
    *str = str->substr(start, end + 1 - start);
  } else {
    // The string contains only whitespace.
    *str = "";
  }
}

void SplitString(const std::string& str, char delimiter,
                 std::vector<std::string>* tokens) {
  std::stringstream ss(str);
  std::string token;
  while (std::getline(ss, token, delimiter)) tokens->push_back(token);
}

std::string RootPath(const std::string& filename) {
  // Handle named VMAs, like "[anon:...]" that are named using PR_SET_VMA, or
  // like "/memfd:..." that are created using memfd_create, by generating
  // custom root paths for them..
  if (filename.rfind("[anon:", 0) == 0) return "[anon:";
  if (filename.rfind("/memfd:", 0) == 0) return "/memfd:";
  if (filename[0] != '/') return "";
  std::string root_path = "";
  size_t pos = 1;
  while (pos < filename.size() && filename[pos] == '/') ++pos;
  for (int i = 1, next = pos; i < 3; ++i) {
    while (next < filename.size() && filename[next] == '/') ++next;
    next = filename.find('/', next);
    if (next == std::string::npos) break;
    pos = next;
  }
  return filename.substr(0, pos);
}

}  // namespace quipper
