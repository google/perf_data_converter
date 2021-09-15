// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_PERF_READER_H_
#define CHROMIUMOS_WIDE_PROFILING_PERF_READER_H_

#include <stdint.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "base/macros.h"
#include "compat/proto.h"
#include "compat/string.h"
#include "kernel/perf_event.h"
#include "perf_serializer.h"
#include "sample_info_reader.h"

namespace quipper {

// Based on code in tools/perf/util/header.c, the metadata are of the following
// formats:

typedef u32 num_siblings_type;

class DataReader;
class DataWriter;

struct PerfFileAttr;

class PerfReader {
 public:
  PerfReader();
  ~PerfReader();

  // Copy stored contents to |*perf_data_proto|. Appends a timestamp. Returns
  // true on success.
  bool Serialize(PerfDataProto* perf_data_proto) const;
  // Read in contents from a protobuf. Returns true on success.
  bool Deserialize(const PerfDataProto& perf_data_proto);

  bool ReadFile(const std::string& filename);
  bool ReadFromVector(const std::vector<char>& data);
  bool ReadFromString(const std::string& str);
  bool ReadFromPointer(const char* data, size_t size);
  bool ReadFromData(DataReader* data);

  bool WriteFile(const std::string& filename);
  bool WriteToVector(std::vector<char>* data);
  bool WriteToString(std::string* str);
  bool WriteToPointer(char* buffer, size_t size);

  // Stores the mapping from filenames to build ids in build_id_events_.
  // Returns true on success.
  // Note: If |filenames_to_build_ids| contains a mapping for a filename for
  // which there is already a build_id_event in build_id_events_, a duplicate
  // build_id_event will be created, and the old build_id_event will NOT be
  // deleted.
  bool InjectBuildIDs(
      const std::map<std::string, std::string>& filenames_to_build_ids);

  // Replaces existing filenames with filenames from |build_ids_to_filenames|
  // by joining on build ids.  If a build id in |build_ids_to_filenames| is not
  // present in this parser, it is ignored.
  bool Localize(
      const std::map<std::string, std::string>& build_ids_to_filenames);

  // Same as Localize, but joins on filenames instead of build ids.
  bool LocalizeUsingFilenames(
      const std::map<std::string, std::string>& filename_map);

  // Stores a list of unique filenames found in MMAP/MMAP2 events into
  // |filenames|.  Any existing data in |filenames| will be lost.
  void GetFilenames(std::vector<std::string>* filenames) const;
  void GetFilenamesAsSet(std::set<std::string>* filenames) const;

  // Uses build id events to populate |filenames_to_build_ids|.
  // Any existing data in |filenames_to_build_ids| will be lost.
  // Note:  A filename returned by GetFilenames need not be present in this map,
  // since there may be no build id event corresponding to the MMAP/MMAP2.
  void GetFilenamesToBuildIDs(
      std::map<std::string, std::string>* filenames_to_build_ids) const;

  // Sort all events in |proto_| by timestamps if they are available. Otherwise
  // event order is unchanged.
  void MaybeSortEventsByTime();

  // Accessors and mutators.

  // This is a plain accessor for the internal protobuf storage. It is meant for
  // exposing the internals. This is not initialized until Read*() or
  // Deserialize() has been called.
  //
  // Call Serialize() instead of this function to acquire an "official" protobuf
  // with a timestamp.
  const PerfDataProto& proto() const { return *proto_; }

  const RepeatedPtrField<PerfDataProto_PerfFileAttr>& attrs() const {
    return proto_->file_attrs();
  }
  const RepeatedPtrField<PerfDataProto_PerfEventType>& event_types() const {
    return proto_->event_types();
  }

  const RepeatedPtrField<PerfDataProto_PerfEvent>& events() const {
    return proto_->events();
  }
  // WARNING: Modifications to the protobuf events may change the amount of
  // space required to store the corresponding raw event. If that happens, the
  // caller is responsible for correctly updating the size in the event header.
  RepeatedPtrField<PerfDataProto_PerfEvent>* mutable_events() {
    return proto_->mutable_events();
  }

  const RepeatedPtrField<PerfDataProto_PerfBuildID>& build_ids() const {
    return proto_->build_ids();
  }
  RepeatedPtrField<PerfDataProto_PerfBuildID>* mutable_build_ids() {
    return proto_->mutable_build_ids();
  }

  const std::string& tracing_data() const {
    return proto_->tracing_data().tracing_data();
  }

  const PerfDataProto_StringMetadata& string_metadata() const {
    return proto_->string_metadata();
  }

  uint64_t metadata_mask() const { return proto_->metadata_mask().Get(0); }

  const std::unordered_set<u32>& event_types_to_skip_when_serializing() const {
    return event_types_to_skip_when_serializing_;
  }

  // Updates the set of event types which will not be serialized in the output
  // proto.
  void SetEventTypesToSkipWhenSerializing(
      std::unordered_set<u32> event_types_to_skip_when_serializing) {
    event_types_to_skip_when_serializing_ =
        std::move(event_types_to_skip_when_serializing);
  }

