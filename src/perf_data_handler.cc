/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/perf_data_handler.h"

#include <sys/mman.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <regex>  
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/intervalmap.h"
#include "src/path_matching.h"
#include "src/quipper/address_context.h"
#include "src/quipper/arm_spe_decoder.h"
#include "src/quipper/binary_data_utils.h"
#include "src/quipper/dso.h"
#include "src/quipper/kernel/perf_event.h"
#include "src/quipper/kernel/perf_internals.h"

using quipper::PerfDataProto;
using quipper::PerfDataProto_MMapEvent;

namespace perftools {
namespace {

static constexpr char kKernelPrefix[] = "[kernel.kallsyms]";
// PID value used by perf for synthesized mmap records for the kernel binary
// and *.ko modules.
static constexpr uint32_t kKernelPid = std::numeric_limits<uint32_t>::max();

bool HasPrefixString(const std::string& s, const char* substr) {
  const size_t substr_len = strlen(substr);
  const size_t s_len = s.length();
  return s_len >= substr_len && s.compare(0, substr_len, substr) == 0;
}

bool HasSuffixString(const std::string& s, const char* substr) {
  const size_t substr_len = strlen(substr);
  const size_t s_len = s.length();
  return s_len >= substr_len &&
         s.compare(s_len - substr_len, substr_len, substr) == 0;
}

// Checks if the auxtrace events contain Arm SPE data.
bool HasArmSPEAuxtrace(const PerfDataProto& perf_proto) {
  for (const auto& event_proto : perf_proto.events()) {
    if (event_proto.has_auxtrace_info_event()) {
      if (event_proto.auxtrace_info_event().type() ==
          quipper::PERF_AUXTRACE_ARM_SPE) {
        return true;
      }
    }
  }
  return false;
}

// Creates a tid->pid mapping through fork & comm events.
std::unordered_map<uint32_t, uint32_t> TidToPidMapping(
    const PerfDataProto& perf_proto) {
  std::unordered_map<uint32_t, uint32_t> t2p;
  for (const auto& event_proto : perf_proto.events()) {
    if (event_proto.has_fork_event()) {
      const auto& fork = event_proto.fork_event();
      t2p[fork.tid()] = fork.pid();
    } else if (event_proto.has_comm_event()) {
      const auto& comm = event_proto.comm_event();
      t2p[comm.tid()] = comm.pid();
    }
  }
  return t2p;
}

// Normalizer iterates through the events and metadata of the given
// PerfDataProto to create its own tables and metadata for each process. During
// the iteration, it drives callbacks to PerfDataHandler with samples in a fully
// normalized form (e.g. samples with their corresponding metadata like their
// mappings, call chains, branch stacks etc.).
class Normalizer {
 public:
  Normalizer(const PerfDataProto& perf_proto, PerfDataHandler* handler)
      : perf_proto_(perf_proto), handler_(handler) {
    for (const auto& build_id : perf_proto_.build_ids()) {
      const std::string& bytes = build_id.build_id_hash();
      std::stringstream hex;
      for (size_t i = 0; i < bytes.size(); ++i) {
        // The char must be turned into an int to be used by stringstream;
        // however, if the byte's value -8 it should be turned to 0x00f8 as an
        // int, not 0xfff8. This cast solves this problem.
        const auto& byte = static_cast<unsigned char>(bytes[i]);
        hex << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(byte);
      }
      std::string filename = PerfDataHandler::NameOrMd5Prefix(
          build_id.filename(), build_id.filename_md5_prefix());
      auto build_id_it = filename_to_build_id_.find(filename);
      BuildIdSource build_id_source =
          build_id.has_is_injected() && build_id.is_injected()
              ? kBuildIdFilenameInjected
              : kBuildIdFilename;
      if (build_id_it != filename_to_build_id_.end() &&
          build_id_it->second.value != hex.str()) {
        LOG(WARNING)
            << "Observed build ID changed for file path " << filename
            << ": initially saw " << build_id_it->second.value << ", now saw "
            << hex.str() << std::hex << " (pid=0x" << build_id.pid() << ")"
            << ". In-flight build ID change may lead to wrong symbolization.";
        build_id_source = kBuildIdFilenameAmbiguous;
      }
      filename_to_build_id_[filename] = BuildId(hex.str(), build_id_source);

      switch (build_id.misc() & quipper::PERF_RECORD_MISC_CPUMODE_MASK) {
        case quipper::PERF_RECORD_MISC_KERNEL:
          if (quipper::IsKernelNonModuleName(filename) ||
              !HasSuffixString(filename, ".ko")) {
            std::string build_id = hex.str();
            if (!maybe_kernel_build_id_.empty() &&
                maybe_kernel_build_id_ != build_id) {
              LOG(WARNING) << "Multiple kernel buildids found, file name: "
                           << filename << ", build id: " << build_id
                           << ". Using the "
                              "first found buildid: "
                           << maybe_kernel_build_id_ << ".";
              break;
            }
            LOG(INFO) << "Using the build id found for the file name: "
                      << filename << ", build id: " << build_id << ".";
            maybe_kernel_build_id_ = build_id;
          }
      }
    }

    // We will use LOST_SAMPLE events to count lost samples for perf version
    // 6.1 or newer.
    // The following code converts the first two parts of the full perf version
    // string into two integers (for example, assume perf_version="6.123.456",
    // then v1=6, v2=123), and decides if the version is 6.1 or newer.
    const std::string& perf_version =
        perf_proto_.string_metadata().has_perf_version()
            ? perf_proto_.string_metadata().perf_version().value()
            : "";
    std::regex rx(R"(([0-9]+)\.([0-9]+).*)");
    std::cmatch cm;
    std::regex_match(perf_version.c_str(), cm, rx);

    if (cm.size() == 3) {
      int v1 = std::stoi(cm[1]);
      int v2 = std::stoi(cm[2]);
      use_lost_sample_ = std::make_pair(v1, v2) >= std::make_pair(6, 1);
    } else {
      LOG(WARNING) << "Invalid perf version: " << perf_version;
    }

    uint64_t current_event_index = 0;
    for (const auto& attr : perf_proto_.file_attrs()) {
      for (uint64_t id : attr.ids()) {
        id_to_event_index_[id] = current_event_index;
      }
      current_event_index++;
    }

    has_spe_auxtrace_ = HasArmSPEAuxtrace(perf_proto_);
    if (has_spe_auxtrace_) {
      tid_to_pid_ = TidToPidMapping(perf_proto_);
    }
  }

