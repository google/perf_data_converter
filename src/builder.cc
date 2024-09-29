/* Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/builder.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include <unordered_map>
#include <unordered_set>

#include "src/quipper/base/logging.h"
#include "google/protobuf/io/gzip_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"

using google::protobuf::io::StringOutputStream;
using google::protobuf::io::GzipOutputStream;
using google::protobuf::io::FileOutputStream;
using google::protobuf::RepeatedField;

namespace perftools {
namespace profiles {
typedef std::unordered_map<uint64, uint64> IndexMap;
typedef std::unordered_set<uint64> IndexSet;
}  // namespace profiles
}  // namespace perftools

namespace perftools {
namespace profiles {

void AddCallstackToSample(Sample *sample, const void *const *stack, int depth,
                          CallstackType type) {
  if (depth <= 0) return;
  if (type == kInterrupt) {
    sample->add_location_id(reinterpret_cast<uint64_t>(stack[0]));
  } else {
    sample->add_location_id(reinterpret_cast<uint64_t>(stack[0]) - 1);
  }
  // These are raw stack unwind addresses, so adjust them by -1 to land
  // inside the call instruction (although potentially misaligned).
  for (int i = 1; i < depth; i++) {
    sample->add_location_id(reinterpret_cast<uint64_t>(stack[i]) - 1);
  }
}

Builder::Builder() : profile_(new Profile()) {
  // string_table[0] must be ""
  profile_->add_string_table("");
}

int64_t Builder::StringId(const char *str) {
  if (str == nullptr || !str[0]) {
    return 0;
  }
  return InternalStringId(str);
}

int64_t Builder::InternalStringId(const std::string &str) {
  const int64_t index = profile_->string_table_size();
  const auto inserted = strings_.emplace(str, index);
  if (!inserted.second) {
    // Failed to insert -- use existing id.
    return inserted.first->second;
  }
  profile_->add_string_table(inserted.first->first);
  return index;
}

uint64_t Builder::FunctionId(const char *name, const char *system_name,
                             const char *file, int64_t start_line) {
  int64_t name_index = StringId(name);
  int64_t system_name_index = StringId(system_name);
  int64_t file_index = StringId(file);

  auto fn =
      std::make_tuple(name_index, system_name_index, file_index, start_line);

  int64_t index = profile_->function_size() + 1;
  const auto inserted = functions_.insert(std::make_pair(fn, index));
  const bool insert_successful = inserted.second;
  if (!insert_successful) {
    const auto existing_function = inserted.first;
    return existing_function->second;
  }

  auto function = profile_->add_function();
  function->set_id(index);
  function->set_name(name_index);
  function->set_system_name(system_name_index);
  function->set_filename(file_index);
  function->set_start_line(start_line);
  return index;
}

void Builder::SetDocURL(const std::string &url) {
  const std::string kHttp = "http://";
  const std::string kHttps = "https://";

  if (!url.empty() && url.compare(0, kHttp.size(), kHttp) != 0 &&
      url.compare(0, kHttps.size(), kHttps) != 0) {
    if (error_.empty()) {
      error_ = "setting invalid profile doc URL '";
      error_.append(url);
      error_.append("'");
    }
    return;
  }
  profile_->set_doc_url(InternalStringId(url));
}

bool Builder::Emit(std::string *output) {
  *output = "";
  if (!profile_ || !Finalize()) {
    return false;
  }
  return Marshal(*profile_, output);
}

bool Builder::Marshal(const Profile &profile, std::string *output) {
  *output = "";
  StringOutputStream stream(output);
  GzipOutputStream gzip_stream(&stream);
  if (!profile.SerializeToZeroCopyStream(&gzip_stream)) {
    LOG(ERROR) << "Failed to serialize to gzip stream";
    return false;
  }
  return gzip_stream.Close();
}

bool Builder::MarshalToFile(const Profile &profile, int fd) {
  FileOutputStream stream(fd);
  GzipOutputStream gzip_stream(&stream);
  if (!profile.SerializeToZeroCopyStream(&gzip_stream)) {
    LOG(ERROR) << "Failed to serialize to gzip stream";
    return false;
  }
  return gzip_stream.Close();
}

bool Builder::MarshalToFile(const Profile &profile, const char *filename) {
  int fd;
  while ((fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0 &&
         errno == EINTR) {
  }
  if (fd == -1) {
    PLOG(ERROR) << "Failed to open file " << filename;
    return false;
  }
  int ret = MarshalToFile(profile, fd);
  close(fd);
  return ret;
}

// Returns a bool indicating if the profile is valid. It logs any
// errors it encounters.
bool Builder::CheckValid(const Profile &profile) {
  IndexSet mapping_ids;
  mapping_ids.reserve(profile.mapping_size());
  for (const auto &mapping : profile.mapping()) {
    const int64_t id = mapping.id();
    if (id != 0) {
      const bool insert_successful = mapping_ids.insert(id).second;
      if (!insert_successful) {
        LOG(ERROR) << "Duplicate mapping id: " << id;
        return false;
      }
    }
  }

  IndexSet function_ids;
  function_ids.reserve(profile.function_size());
  for (const auto &function : profile.function()) {
    const int64_t id = function.id();
    if (id != 0) {
      const bool insert_successful = function_ids.insert(id).second;
      if (!insert_successful) {
        LOG(ERROR) << "Duplicate function id: " << id;
        return false;
      }
    }
  }

  IndexSet location_ids;
  location_ids.reserve(profile.location_size());
  for (const auto &location : profile.location()) {
    const int64_t id = location.id();
    if (id != 0) {
      const bool insert_successful = location_ids.insert(id).second;
      if (!insert_successful) {
        LOG(ERROR) << "Duplicate location id: " << id;
        return false;
      }
    }
    const int64_t mapping_id = location.mapping_id();
    if (mapping_id != 0 && mapping_ids.count(mapping_id) == 0) {
      LOG(ERROR) << "Missing mapping " << mapping_id << " from location " << id;
      return false;
    }
    for (const auto &line : location.line()) {
      int64_t function_id = line.function_id();
      if (function_id != 0 && function_ids.count(function_id) == 0) {
        LOG(ERROR) << "Missing function " << function_id;
        return false;
      }
    }
  }

  int sample_type_len = profile.sample_type_size();
  if (sample_type_len == 0) {
    LOG(ERROR) << "No sample type specified";
    return false;
  }

  const int default_sample_type = profile.default_sample_type();
  if (default_sample_type <= 0 ||
      default_sample_type >= profile.string_table_size()) {
    LOG(ERROR) << "No default sample type specified";
    return false;
  }

  std::unordered_set<int> value_types;
  for (const auto &sample_type : profile.sample_type()) {
    if (!value_types.insert(sample_type.type()).second) {
      LOG(ERROR) << "Duplicate sample_type specified";
      return false;
    }
  }

  if (value_types.count(default_sample_type) == 0) {
    LOG(ERROR) << "Default sample type not found";
    return false;
  }

  for (const auto &sample : profile.sample()) {
    if (sample.value_size() != sample_type_len) {
      LOG(ERROR) << "Found sample with " << sample.value_size()
                 << " values, expecting " << sample_type_len;
      return false;
    }
    for (uint64_t location_id : sample.location_id()) {
      if (location_id == 0) {
        LOG(ERROR) << "Sample referencing location_id=0";
        return false;
      }

      if (location_ids.count(location_id) == 0) {
        LOG(ERROR) << "Missing location " << location_id;
        return false;
      }
    }

    for (const auto &label : sample.label()) {
      int64_t str = label.str();
      int64_t num = label.num();
      if (str != 0 && num != 0) {
        LOG(ERROR) << "One of str/num must be unset, got " << str << "," << num;
        return false;
      }
    }
  }
  return true;
}

// Finalizes the profile for serialization.
// - Creates missing locations for unsymbolized profiles.
// - Associates locations to the corresponding mappings.
bool Builder::Finalize() {
  if (!error_.empty()) {
    // We really should be returning an absl::Status instead of logging here.
    LOG(ERROR) << error_;
    return false;
  }
  if (profile_->location_size() == 0) {
    IndexMap address_to_id;
    address_to_id.reserve(profile_->sample_size());
    for (auto &sample : *profile_->mutable_sample()) {
      // Copy sample locations into a temp vector, and then clear and
      // repopulate it with the corresponding location IDs.
      const RepeatedField<uint64_t> addresses = sample.location_id();
      sample.clear_location_id();
      for (uint64_t address : addresses) {
        int64_t index = address_to_id.size() + 1;
        const auto inserted = address_to_id.emplace(address, index);
        if (inserted.second) {
          auto loc = profile_->add_location();
          loc->set_id(index);
          loc->set_address(address);
        }
        sample.add_location_id(inserted.first->second);
      }
    }
  }

  // Look up location address on mapping ranges.
  if (profile_->mapping_size() > 0) {
    std::map<uint64_t, std::pair<uint64_t, uint64_t> > mapping_map;
    for (const auto &mapping : profile_->mapping()) {
      mapping_map[mapping.memory_start()] =
          std::make_pair(mapping.memory_limit(), mapping.id());
    }

    for (auto &loc : *profile_->mutable_location()) {
      if (loc.address() != 0 && loc.mapping_id() == 0) {
        auto mapping = mapping_map.upper_bound(loc.address());
        if (mapping == mapping_map.begin()) {
          // Address landed before the first mapping
          continue;
        }
        mapping--;
        uint64_t limit = mapping->second.first;
        uint64_t id = mapping->second.second;

        if (loc.address() <= limit) {
          loc.set_mapping_id(id);
        }
      }
    }
  }
  return CheckValid(*profile_);
}

}  // namespace profiles
}  // namespace perftools
