#ifndef PERF_DATA_CONVERTER_SRC_QUIPPER_ADDRESS_CONTEXT_H_
#define PERF_DATA_CONVERTER_SRC_QUIPPER_ADDRESS_CONTEXT_H_

#include "kernel/perf_event.h"
#include "src/quipper/perf_data.pb.h"

namespace quipper {

enum class AddressContext {
  kUnknown,
  kHostKernel,
  kHostUser,
  kGuestKernel,
  kGuestUser,
  kHypervisor
};

AddressContext ContextFromHeader(
    const quipper::PerfDataProto::EventHeader& header);
AddressContext ContextFromCallchain(enum perf_callchain_context context);

}  // namespace quipper

#endif  // PERF_DATA_CONVERTER_SRC_QUIPPER_ADDRESS_CONTEXT_H_