  Normalizer(const Normalizer&) = delete;
  Normalizer& operator=(const Normalizer&) = delete;

  ~Normalizer() {}

  // Converts to a protobuf using quipper and then aggregate the results.
  void Normalize();

 private:
  // Using a 32-bit type for the PID values as the max PID value on 64-bit
  // systems is 2^22, see http://man7.org/linux/man-pages/man5/proc.5.html.
  typedef std::unordered_map<uint32_t, PerfDataHandler::Mapping*> PidToMMapMap;
  typedef std::unordered_map<uint32_t, const quipper::PerfDataProto_CommEvent*>
      PidToCommMap;

  typedef IntervalMap<const PerfDataHandler::Mapping*> MMapIntervalMap;

  // Gets the build ID if the mmap2 event's build_id field exists, otherwise
  // finds the build ID according to the filename from the mmap.
  BuildId GetBuildId(const quipper::PerfDataProto_MMapEvent* mmap);

  void ConvertMmapFromKsymbol(
      const quipper::PerfDataProto_KsymbolEvent& ksymbol_event, uint32_t prot,
      quipper::PerfDataProto_MMapEvent* mmap);

  // Copy the parent's mmaps/comm if they exist.  Otherwise, items
  // will be lazily populated.
  void UpdateMapsWithMMapEvent(const quipper::PerfDataProto_MMapEvent* mmap);

  void UpdateMapsWithForkEvent(const quipper::PerfDataProto_ForkEvent& fork);
  void LogStats();

  // Handles the sample_event in event_proto (wrapped in the sample context) and
  // call handler_->Sample.
  void HandleSample(PerfDataHandler::SampleContext* context);

  // Handles the auxtrace event in event_proto that contains the Arm SPE
  // records to parse potential samples.
  void HandleSpeAuxtrace(const quipper::PerfDataProto::PerfEvent& event_proto);

  // Handles the ksymbol event in event_proto.
  void HandleKsymbol(const quipper::PerfDataProto::PerfEvent& event_proto);

  // Handles the perf LOST event or LOST_SAMPLE event.
  void HandleLost(const quipper::PerfDataProto::PerfEvent& event_proto);

  // Get a memoized fake mapping by specified attributes or add one. Never
  // returns nullptr. The returned pointer is owned by the normalizer and
  // bound to its lifetime.
  const PerfDataHandler::Mapping* GetOrAddFakeMapping(const std::string& comm,
                                                      const BuildId& build_id,
                                                      uint64_t comm_md5_prefix,
                                                      uint64_t start_addr);

  // Find the MMAP event which has ip in its address range from pid.  If no
  // mapping is found, returns nullptr.
  const PerfDataHandler::Mapping* TryLookupInPid(uint32_t pid,
                                                 uint64_t ip) const;

  // Find the mapping for a given ip given a context; returns nullptr if none
  // can be found.
  const PerfDataHandler::Mapping* GetMappingFromPidAndIP(
      uint32_t pid, uint64_t ip, quipper::AddressContext context) const;

  // Find the main MMAP event for this pid.  If no mapping is found,
  // nullptr is returned.
  const PerfDataHandler::Mapping* GetMainMMapFromPid(uint32_t pid) const;

  // For profiles with a single event, perf doesn't bother sending the
  // id.  So, if there is only one event, the event index must be 0.
  // Returns the event index corresponding to the id for this sample, or
  // -1 for an error.
  int64_t GetEventIndexForSample(
      const quipper::PerfDataProto_SampleEvent& sample) const;

  const quipper::PerfDataProto& perf_proto_;
  PerfDataHandler* handler_;  // unowned.

  // Mapping we have allocated.
  std::vector<std::unique_ptr<PerfDataHandler::Mapping>> owned_mappings_;
  std::vector<std::unique_ptr<quipper::PerfDataProto_MMapEvent>>
      owned_quipper_mappings_;

  struct FakeMappingKey {
    std::string comm;
    std::string build_id;
    uint64_t comm_md5_prefix;

    bool operator==(const FakeMappingKey& rhs) const {
      return (comm == rhs.comm && build_id == rhs.build_id &&
              comm_md5_prefix == rhs.comm_md5_prefix);
    }

    struct Hasher {
      std::size_t operator()(const FakeMappingKey& k) const noexcept {
        std::size_t h = std::hash<std::string>{}(k.comm);
        h ^= std::hash<std::string>{}(k.build_id);
        h ^= std::hash<uint64_t>{}(k.comm_md5_prefix);
        return h;
      }
    };
  };

  std::unordered_map<FakeMappingKey, const PerfDataHandler::Mapping*,
                     FakeMappingKey::Hasher>
      fake_mappings_;

