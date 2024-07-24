#include "address_context.h"

#include "kernel/perf_event.h"
#include "src/quipper/perf_data.pb.h"

namespace quipper {

AddressContext ContextFromHeader(const PerfDataProto::EventHeader& header) {
  if (header.has_misc()) {
    switch (header.misc() & PERF_RECORD_MISC_CPUMODE_MASK) {
      case PERF_RECORD_MISC_KERNEL:
        return AddressContext::kHostKernel;
      case PERF_RECORD_MISC_USER:
        return AddressContext::kHostUser;
      case PERF_RECORD_MISC_GUEST_KERNEL:
        return AddressContext::kGuestKernel;
      case PERF_RECORD_MISC_GUEST_USER:
        return AddressContext::kGuestUser;
      case PERF_RECORD_MISC_HYPERVISOR:
        return AddressContext::kHypervisor;
    }
  }
  return AddressContext::kUnknown;
}

AddressContext ContextFromCallchain(enum perf_callchain_context context) {
  // These PERF_CONTEXT_* values are magic constants used as entries in a
  // perf_callchain to demarcate contexts within that stack. Most values in
  // the stack will be valid program counters, and as such this will return
  // kUnknown for them.
  switch (context) {
    case PERF_CONTEXT_HV:
      return AddressContext::kHypervisor;
    case PERF_CONTEXT_KERNEL:
      return AddressContext::kHostKernel;
    case PERF_CONTEXT_USER:
      return AddressContext::kHostUser;
    case PERF_CONTEXT_GUEST_KERNEL:
      return AddressContext::kGuestKernel;
    case PERF_CONTEXT_GUEST_USER:
      return AddressContext::kGuestUser;
    default:
      return AddressContext::kUnknown;
  }
}

}  // namespace quipper
