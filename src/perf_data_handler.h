/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PERFTOOLS_PERF_DATA_HANDLER_H_
#define PERFTOOLS_PERF_DATA_HANDLER_H_

#include <unordered_map>
#include <vector>

#include "src/quipper/arm_spe_decoder.h"
#include "src/quipper/perf_data.pb.h"

namespace perftools {

// The source of a build ID. It is supposed to be used in conjunction with the
// build ID to label how the build ID is derived.
enum BuildIdSource {
  kUnknown = 0,
  // The build ID is from the buildid-mmap event, and its value is the same from
  // the filename_to_build_id_ of Normalizer. This is expected to be the common
  // case.
  kBuildIdMmapSameFilename = 1,
  // The build ID is from the buildid-mmap event, and its value is
  // different from the filename_to_build_id_ of Normalizer. This
  // indicates that the item suffers from build-ID-mismatch.
  kBuildIdMmapDiffFilename = 2,
  // The build ID is from the filename_to_build_id_ of Normalizer. This
  // means that the build ID is missing in the buildid-mmap events.
  kBuildIdFilename = 3,
  // Same as kBuildIdFilename, except the build ID event is not from the
  // perf.data but injected from a filename->build ID map as a parameter for
  // perf_data_converter.
  kBuildIdFilenameInjected = 4,
  // Same as kBuildIdFilename, except the filename -> build ID entry was
  // overwritten by different build ID values (e.g., there exists multiple build
  // ID events having the same file name but different build IDs).
  kBuildIdFilenameAmbiguous = 5,
  // The build ID is from a pre-defined kernel prefix. This means that
  // the build ID is missing in the buildid-mmap events.
  kBuildIdKernelPrefix = 6,
  // The build ID cannot be found for the corresponding mmap
  // event of this item.
  kBuildIdMissing = 7,
  // There is no mmap event found for this item.
  kBuildIdNoMmap = 8,
};

typedef std::unordered_map<BuildIdSource, int64_t> BuildIdStats;

// The wrapper class to wrap a build ID string with a source label.
struct BuildId {
 public:
  BuildId(std::string v, BuildIdSource s) : value(v), source(s) {}
  BuildId() : value(""), source(kUnknown) {}

  std::string value;
  BuildIdSource source;
};

// PerfDataHandler defines an interface for processing PerfDataProto
// with normalized sample fields (i.e., materializing mappings,
// filenames, and build-ids).
//
// To use, subclass PerfDataHandler and implement the required
// methods, then call Process() and handler will be called for every
// SAMPLE event.
//
// Context events' pointers to Mappings will be constant for the lifetime of a
// process, so subclasses may use the pointer values as a key to various caches
// they may want to maintain as part of the output data creation.
class PerfDataHandler {
 public:
  struct Mapping {
   public:
    Mapping(const std::string& filename, const BuildId& build_id,
            uint64_t start, uint64_t limit, uint64_t file_offset,
            uint64_t filename_md5_prefix)
        : filename(filename),
          build_id(build_id.value, build_id.source),
          start(start),
          limit(limit),
          file_offset(file_offset),
          filename_md5_prefix(filename_md5_prefix) {}

    std::string filename;  // Empty if missing.
    BuildId build_id;      // build_id.value is empty if missing
    uint64_t start;
    uint64_t limit;  // limit=ceiling.
    uint64_t file_offset;
    uint64_t filename_md5_prefix;

   private:
    Mapping() {}
  };

  struct Location {
    Location() : ip(0), mapping(nullptr) {}

    uint64_t ip;
    const Mapping* mapping;
  };

  struct BranchStackPair {
    BranchStackPair()
        : mispredicted(false),
          predicted(false),
          in_transaction(false),
          abort(false),
          cycles(0),
          spec(0) {}

    Location from;
    Location to;
    // Branch target was mispredicted.
    bool mispredicted;
    // Branch target was predicted.
    bool predicted;
    // Indicates running in a hardware transaction.
    bool in_transaction;
    // Indicates aborting a hardware transaction.
    bool abort;
    // The cycles from last taken branch (LBR).
    uint32_t cycles;
    // Branch speculation outcome classification if supported.
    uint32_t spec;
  };

  struct SampleContext {
    SampleContext(const quipper::PerfDataProto::EventHeader& h,
                  const quipper::PerfDataProto::SampleEvent& s)
        : header(h),
          sample(s),
          main_mapping(nullptr),
          sample_mapping(nullptr),
          addr_mapping(nullptr),
          file_attrs_index(-1),
          cgroup(nullptr),
          spe{false, quipper::ArmSpeDecoder::Record()} {}

    // The event's header.
    const quipper::PerfDataProto::EventHeader& header;
    // An event.
    const quipper::PerfDataProto::SampleEvent& sample;
    // The mapping for the main binary for this program.
    const Mapping* main_mapping;
    // The mapping in which event.ip is found.
    const Mapping* sample_mapping;
    // The mapping in which event.addr is found.
    const Mapping* addr_mapping;
    // Locations corresponding to event.callchain.
    std::vector<Location> callchain;
    // Locations corresponding to entries in event.branch_stack.
    std::vector<BranchStackPair> branch_stack;
    // An index into PerfDataProto.file_attrs or -1 if
    // unavailable.
    int64_t file_attrs_index;
    // Cgroup pathname
    const std::string* cgroup;
    // The attributes for the sample if it comes from Arm SPE.
    struct {
      bool is_spe;
      quipper::ArmSpeDecoder::Record record;
    } spe;
  };

  struct CommContext {
    // A comm event.
    const quipper::PerfDataProto::CommEvent* comm;
    // Whether the comm event happens due to exec().
    bool is_exec = false;
  };

  struct MMapContext {
    // A memory mapping to be passed to the subclass. Should be the same mapping
    // that gets added to pid_to_mmaps_.
    const PerfDataHandler::Mapping* mapping;
    // The process id used as a key to pid_to_mmaps_.
    uint32_t pid;
  };

  PerfDataHandler(const PerfDataHandler&) = delete;
  PerfDataHandler& operator=(const PerfDataHandler&) = delete;

  // Process initiates processing of perf_proto.  handler.Sample will
  // be called for every event in the profile.
  static void Process(const quipper::PerfDataProto& perf_proto,
                      PerfDataHandler* handler);

  // Returns name string if it's non empty or hex string of md5_prefix.
  static std::string NameOrMd5Prefix(std::string name, uint64_t md5_prefix);

  // Returns the file name of the mapping as either the real file path if it's
  // present or the string representation of the file path MD5 checksum prefix
  // when the real file path was stripped from the data for privacy reasons.
  static std::string MappingFilename(const Mapping* m);

  virtual ~PerfDataHandler() {}

  // Implement these callbacks:
  // Called for every sample.
  virtual void Sample(const SampleContext& sample) = 0;
  // When comm.pid()==comm.tid() it indicates an exec() happened.
  virtual void Comm(const CommContext& comm) = 0;
  // Called for every mmap event.
  virtual void MMap(const MMapContext& mmap) = 0;

 protected:
  PerfDataHandler();

  // The build ID source count of each process; key by process ID.
  std::unordered_map<uint32_t, BuildIdStats> process_build_id_stats_;

  // Increments the counter of the corresponding Build ID Stat of the process
  // according to the given mapping.
  void IncBuildIdStats(uint32_t pid, const PerfDataHandler::Mapping* mapping);
};

}  // namespace perftools

#endif  // PERFTOOLS_PERF_DATA_HANDLER_H_