  // The event for a given sample is determined by the id.
  // Map each id to an index in the event_profiles_ vector.
  std::unordered_map<uint64_t, uint64_t> id_to_event_index_;

  // pid_to_comm_event maps a pid to the corresponding comm event.
  PidToCommMap pid_to_comm_event_;

  // pid_to_mmaps maps a pid to all mmap events that correspond to that pid.
  std::unordered_map<uint32_t, std::unique_ptr<MMapIntervalMap>> pid_to_mmaps_;

  // pid_to_executable_mmap maps a pid to mmap that most likely contains the
  // filename of the main executable for that pid.
  PidToMMapMap pid_to_executable_mmap_;

  // |pid_had_any_mmap_| stores the pids that have their mmap events found.
  std::unordered_set<uint32_t> pid_had_any_mmap_;

  // map filenames to build-ids.
  // TODO(b/250664624): remove this field when buildid-mmap is available to all.
  std::unordered_map<std::string, BuildId> filename_to_build_id_;

  // maybe_kernel_build_id_ contains a possible kernel build id obtained from a
  // perf proto buildid whose misc bits set to
  // quipper::PERF_RECORD_MISC_CPUMODE_MASK. This is used as a kernel buildid
  // when no buildid found for the filename [kernel.kallsyms] .
  std::string maybe_kernel_build_id_;

  // map from cgroup id to pathname.
  std::unordered_map<uint64_t, const std::string> cgroup_map_;

  // Whether we should use lost_samples_event event to count lost samples.
  // Difference between lost_event and lost_samples_event: lost_event can be
  // from all types of lost events, including samples, mmap, comm, etc. So
  // counting lost_event would have the issue of over counting other events
  // against lost samples. We don't have a solution to solve this problem until
  // perf 6.1, which makes the lost_samples_event that counts only lost samples.
  // We still keep the code to count lost_event for backward compatibility of
  // older perf data.
  bool use_lost_sample_ = false;

  // Whether the following auxtrace events contain Arm SPE data.
  bool has_spe_auxtrace_ = false;

  // map from thread ID to process ID. It is used for parsing SPE records into
  // samples.
  std::unordered_map<uint32_t, uint32_t> tid_to_pid_;

  struct {
    int64_t samples = 0;
    int64_t samples_with_addr = 0;
    int64_t synthesized_lost_samples = 0;
    int64_t missing_main_mmap = 0;
    int64_t missing_sample_mmap = 0;
    int64_t missing_addr_mmap = 0;
    int64_t missing_pid = 0;

    int64_t callchain_ips = 0;
    int64_t missing_callchain_mmap = 0;

    int64_t branch_stack_ips = 0;
    int64_t missing_branch_stack_mmap = 0;