  // Sets the callback to be called for each sample event in the perf data file.
  // It will be called even if PERF_RECORD_SAMPLE is in the set of event types
  // passed to |SetEventTypesToSkipWhenSerializing|. This is useful when SAMPLE
  // events can be processed in a streaming fashion, which uses much less memory
  // than storing them all in the final proto.
  void SetSampleCallback(
      std::function<void(const PerfDataProto_SampleEvent&)> callback) {
    sample_event_callback_ = callback;
  }

 private:
  bool ReadHeader(DataReader* data);
  bool ReadAttrsSection(DataReader* data);
  bool ReadAttr(DataReader* data);
  bool ReadEventAttr(DataReader* data, perf_event_attr* attr);
  bool ReadUniqueIDs(DataReader* data, size_t num_ids, std::vector<u64>* ids);

  bool ReadEventTypesSection(DataReader* data);
  // if event_size == 0, then not in an event.
  bool ReadEventType(DataReader* data, int attr_idx, size_t event_size);

  bool ReadDataSection(DataReader* data);

  // Reads the event data of non-header events from both file and pipe mode
  // perf outputs. Returns true on success. Otherwise, returns false. On
  // success, updates the |read_size| with the size of the read non-header event
  // data including any data following the actual event not including the size
  // of the header.
  bool ReadNonHeaderEventDataWithoutHeader(DataReader* data,
                                           const perf_event_header& header,
                                           size_t* read_size);

  // Reads metadata in normal mode.
  bool ReadMetadata(DataReader* data);

  // Reads metadata without header. Useful for reading metadata in piped mode,
  // where the header must be read first in order to determine that it is a
  // header feature event (similar to metadata event in file mode) and the type
  // of metadata.
  bool ReadMetadataWithoutHeader(DataReader* data, u32 type, size_t size);

  // The following functions read various types of metadata.
  bool ReadTracingMetadata(DataReader* data, size_t size);
  bool ReadBuildIDMetadata(DataReader* data, size_t size);
  // Reads contents of a build ID event or block beyond the header. Useful for
  // reading build IDs in piped mode, where the header must be read first in
  // order to determine that it is a build ID event.
  bool ReadBuildIDMetadataWithoutHeader(DataReader* data,
                                        const perf_event_header& header);

  // Reads and serializes trace data following PERF_RECORD_AUXTRACE event.
  bool ReadAuxtraceTraceData(DataReader* data,
                             PerfDataProto_PerfEvent* proto_event);

  // Reads a singular string metadata field (with preceding size field) from
  // |data| and writes the string and its Md5sum prefix into |dest|.
  bool ReadSingleStringMetadata(
      DataReader* data, size_t max_readable_size,
      PerfDataProto_StringMetadata_StringAndMd5sumPrefix* dest) const;
  // Reads a string metadata with multiple string fields (each with preceding
  // size field) from |data|. Writes each string field and its Md5sum prefix
  // into |dest_array|. Writes the combined string fields (joined into one
  // string into |dest_single|.
  bool ReadRepeatedStringMetadata(
      DataReader* data, size_t max_readable_size,
      RepeatedPtrField<PerfDataProto_StringMetadata_StringAndMd5sumPrefix>*
          dest_array,
      PerfDataProto_StringMetadata_StringAndMd5sumPrefix* dest_single) const;

  bool ReadUint32Metadata(DataReader* data, u32 type, size_t size);
  bool ReadUint64Metadata(DataReader* data, u32 type, size_t size);
  bool ReadCPUTopologyMetadata(DataReader* data, size_t size);
  bool ReadNUMATopologyMetadata(DataReader* data);
  bool ReadPMUMappingsMetadata(DataReader* data, size_t size);
  bool ReadGroupDescMetadata(DataReader* data);
  bool ReadEventDescMetadata(DataReader* data);

  // Read perf data from file perf output data.
  bool ReadFileData(DataReader* data);

  // Read perf data from piped perf output data.
  bool ReadPipedData(DataReader* data);

  // Processes the remaining piped-mode header events, introduced after
  // perf-4.13.
  bool ProcessPipedModeHeaderEvents(DataReader* data,
                                    const perf_event_header& header,
                                    size_t size_without_header);

  // Returns the size in bytes that would be written by any of the methods that
  // write the entire perf data file (WriteFile, WriteToPointer, etc).
  size_t GetSize() const;

  // Populates |*header| with the proper contents based on the perf data that
  // has been read.
  void GenerateHeader(struct perf_file_header* header) const;

  // Like WriteToPointer, but does not check if the buffer is large enough.
  bool WriteToPointerWithoutCheckingSize(char* buffer, size_t size);

  bool WriteHeader(const struct perf_file_header& header,
                   DataWriter* data) const;
  bool WriteAttrs(const struct perf_file_header& header,
                  DataWriter* data) const;
  bool WriteData(const struct perf_file_header& header, DataWriter* data) const;
  bool WriteMetadata(const struct perf_file_header& header,
                     DataWriter* data) const;

