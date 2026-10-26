#include "src/aslr.h"

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <limits>

#include "absl/container/flat_hash_map.h"

#include "google/protobuf/arena.h"

namespace perftools {
namespace profiles {
namespace internal {

bool SerializeWithAslrEntropyRedaction(const Profile& profile,
                                       ZeroCopyOutputStream& out_stream) {
  const size_t page_size = getpagesize();
  uint64_t fake_start_address = 0x200000ULL;
  absl::flat_hash_map<uint64_t, int64_t> mapping_to_difference;

  google::protobuf::Arena arena;
  Profile* redacted = google::protobuf::Arena::Create<Profile>(&arena);

  *redacted = profile;

  for (auto& mapping : *redacted->mutable_mapping()) {
    // Fake mappings that should not be redacted.
    if (mapping.memory_start() == 0 &&
        (mapping.memory_limit() == std::numeric_limits<uint64_t>::max() ||
         mapping.memory_limit() == std::numeric_limits<int64_t>::max() ||
         mapping.memory_limit() == 0)) {
      continue;
    }

    int64_t difference = static_cast<int64_t>(mapping.memory_start()) -
                         static_cast<int64_t>(fake_start_address);
    uint64_t size = mapping.memory_limit() - mapping.memory_start();

    mapping.set_memory_start(fake_start_address);
    mapping.set_memory_limit(mapping.memory_start() + size);

    // Round up to the next page boundary.
    size = (size + page_size - 1) & ~(page_size - 1);
    fake_start_address += size;
    mapping_to_difference[mapping.id()] = difference;
  }

  for (auto& location : *redacted->mutable_location()) {
    if (location.address() == 0) {
      continue;
    }

    // Locations not associated with any mapping in the profile (such as frames
    // outside recorded ELF mappings or presymbolized __start_<section> entries)
    // cannot be shifted relative to a mapping, so clear their raw addresses.
    if (location.mapping_id() == 0) {
      location.set_address(0);
      continue;
    }

    // Either mapping_id refers to one of the fake full-address-space mappings
    // skipped above, or the profile has a dangling non-zero mapping ID.
    const auto it = mapping_to_difference.find(location.mapping_id());
    if (it == mapping_to_difference.end()) {
      continue;
    }

    location.set_address(location.address() - it->second);
  }
  return redacted->SerializeToZeroCopyStream(&out_stream);
}

}  // namespace internal
}  // namespace profiles
}  // namespace perftools
