// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "string_utils.h"

#include <stdlib.h>

#include <cstdint>
#include <sstream>
#include <string>

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
  // For temporary files and directory, capture only the top level directory.
  if (filename.rfind("/tmp/", 0) == 0) return "/tmp";
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

bool Stou32(const std::string& str, uint32_t& result) {
  char* end;
  
  result = strtoul(str.c_str(), &end, 10);
  if (*end) return false;
  return true;
}

bool ParseCPUNumbers(const std::string& cpus, std::vector<uint32_t>& result) {
  std::vector<std::string> tokens;
  SplitString(cpus, ',', &tokens);
  if (tokens.empty()) return false;

  for (const auto& token : tokens) {
    std::vector<std::string> range_tokens;
    SplitString(token, '-', &range_tokens);

    if (range_tokens.empty() || range_tokens.size() > 2) {
      return false;
    }

    if (range_tokens.size() == 1) {
      uint32_t cpu;
      if (!Stou32(token, cpu)) {
        return false;
      }
      result.push_back(cpu);
      continue;
    }

    uint32_t cpu_start, cpu_end;
    if (!Stou32(range_tokens[0], cpu_start) ||
        !Stou32(range_tokens[1], cpu_end)) {
      return false;
    }

    if (cpu_end <= cpu_start) {
      return false;
    }

    for (uint32_t i = cpu_start; i <= cpu_end; ++i) {
      result.push_back(i);
    }
  }

  return true;
}

std::string FormatCPUNumbers(const std::vector<uint32_t>& cpus) {
  if (cpus.empty()) return "";

  std::stringstream tokens;

  for (int i = 0; i < cpus.size();) {
    uint32_t start = cpus[i];
    int j;
    for (j = i + 1; j < cpus.size(); ++j) {
      if (cpus[j] > cpus[j - 1] + 1) break;
    }
    if (i > 0) tokens << ",";
    if (j - i == 1)
      tokens << cpus[i];
    else
      tokens << start << '-' << cpus[j - 1];
    i = j;
  }
  return tokens.str();
}

}  // namespace quipper