  // For writing the various types of metadata.
  bool WriteBuildIDMetadata(u32 type, DataWriter* data) const;
  bool WriteSingleStringMetadata(
      const PerfDataProto_StringMetadata_StringAndMd5sumPrefix& src,
      DataWriter* data) const;
  bool WriteRepeatedStringMetadata(
      const RepeatedPtrField<
          PerfDataProto_StringMetadata_StringAndMd5sumPrefix>& src_array,
      DataWriter* data) const;
  bool WriteUint32Metadata(u32 type, DataWriter* data) const;
  bool WriteUint64Metadata(u32 type, DataWriter* data) const;
  bool WriteEventDescMetadata(DataWriter* data) const;
  bool WriteCPUTopologyMetadata(DataWriter* data) const;
  bool WriteNUMATopologyMetadata(DataWriter* data) const;
  bool WritePMUMappingsMetadata(DataWriter* data) const;
  bool WriteGroupDescMetadata(DataWriter* data) const;

  // For reading event blocks within piped perf data.
  bool ReadAttrEventBlock(DataReader* data, size_t size);

  // Reads PERF_RECORD_HEADER_FEATURE within piped perf data.
  bool ReadHeaderFeature(DataReader* data, const perf_event_header& header);

  // Returns the number of types of metadata stored and written to output data.
  size_t GetNumSupportedMetadata() const;

  // For computing the sizes of the various types of metadata.
  size_t GetBuildIDMetadataSize() const;
  size_t GetStringMetadataSize() const;
  size_t GetUint32MetadataSize() const;
  size_t GetUint64MetadataSize() const;
  size_t GetEventDescMetadataSize() const;
  size_t GetCPUTopologyMetadataSize() const;
  size_t GetNUMATopologyMetadataSize() const;
  size_t GetPMUMappingsMetadataSize() const;
  size_t GetGroupDescMetadataSize() const;

  // Returns true if we should write the number of strings for the string
  // metadata of type |type|.
  bool NeedsNumberOfStringData(u32 type) const;

  // Replaces existing filenames in MMAP/MMAP2 events based on |filename_map|.
  // This method does not change |build_id_events_|.
  bool LocalizeMMapFilenames(
      const std::map<std::string, std::string>& filename_map);

  // Stores a PerfFileAttr in |proto_|, creates a sample info reader from
  // |attr|, and updates |serializer_|. Returns true on success. Returns false
  // when the event ID position in the events linked to the |attr| are
  // inconsistent with the rest of the events.
  bool AddPerfFileAttr(const PerfFileAttr& attr);

  bool get_metadata_mask_bit(uint32_t bit) const {
    return metadata_mask() & (1 << bit);
  }
  void set_metadata_mask_bit(uint32_t bit) {
    proto_->set_metadata_mask(0, metadata_mask() | (1 << bit));
  }

  // Calculates and sets the correct event size in each event that has
  // event.header.size == 0. Returns true on success. Returns false when the
  // proto contains an unsupported perf event.
  bool PopulateMissingEventSize();

  // The file header is either a normal header or a piped header.
  union {
    struct perf_file_header header_;
    struct perf_pipe_file_header piped_header_;
  };

  // Store the perf data as a protobuf.
  Arena arena_;
  PerfDataProto* proto_;

  // Attribute ids that have been added to |proto_|. PerfFileAttr is generated
  // in PERF_RECORD_HEADER_ATTR, PERF_RECORD_HEADER_EVENT_TYPE, and
  // HEADER_EVENT_DESC. file_attrs_seen_ is used to deduplicate PerfFileAttr
  // read from these records.
  std::unordered_set<u64> file_attrs_seen_;

  // Attribute configs that have been added to |proto_|. PERF_RECORD_HEADER_ATTR
  // generated with perf-4.14 doesn't contain IDs for some perf events. In such
  // cases, PerfFileAttr could be deduplicated using PerfFileAttr.config if the
  // config is previously seen.
  std::unordered_set<u64> file_attr_configs_seen_;

  // Whether the incoming data is from a machine with a different endianness. We
  // got rid of this flag in the past but now we need to store this so it can be
  // passed to |serializer_|.
  bool is_cross_endian_;

  // For serializing individual events.
  PerfSerializer serializer_;

  // When writing to a new perf data file, this is used to hold the generated
  // file header, which may differ from the input file header, if any.
  struct perf_file_header out_header_;

  // For build-id embedded in MMAP2 records
  std::unordered_set<std::string> filenames_with_build_id_;

  // Which event types should be skipped when serializing to the output proto,
  // e.g. PERF_RECORD_SAMPLE, PERF_RECORD_COMM, etc.
  std::unordered_set<u32> event_types_to_skip_when_serializing_;

  // Callback to be called for each sample event in the perf data file if set,
  // even if PERF_RECORD_SAMPLE is in |event_types_to_skip_when_serializing|.
  std::function<void(const PerfDataProto_SampleEvent&)> sample_event_callback_;

  PerfReader(const PerfReader&) = delete;
  PerfReader& operator=(const PerfReader&) = delete;
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_PERF_READER_H_
