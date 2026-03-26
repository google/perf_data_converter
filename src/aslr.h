#ifndef PERFTOOLS_PROFILES_PROTO_ASLR_H_
#define PERFTOOLS_PROFILES_PROTO_ASLR_H_

#include "google/protobuf/io/zero_copy_stream.h"
#include "src/profile.pb.h"

namespace perftools {
namespace profiles {

using ::google::protobuf::io::ZeroCopyOutputStream;

namespace internal {

bool SerializeWithAslrEntropyRedaction(const Profile& profile,
                                       ZeroCopyOutputStream& out_stream);

}  // namespace internal

}  // namespace profiles
}  // namespace perftools

#endif  // PERFTOOLS_PROFILES_PROTO_ASLR_H_