    int64_t no_event_errors = 0;
  } stat_;
};

void Normalizer::UpdateMapsWithForkEvent(
    const quipper::PerfDataProto_ForkEvent& fork) {
  if (fork.pid() == fork.ppid()) {
    // Don't care about threads.
    return;
  }
  const auto& it = pid_to_mmaps_.find(fork.ppid());
  if (it != pid_to_mmaps_.end()) {
    pid_to_mmaps_[fork.pid()] =
        std::unique_ptr<MMapIntervalMap>(new MMapIntervalMap(*it->second));
  }
  auto comm_it = pid_to_comm_event_.find(fork.ppid());
  if (comm_it != pid_to_comm_event_.end()) {
    pid_to_comm_event_[fork.pid()] = comm_it->second;
  }
  auto exec_mmap_it = pid_to_executable_mmap_.find(fork.ppid());
  if (exec_mmap_it != pid_to_executable_mmap_.end()) {
    pid_to_executable_mmap_[fork.pid()] = exec_mmap_it->second;
  }
}

static constexpr char kLostMappingFilename[] = "[lost]";
static const uint64_t kLostMd5Prefix = quipper::Md5Prefix(kLostMappingFilename);

void Normalizer::Normalize() {
  // Perf keeps the tracking bits (e.g. comm_exec) in only one of the events'
  // file_attrs.
  bool has_comm_exec_support = false;
  for (const auto& fa : perf_proto_.file_attrs()) {
    if (fa.attr().comm_exec()) {
      has_comm_exec_support = true;
      break;
    }
  }
  for (const auto& event_proto : perf_proto_.events()) {
    if (event_proto.has_mmap_event()) {
      UpdateMapsWithMMapEvent(&event_proto.mmap_event());
      pid_had_any_mmap_.insert(event_proto.mmap_event().pid());
    } else if (event_proto.has_comm_event()) {
      PerfDataHandler::CommContext comm_context;
      if (event_proto.comm_event().pid() == event_proto.comm_event().tid()) {
        if (!has_comm_exec_support ||
            event_proto.header().misc() & quipper::PERF_RECORD_MISC_COMM_EXEC ||
            pid_had_any_mmap_.find(event_proto.comm_event().pid()) ==
                pid_had_any_mmap_.end()) {
          // Based on the perf data collected, comm events (with pid == tid) can
          // be generated (1) on exec() or (2) when the main thread name is set
          // after exec (generating another COMM EVENT, e.g. using PR_SET_NAME
          // http://man7.org/linux/man-pages/man2/prctl.2.html).
          // We want to identify if a comm event (with pid == tid) is due to
          // exec() (the first case) and erase the pid to executable mapping in
          // |pid_to_executable_mmap_| if so.
          // One way to know that comm event is due to exec() is to check if the
          // misc bit is set to PERF_RECORD_MISC_COMM_EXEC. However, this misc
          // bit is only set in newer kernels (>= 3.16) and for execs that
          // happen after perf collection start. Thus, we need to have some
          // heuristics to cover other cases and identify possible comm events
          // that happen due to exec().
          // Another way is to find the contrary scenario for the second case.
          // Commonly found patterns of comm events on setting the main thread
          // name can look like this: FORK EVENT -> COMM EVENT (on exec()) ->
          // MMAP EVENTs -> SAMPLE EVENTs -> COMM EVENT (on setting main thread
          // name) -> SAMPLE EVENTs ... Thus, if a mmap event is already found
          // for a pid before a comm event, this comm event is due to setting
          // the main thread name. Vice versa, if the mmap event is not yet
          // found for the pid, it is very likely this comm event happens due
          // to exec() and |pid_to_executable_mmap_| should be erased.
          // Also note that for older kernels (< 3.16), where the comm_exec
          // in perf file attribute is not set, we will erase the mapping in
          // |pid_to_executable_mmap_| at the occurrence of a comm event.
          // Thus we have the following heuristics:
          // The pid to executable mapping in |pid_to_executable_mmap_| is
          // erased when either one of the following is true (1) comm_exec in
          // perf file attribute is not set (kernel < 3.16) (2) comm_event's
          // PERF_RECORD_MISC_COMM_EXEC misc bit is set in header, meaning an
          // exec() happened, (3) no mmap event for this pid has been found,
          // meaning this is the first comm event after an exec().
          pid_to_executable_mmap_.erase(event_proto.comm_event().pid());
          // is_exec is true if the comm event happened due to exec(), this flag
          // is passed to perf_data_converter and used to modify PerPidInfo.
          comm_context.is_exec = true;
        }
        pid_to_comm_event_[event_proto.comm_event().pid()] =
            &event_proto.comm_event();
      }
      comm_context.comm = &event_proto.comm_event();
      handler_->Comm(comm_context);
    } else if (event_proto.has_fork_event()) {
      UpdateMapsWithForkEvent(event_proto.fork_event());
    } else if (event_proto.has_cgroup_event()) {
      const auto& cgroup = event_proto.cgroup_event();
      cgroup_map_.insert({cgroup.id(), cgroup.path()});
    } else if (event_proto.has_lost_samples_event() ||
               event_proto.has_lost_event()) {
      HandleLost(event_proto);
    } else if (event_proto.has_sample_event()) {
      PerfDataHandler::SampleContext sample_context(event_proto.header(),
                                                    event_proto.sample_event());
      HandleSample(&sample_context);
    } else if (event_proto.has_auxtrace_event()) {
      if (has_spe_auxtrace_) {
        HandleSpeAuxtrace(event_proto);
      }
    } else if (event_proto.has_auxtrace_error_event()) {
      LOG(WARNING) << "auxtrace_error event: "
                   << event_proto.auxtrace_error_event().msg();
    } else if (event_proto.has_ksymbol_event()) {
      HandleKsymbol(event_proto);
    }
  }

  LogStats();
}

void Normalizer::HandleSample(PerfDataHandler::SampleContext* context) {
  CHECK(context != nullptr);
  if (context->spe.is_spe) {
    // This is for the situation like SPE-record generated sample, where we want
    // to use the first file_attrs_index instead of finding it through ID,
    // because such synthesized sample does not have sample.id to find file
    // attribute index.
    // TODO(b/277114009): replace this with a proper file attr when we have
    // feasible way to tell which file attribute is for certain event (e.g. Arm
    // SPE).
    context->file_attrs_index = 0;
  } else {
    context->file_attrs_index = GetEventIndexForSample(context->sample);
  }
  if (context->file_attrs_index == -1) {
    ++stat_.no_event_errors;
    return;
  }
  ++stat_.samples;

  const auto& sample = context->sample;
  uint32_t pid = sample.pid();
  quipper::AddressContext header_context =
      quipper::ContextFromHeader(context->header);

  context->sample_mapping =
      GetMappingFromPidAndIP(pid, sample.ip(), header_context);
  stat_.missing_sample_mmap += context->sample_mapping == nullptr;

  if (sample.has_addr()) {
    ++stat_.samples_with_addr;
    context->addr_mapping =
        GetMappingFromPidAndIP(pid, sample.addr(), header_context);
    stat_.missing_addr_mmap += context->addr_mapping == nullptr;
  }

  context->main_mapping = GetMainMMapFromPid(pid);
  std::unique_ptr<PerfDataHandler::Mapping> fake;
  // Kernel samples might take some extra work.
  if (context->main_mapping == nullptr &&
      header_context == quipper::AddressContext::kHostKernel) {
    auto comm_it = pid_to_comm_event_.find(pid);
    auto kernel_it = pid_to_executable_mmap_.find(kKernelPid);
    if (comm_it != pid_to_comm_event_.end()) {
      BuildId build_id("", kBuildIdMissing);
      if (kernel_it != pid_to_executable_mmap_.end()) {
        build_id.value = kernel_it->second->build_id.value;
        build_id.source = kBuildIdKernelPrefix;
      }
      // The comm_md5_prefix is used for the filename_md5_prefix field in the
      // fake mapping. This allows recovery of the process name (execname) by
      // resolving its md5 prefix when the comm string is nil or empty.
      context->main_mapping =
          GetOrAddFakeMapping(comm_it->second->comm(), build_id,
                              comm_it->second->comm_md5_prefix(), 0);
    } else if (pid == 0 && kernel_it != pid_to_executable_mmap_.end()) {
      // PID is 0 for the per-CPU idle tasks. Attribute these to the kernel.
      context->main_mapping = kernel_it->second;
    }
  }

  stat_.missing_main_mmap += context->main_mapping == nullptr;

  // Normalize the callchain.
  context->callchain.resize(sample.callchain_size());
  quipper::AddressContext callchain_context = quipper::AddressContext::kUnknown;
  for (int i = 0; i < sample.callchain_size(); ++i) {
    ++stat_.callchain_ips;
    uint64_t ip = sample.callchain(i);
    quipper::AddressContext current_context = quipper::ContextFromCallchain(
        static_cast<quipper::perf_callchain_context>(ip));

    const PerfDataHandler::Mapping* mapping;
    if (current_context != quipper::AddressContext::kUnknown) {
      // This callchain frame is actually a context marker.
      // Store the context for future frames, but don't give this one a mapping.
      callchain_context = current_context;
      mapping = nullptr;
      ++stat_.missing_callchain_mmap;
    } else {
      mapping = GetMappingFromPidAndIP(pid, ip, callchain_context);
    }
    context->callchain[i].ip = ip;
    context->callchain[i].mapping = mapping;
  }

  // Normalize the branch_stack.
  context->branch_stack.resize(sample.branch_stack_size());
  for (int i = 0; i < sample.branch_stack_size(); ++i) {
    stat_.branch_stack_ips += 2;
    const auto& entry = sample.branch_stack(i);
    // from
    context->branch_stack[i].from.ip = entry.from_ip();
    context->branch_stack[i].from.mapping = GetMappingFromPidAndIP(
        pid, entry.from_ip(), quipper::AddressContext::kUnknown);
    stat_.missing_branch_stack_mmap +=
        context->branch_stack[i].from.mapping == nullptr;
    // to
    context->branch_stack[i].to.ip = entry.to_ip();
    context->branch_stack[i].to.mapping = GetMappingFromPidAndIP(
        pid, entry.to_ip(), quipper::AddressContext::kUnknown);
    stat_.missing_branch_stack_mmap +=
        context->branch_stack[i].to.mapping == nullptr;
    context->branch_stack[i].mispredicted = entry.mispredicted();
    context->branch_stack[i].predicted = entry.predicted();
    context->branch_stack[i].in_transaction = entry.in_transaction();
    context->branch_stack[i].abort = entry.abort();
    context->branch_stack[i].cycles = entry.cycles();
    context->branch_stack[i].spec = entry.spec();
  }

  if (sample.has_cgroup()) {
    auto cgrp_it = cgroup_map_.find(sample.cgroup());
    if (cgrp_it != cgroup_map_.end()) {
      context->cgroup = &cgrp_it->second;
    }
  }
  handler_->Sample(*context);
}

const PerfDataHandler::Mapping* Normalizer::GetOrAddFakeMapping(
    const std::string& comm, const BuildId& build_id, uint64_t comm_md5_prefix,
    uint64_t start_addr) {
  FakeMappingKey key = {comm, build_id.value, comm_md5_prefix};
  auto it = fake_mappings_.find(key);
  if (it != fake_mappings_.end()) {
    return it->second;
  }
  owned_mappings_.emplace_back(new PerfDataHandler::Mapping(
      comm, build_id, start_addr, start_addr + 1, 0, comm_md5_prefix));
  return fake_mappings_.insert({key, owned_mappings_.back().get()})
      .first->second;
}

void Normalizer::HandleLost(
    const quipper::PerfDataProto::PerfEvent& event_proto) {
  quipper::PerfDataProto::SampleEvent sample;
  uint64_t num_lost = 0;

  // See the definition of this variable for details on how we process
  // lost_samples_event or lost_event.
  if (use_lost_sample_) {
    if (!event_proto.has_lost_samples_event()) {
      return;
    }
    num_lost = event_proto.lost_samples_event().num_lost();
    stat_.samples += num_lost;
    stat_.missing_main_mmap += num_lost;
    sample.set_id(event_proto.lost_samples_event().sample_info().id());
    sample.set_pid(event_proto.lost_samples_event().sample_info().pid());
    sample.set_tid(event_proto.lost_samples_event().sample_info().tid());
  } else {
    // As mentioned in the definition, for backward compatibility.
    if (!event_proto.has_lost_event()) {
      return;
    }
    num_lost = event_proto.lost_event().lost();
    stat_.samples += num_lost;
    stat_.missing_main_mmap += num_lost;
    sample.set_id(event_proto.lost_event().id());
    sample.set_pid(event_proto.lost_event().sample_info().pid());
    sample.set_tid(event_proto.lost_event().sample_info().tid());
  }

  int64_t event_index = GetEventIndexForSample(sample);
  if (event_index == -1) {
    ++stat_.no_event_errors;
    return;
  }

  // Use a special address and associated mapping for synthesized lost
  // samples, so we can differentiate them from actual unmapped samples, see
  // b/195154469. The chosen address avoids potential collisions with the
  // address space when quipper's do_remap option is set to true or false.
  // With remapping enabled, the address is guaranteed to be larger than the
  // mapped quipper space. Without remapping, the address is in a reserved
  // address range. Kernel addresses on x86 and ARM have the high 16 bits
  // set, while PowerPC has a reserved space from 0x1000000000000000 to
  // 0xBFFFFFFFFFFFFFFF. Quipper sets the highest byte of callchain unmapped
  // addresses to 0x8 to avoid spurious symbolization in the presence of
  // remapping. Here, we set the highest byte of the synthesized lost sample
  // addresses to 0x9, to avoid any collisions.
  sample.set_ip(9ULL << 60);
  quipper::PerfDataProto::EventHeader header;
  PerfDataHandler::SampleContext context(header, sample);
  context.file_attrs_index = event_index;
  context.sample_mapping =
      GetOrAddFakeMapping(kLostMappingFilename, BuildId("", kBuildIdMissing),
                          kLostMd5Prefix, sample.ip());
  for (uint64_t i = 0; i < num_lost; ++i) {
    handler_->Sample(context);
  }
  stat_.synthesized_lost_samples += num_lost;
}

static void CheckStat(int64_t num, int64_t denom, const std::string& desc) {
  const int max_missing_pct = 1;
  if (denom > 0 && num * 100 / denom > max_missing_pct) {
    LOG(WARNING) << "stat: " << desc << " " << num << "/" << denom;
  }
}

void Normalizer::LogStats() {
  CheckStat(stat_.missing_main_mmap, stat_.samples, "missing_main_mmap");
  CheckStat(stat_.missing_sample_mmap, stat_.samples, "missing_sample_mmap");
  CheckStat(stat_.synthesized_lost_samples, stat_.samples,
            "synthesized lost samples");
  CheckStat(stat_.missing_addr_mmap, stat_.samples_with_addr,
            "missing_addr_mmap");
  CheckStat(stat_.missing_callchain_mmap, stat_.callchain_ips,
            "missing_callchain_mmap");
  CheckStat(stat_.missing_branch_stack_mmap, stat_.branch_stack_ips,
            "missing_branch_stack_mmap");
  CheckStat(stat_.missing_pid, stat_.samples, "missing_pid");
  CheckStat(stat_.no_event_errors, 1, "unknown event id");
}

// IsSameBuildId returns true iff build ID is a prefix of the other AND the rest
// of characters are all 0s.
static bool IsSameBuildId(const std::string& build_id1,
                          const std::string& build_id2) {
  auto is_same = [](const std::string& a, const std::string& b) {
    return HasPrefixString(a, b.c_str()) &&
           a.substr(a.find(b) + b.length()).find_first_not_of('0') ==
               std::string::npos;
  };
  return is_same(build_id1, build_id2) || is_same(build_id2, build_id1);
}

BuildId Normalizer::GetBuildId(const quipper::PerfDataProto_MMapEvent* mmap) {
  std::string filename = PerfDataHandler::NameOrMd5Prefix(
      mmap->filename(), mmap->filename_md5_prefix());
  auto it = filename_to_build_id_.find(filename);
  BuildId build_id_from_filename = it != filename_to_build_id_.end()
                                       ? it->second
                                       : BuildId("", kBuildIdMissing);

  std::string build_id_from_mmap = mmap->has_build_id() ? mmap->build_id() : "";

  if (!build_id_from_mmap.empty()) {
    return IsSameBuildId(build_id_from_filename.value, build_id_from_mmap)
               ? BuildId(build_id_from_mmap, kBuildIdMmapSameFilename)
               : BuildId(build_id_from_mmap, kBuildIdMmapDiffFilename);
  } else if (!build_id_from_filename.value.empty()) {
    return build_id_from_filename;
  }
  // else, both build_id_from_mmap and build_id_from_filename.value are empty.

  if (HasPrefixString(filename, kKernelPrefix)) {
    // The build ID of a kernel non-module filename with its
    // quipper::PERF_RECORD_MISC_CPUMODE_MASK misc bits set to
    // quipper::PERF_RECORD_MISC_KERNEL is used as the kernel build id when no
    // build id was found for the filename |kKernelPrefix|. E.g. Build Id of the
    // filename /usr/lib/debug/boot/vmlinux-4.16.0-1-amd64 as referenced in the
    // github issue(https://github.com/google/perf_data_converter/issues/36),
    // will be used when [kernel.kallsyms] has no buildid.
    return BuildId(maybe_kernel_build_id_, kBuildIdKernelPrefix);
  }

  return {"", kBuildIdMissing};
}

static bool IsVirtualMapping(const std::string& map_name) {
  return HasPrefixString(map_name, "//") ||
         (HasPrefixString(map_name, "[") && HasSuffixString(map_name, "]")) ||
         HasPrefixString(map_name, "/memfd:") ||
         HasPrefixString(map_name, "[anon:");
}

void Normalizer::ConvertMmapFromKsymbol(
    const quipper::PerfDataProto_KsymbolEvent& ksymbol_event, uint32_t prot,
    quipper::PerfDataProto_MMapEvent* mmap) {
  uint32_t pid = 1;
  uint32_t tid = 1;
  uint32_t flags = 0;
  uint64_t pgoff = 0;
  mmap->set_pid(pid);
  mmap->set_tid(tid);
  mmap->set_flags(flags);
  mmap->set_pgoff(pgoff);
  mmap->set_prot(prot);
  mmap->set_start(ksymbol_event.addr());
  mmap->set_len(ksymbol_event.len());
  // TODO(go/gwp-bpf-name-breakdown): Need to do post-processing on `filename`.
  mmap->set_filename(ksymbol_event.name());
  *mmap->mutable_sample_info() = ksymbol_event.sample_info();
}

void Normalizer::UpdateMapsWithMMapEvent(
    const quipper::PerfDataProto_MMapEvent* mmap) {
  if (mmap->len() == 0) {
    LOG(WARNING) << "bogus mapping: " << mmap->filename();
    return;
  }
  uint32_t pid = mmap->pid();
  MMapIntervalMap* interval_map = nullptr;
  const auto& it = pid_to_mmaps_.find(pid);
  if (it == pid_to_mmaps_.end()) {
    interval_map = new MMapIntervalMap;
    pid_to_mmaps_[pid] = std::unique_ptr<MMapIntervalMap>(interval_map);
  } else {
    interval_map = it->second.get();
  }

  PerfDataHandler::Mapping* mapping = new PerfDataHandler::Mapping(
      mmap->filename(), GetBuildId(mmap), mmap->start(),
      mmap->start() + mmap->len(), mmap->pgoff(), mmap->filename_md5_prefix());
  owned_mappings_.emplace_back(mapping);
  if (mapping->start <= (static_cast<uint64_t>(1) << 63) &&
      mapping->file_offset > (static_cast<uint64_t>(1) << 63) &&
      mapping->limit > (static_cast<uint64_t>(1) << 63)) {
    // Prior to the fix (https://lore.kernel.org/patchwork/patch/473712/), perf
    // synthesizes the kernel start using the address of the first symbol, found
    // in the `/proc/kallsyms` file, that doesn't contain the `[` character.
    // This address is usually 0. This causes the kernel mapping to subsume the
    // entire user space mapping. Copy the offset value to start.

    // file_offset here actually refers to the address of the text or _stext
    // kernel symbol and might have to be page aligned.
    mapping->start = mapping->file_offset - mapping->file_offset % 4096;
  }

  interval_map->Set(mapping->start, mapping->limit, mapping);
  // Pass the final mapping through to the subclass also.
  PerfDataHandler::MMapContext mmap_context;
  mmap_context.pid = pid;
  mmap_context.mapping = mapping;
  handler_->MMap(mmap_context);

  // Main executables are usually loaded at 0x8048000 or 0x400000.
  // If we ever see an MMAP starting at one of those locations, that should be
  // our guess.
  // This is true even if the old MMAP started at one of the locations, because
  // the pid may have been recycled since then (so newer is better).
  if (mapping->start == 0x8048000 || mapping->start == 0x400000) {
    pid_to_executable_mmap_[pid] = mapping;
    return;
  }
  // Figure out whether this MMAP is the main executable.
  // If there have been no previous MMAPs for this pid, then this MMAP is our
  // best guess.
  auto old_mapping_it = pid_to_executable_mmap_.find(pid);
  PerfDataHandler::Mapping* old_mapping =
      old_mapping_it == pid_to_executable_mmap_.end() ? nullptr
                                                      : old_mapping_it->second;

  if (old_mapping != nullptr && old_mapping->start == 0x400000 &&
      old_mapping->filename.empty() &&
      mapping->start - mapping->file_offset == 0x400000) {
    // Hugepages remap the main binary, but the original mapping loses
    // its name, so we have this hack.
    old_mapping->filename = mmap->filename();
  }

  if (old_mapping == nullptr && !HasSuffixString(mmap->filename(), ".ko") &&
      !HasSuffixString(mmap->filename(), ".so") &&
      !IsDeletedSharedObject(mmap->filename()) &&
      !IsVersionedSharedObject(mmap->filename()) &&
      !IsVirtualMapping(mmap->filename()) &&
      // Java runtime shared class image ("classes.jsa") may be mapped into the
      // program address space early. Ignore it when determining the logical
      // name of the process since "classes.jsa" is not useful as the name.
      // See b/152911108 for more context.
      !HasSuffixString(mmap->filename(), "/classes.jsa") &&
      !HasPrefixString(mmap->filename(), kKernelPrefix)) {
    if (!HasPrefixString(mmap->filename(), "/usr/bin/") &&
        !HasPrefixString(mmap->filename(), "/usr/sbin/") &&
        !HasPrefixString(mmap->filename(), "/bin/") &&
        !HasPrefixString(mmap->filename(), "/sbin/") &&
        !HasPrefixString(mmap->filename(), "/usr/local/bin/") &&
        !HasPrefixString(mmap->filename(), "/usr/local/sbin/") &&
        !HasPrefixString(mmap->filename(), "/usr/libexec/") &&
        !HasSuffixString(mmap->filename(), "/sel_ldr/")) {
      LOG(INFO) << "Guessing main mapping for PID=" << pid << " "
                << mmap->filename();
    }
    pid_to_executable_mmap_[pid] = mapping;
    return;
  }

  if (pid == kKernelPid && HasPrefixString(mmap->filename(), kKernelPrefix)) {
    pid_to_executable_mmap_[pid] = mapping;
  }
}

const PerfDataHandler::Mapping* Normalizer::TryLookupInPid(uint32_t pid,
                                                           uint64_t ip) const {
  const auto& it = pid_to_mmaps_.find(pid);
  if (it == pid_to_mmaps_.end()) {
    VLOG(2) << "No mmaps for pid " << pid;
    return nullptr;
  }
  MMapIntervalMap* mmaps = it->second.get();

  const PerfDataHandler::Mapping* mapping = nullptr;
  mmaps->Lookup(ip, &mapping);
  return mapping;
}

// Find the mapping for ip in the context of pid and context.  We might be
// looking at a kernel IP, however (which can show up in any pid, and are
// stored in our map as pid = -1), so check there if the lookup fails
// in our process.
const PerfDataHandler::Mapping* Normalizer::GetMappingFromPidAndIP(
    uint32_t pid, uint64_t ip, quipper::AddressContext context) const {
  if (ip >> 60 == 0x8) {
    // In case the highest 4 bits of ip is 1000, it has a null mapping. See
    // the comment mentioning "highest 4 bits" in perf_parser.cc for details.
    return nullptr;
  }
  if (context == quipper::AddressContext::kGuestKernel ||
      context == quipper::AddressContext::kGuestUser ||
      context == quipper::AddressContext::kHypervisor) {
    // Addresses from these contexts are meaningless in the host's context.
    // If they are assigned a mapping, they would be symbolized against that
    // mapping, which would be incorrect and confusing.
    return nullptr;
  }
  // First look up the mapping for the ip in the address space of the given pid.
  // If no mapping is found, then try to find in the kernel space, with pid of
  // -1. However, if the ip is guaranteed to be in user context, it will not be
  // looked up in the kernel space.
  const PerfDataHandler::Mapping* mapping = TryLookupInPid(pid, ip);
  if (mapping == nullptr && context != quipper::AddressContext::kHostUser) {
    mapping = TryLookupInPid(-1, ip);
  }
  if (mapping == nullptr) {
    VLOG(2) << "no sample mmap found for pid " << pid << " and ip " << ip;
    return nullptr;
  }
  CHECK_GE(ip, mapping->start);
  CHECK_LT(ip, mapping->limit);
  return mapping;
}

const PerfDataHandler::Mapping* Normalizer::GetMainMMapFromPid(
    uint32_t pid) const {
  auto mapping_it = pid_to_executable_mmap_.find(pid);
  if (mapping_it != pid_to_executable_mmap_.end()) {
    return mapping_it->second;
  }

  VLOG(2) << "No argv0 name found for sample with pid: " << pid;
  return nullptr;
}

int64_t Normalizer::GetEventIndexForSample(
    const quipper::PerfDataProto_SampleEvent& sample) const {
  if (perf_proto_.file_attrs().size() == 1) {
    return 0;
  }

  if (!sample.has_id()) {
    LOG(ERROR) << "Perf sample did not have id";
    return -1;
  }

  auto it = id_to_event_index_.find(sample.id());
  if (it == id_to_event_index_.end()) {
    LOG(ERROR) << "Incorrect event id: " << sample.id();
    return -1;
  }
  return it->second;
}

void Normalizer::HandleSpeAuxtrace(
    const quipper::PerfDataProto::PerfEvent& event_proto) {
  const quipper::PerfDataProto::AuxtraceEvent& auxtrace_event =
      event_proto.auxtrace_event();
  if (!auxtrace_event.has_trace_data()) {
    return;
  }

  quipper::ArmSpeDecoder::Record record;
  quipper::ArmSpeDecoder decoder(auxtrace_event.trace_data(), false);
  while (decoder.NextRecord(&record)) {
    // Synthesize a perf data sample with from the SPE record.
    uint32_t tid = record.context.id;
    uint32_t pid = 0;
    if (tid != 0) {
      auto pid_it = tid_to_pid_.find(tid);
      if (pid_it == tid_to_pid_.end()) {
        stat_.missing_pid++;
        LOG(WARNING) << "tid->pid mapping does not contain tid " << tid;
      } else {
        pid = pid_it->second;
      }
    }

    quipper::PerfDataProto::PerfEvent event_proto;
    auto& sample = *event_proto.mutable_sample_event();
    sample.set_tid(tid);
    sample.set_pid(pid);
    sample.set_ip(record.ip.addr);

    PerfDataHandler::SampleContext context(event_proto.header(), sample);
    context.spe.is_spe = true;
    context.spe.record = record;
    HandleSample(&context);
  }
}

void Normalizer::HandleKsymbol(
    const quipper::PerfDataProto::PerfEvent& event_proto) {
  quipper::PerfDataProto_MMapEvent mmap;
  if (event_proto.ksymbol_event().ksym_type() ==
          quipper::PERF_RECORD_KSYMBOL_TYPE_BPF &&
      event_proto.ksymbol_event().flags() == 0) {
    uint32_t prot = PROT_EXEC | PROT_READ;
    ConvertMmapFromKsymbol(event_proto.ksymbol_event(), prot, &mmap);
    UpdateMapsWithMMapEvent(&mmap);
  }
}

}  // namespace

// Finds needle in haystack starting at cursor. It then returns the index
// directly after needle or std::string::npos if needle was not found.
size_t FindAfter(const std::string& haystack, const std::string& needle,
                 size_t cursor) {
  auto next_cursor = haystack.find(needle, cursor);
  if (next_cursor != std::string::npos) {
    next_cursor += needle.size();
  }
  return next_cursor;
}

bool IsDeletedSharedObject(const std::string& path) {
  size_t cursor = 1;
  while ((cursor = FindAfter(path, ".so", cursor)) != std::string::npos) {
    const auto ch = path.at(cursor);
    if (ch == '.' || ch == '_' || ch == ' ') {
      return path.find("(deleted)", cursor) != std::string::npos;
    }
  }
  return false;
}

bool IsVersionedSharedObject(const std::string& path) {
  return path.find(".so.", 1) != std::string::npos;
}

PerfDataHandler::PerfDataHandler() {}

void PerfDataHandler::Process(const quipper::PerfDataProto& perf_proto,
                              PerfDataHandler* handler) {
  Normalizer Normalizer(perf_proto, handler);
  return Normalizer.Normalize();
}

std::string PerfDataHandler::NameOrMd5Prefix(std::string name,
                                             uint64_t md5_prefix) {
  if (name.empty()) {
    std::stringstream ss;
    ss << std::hex << md5_prefix;
    name = ss.str();
  }
  return name;
}

std::string PerfDataHandler::MappingFilename(const Mapping* m) {
  return NameOrMd5Prefix(m->filename, m->filename_md5_prefix);
}

void PerfDataHandler::IncBuildIdStats(uint32_t pid,
                                      const PerfDataHandler::Mapping* mapping) {
  BuildIdSource source =
      mapping != nullptr ? mapping->build_id.source : kBuildIdNoMmap;
  process_build_id_stats_[pid][source]++;
}

}  // namespace perftools
