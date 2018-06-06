// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_TEST_PERF_DATA_H_
#define CHROMIUMOS_WIDE_PROFILING_TEST_PERF_DATA_H_

#include <memory>
#include <ostream>  
#include <vector>

#include "binary_data_utils.h"
#include "compat/string.h"
#include "kernel/perf_internals.h"

namespace quipper {
namespace testing {

// Union for punning 32-bit words into a 64-bit word.
union PunU32U64 {
  u32 v32[2];
  u64 v64;
};

class StreamWriteable {
 public:
  StreamWriteable() : is_cross_endian_(false) {}
  virtual ~StreamWriteable() {}

  virtual void WriteTo(std::ostream* out) const = 0;

  virtual StreamWriteable& WithCrossEndianness(bool value) {
    is_cross_endian_ = value;
    return *this;
  }

  // Do not call MaybeSwap() directly. The syntax of test data structure
  // initialization makes data sizes ambiguous, so these force the caller to
  // explicitly specify value sizes.
  uint16_t MaybeSwap16(uint16_t value) const { return MaybeSwap(value); }
  uint32_t MaybeSwap32(uint32_t value) const { return MaybeSwap(value); }
  uint64_t MaybeSwap64(uint64_t value) const { return MaybeSwap(value); }

 protected:
  // Derived classes can call this to determine the cross-endianness. However,
  // the actual implementation of cross-endianness is up to the derived class,
  // if it supports it at all.
  bool is_cross_endian() const { return is_cross_endian_; }

 private:
  template <typename T>
  T MaybeSwap(T value) const {
    if (is_cross_endian()) ByteSwap(&value);
    return value;
  }

  bool is_cross_endian_;
};

// Normal mode header
class ExamplePerfDataFileHeader : public StreamWriteable {
 public:
  typedef ExamplePerfDataFileHeader SelfT;
  explicit ExamplePerfDataFileHeader(const unsigned long features);  

  SelfT& WithAttrIdsCount(size_t n);
  SelfT& WithAttrCount(size_t n);
  SelfT& WithDataSize(size_t sz);

  // Used for testing compatibility w.r.t. sizeof(perf_event_attr)
  SelfT& WithCustomPerfEventAttrSize(size_t sz);

  const struct perf_file_header& header() const { return header_; }

  u64 data_end_offset() const {
    return header_.data.offset + header_.data.size;
  }
  ssize_t data_end() const { return static_cast<ssize_t>(data_end_offset()); }

  void WriteTo(std::ostream* out) const override;

 protected:
  struct perf_file_header header_;
  size_t attr_ids_count_ = 0;

 private:
  void UpdateSectionOffsets();
};

// Produces the pipe-mode file header.
class ExamplePipedPerfDataFileHeader : public StreamWriteable {
 public:
  ExamplePipedPerfDataFileHeader() {}
  void WriteTo(std::ostream* out) const override;
};

// Produces a PERF_RECORD_HEADER_ATTR event with struct perf_event_attr
// describing a hardware event. The sample_type mask and the sample_id_all
// bit are paramatized.
class ExamplePerfEventAttrEvent_Hardware : public StreamWriteable {
 public:
  typedef ExamplePerfEventAttrEvent_Hardware SelfT;
  explicit ExamplePerfEventAttrEvent_Hardware(u64 sample_type,
                                              bool sample_id_all)
      : attr_size_(sizeof(perf_event_attr)),
        sample_type_(sample_type),
        read_format_(0),
        sample_id_all_(sample_id_all),
        config_(0),
        use_clockid_(false),
        context_switch_(false),
        write_backward_(false),
        namespaces_(false) {}
  SelfT& WithConfig(u64 config) {
    config_ = config;
    return *this;
  }
  SelfT& WithAttrSize(u32 size) {
    attr_size_ = size;
    return *this;
  }
  SelfT& WithReadFormat(u64 format) {
    read_format_ = format;
    return *this;
  }

  SelfT& WithId(u64 id) {
    ids_.push_back(id);
    return *this;
  }
  SelfT& WithIds(std::initializer_list<u64> ids) {
    ids_.insert(ids_.end(), ids.begin(), ids.end());
    return *this;
  }
  SelfT& WithUseClockid(bool use_clockid) {
    use_clockid_ = use_clockid;
    return *this;
  }
  SelfT& WithContextSwitch(bool context_switch) {
    context_switch_ = context_switch;
    return *this;
  }
  SelfT& WithWriteBackward(bool write_backward) {
    write_backward_ = write_backward;
    return *this;
  }
  SelfT& WithNamespaces(bool namespaces) {
    namespaces_ = namespaces;
    return *this;
  }
  void WriteTo(std::ostream* out) const override;

 private:
  u32 attr_size_;
  const u64 sample_type_;
  u64 read_format_;
  const bool sample_id_all_;
  u64 config_;
  std::vector<u64> ids_;
  bool use_clockid_;
  bool context_switch_;
  bool write_backward_;
  bool namespaces_;
};

class AttrIdsSection : public StreamWriteable {
 public:
  explicit AttrIdsSection(size_t initial_offset) : offset_(initial_offset) {}

  perf_file_section AddId(u64 id) { return AddIds({id}); }
  perf_file_section AddIds(std::initializer_list<u64> ids) {
    ids_.insert(ids_.end(), ids.begin(), ids.end());
    perf_file_section s = {
        .offset = offset_,
        .size = ids.size() * sizeof(decltype(ids)::value_type),
    };
    offset_ += s.size;
    return s;
  }
  void WriteTo(std::ostream* out) const override;

 private:
  u64 offset_;
  std::vector<u64> ids_;
};

// Produces a struct perf_file_attr with a perf_event_attr describing a
// hardware event.
class ExamplePerfFileAttr_Hardware : public StreamWriteable {
 public:
  typedef ExamplePerfFileAttr_Hardware SelfT;
  explicit ExamplePerfFileAttr_Hardware(u64 sample_type, bool sample_id_all)
      : attr_size_(sizeof(perf_event_attr)),
        sample_type_(sample_type),
        sample_id_all_(sample_id_all),
        config_(0),
        ids_section_({.offset = MaybeSwap64(104), .size = MaybeSwap64(0)}),
        use_clockid_(false),
        context_switch_(false),
        write_backward_(false),
        namespaces_(false) {}
  SelfT& WithAttrSize(u32 size) {
    attr_size_ = size;
    return *this;
  }
  SelfT& WithConfig(u64 config) {
    config_ = config;
    return *this;
  }
  SelfT& WithIds(const perf_file_section& section) {
    ids_section_ = section;
    return *this;
  }
  SelfT& WithUseClockid(bool use_clockid) {
    use_clockid_ = use_clockid;
    return *this;
  }
  SelfT& WithContextSwitch(bool context_switch) {
    context_switch_ = context_switch;
    return *this;
  }
  SelfT& WithWriteBackward(bool write_backward) {
    write_backward_ = write_backward;
    return *this;
  }
  SelfT& WithNamespaces(bool namespaces) {
    namespaces_ = namespaces;
    return *this;
  }
  void WriteTo(std::ostream* out) const override;

 private:
  u32 attr_size_;
  const u64 sample_type_;
  const bool sample_id_all_;
  u64 config_;
  perf_file_section ids_section_;
  bool use_clockid_;
  bool context_switch_;
  bool write_backward_;
  bool namespaces_;
};

// Produces a struct perf_file_attr with a perf_event_attr describing a
// tracepoint event.
class ExamplePerfFileAttr_Tracepoint : public StreamWriteable {
 public:
  explicit ExamplePerfFileAttr_Tracepoint(const u64 tracepoint_event_id)
      : tracepoint_event_id_(tracepoint_event_id) {}
  void WriteTo(std::ostream* out) const override;

 private:
  const u64 tracepoint_event_id_;
};

// Produces a sample field array that can be used with either SAMPLE events
// or as the sample_id of another event.
// NB: This class simply places the fields in the order called. It does not
// enforce that they are in the correct order, or match the sample type.
// See enum perf_event_type in perf_event.h.
class SampleInfo {
 public:
  SampleInfo& Ip(u64 ip) { return AddField(ip); }
  SampleInfo& Tid(u32 pid, u32 tid) {
    return AddField(PunU32U64{.v32 = {pid, tid}}.v64);
  }
  SampleInfo& Tid(u32 pid) {
    return AddField(PunU32U64{.v32 = {pid, pid}}.v64);
  }
  SampleInfo& Time(u64 time) { return AddField(time); }
  SampleInfo& Id(u64 id) { return AddField(id); }
  SampleInfo& BranchStack_nr(u64 nr) { return AddField(nr); }
  SampleInfo& BranchStack_lbr(u64 from, u64 to, u64 flags) {
    AddField(from);
    AddField(to);
    AddField(flags);
    return *this;
  }

  const char* data() const {
    return reinterpret_cast<const char*>(fields_.data());
  }
  const size_t size() const {
    return fields_.size() * sizeof(decltype(fields_)::value_type);
  }

 private:
  SampleInfo& AddField(u64 value) {
    fields_.push_back(value);
    return *this;
  }

  std::vector<u64> fields_;
};

// Produces a PERF_RECORD_MMAP event with the given file and mapping.
class ExampleMmapEvent : public StreamWriteable {
 public:
  ExampleMmapEvent(u32 pid, u64 start, u64 len, u64 pgoff, string filename,
                   const SampleInfo& sample_id)
      : pid_(pid),
        start_(start),
        len_(len),
        pgoff_(pgoff),
        filename_(filename),
        sample_id_(sample_id) {}
  size_t GetSize() const;
  void WriteTo(std::ostream* out) const override;

 private:
  const u32 pid_;
  const u64 start_;
  const u64 len_;
  const u64 pgoff_;
  const string filename_;
  const SampleInfo sample_id_;
};

// Produces a PERF_RECORD_MMAP2 event with the given file and mapping.
class ExampleMmap2Event : public StreamWriteable {
 public:
  typedef ExampleMmap2Event SelfT;
  // pid is used as both pid and tid.
  ExampleMmap2Event(u32 pid, u64 start, u64 len, u64 pgoff, string filename,
                    const SampleInfo& sample_id)
      : ExampleMmap2Event(pid, pid, start, len, pgoff, filename, sample_id) {}
  ExampleMmap2Event(u32 pid, u32 tid, u64 start, u64 len, u64 pgoff,
                    string filename, const SampleInfo& sample_id)
      : pid_(pid),
        tid_(tid),
        start_(start),
        len_(len),
        pgoff_(pgoff),
        maj_(6),
        min_(7),
        ino_(8),
        filename_(filename),
        sample_id_(sample_id) {}

  SelfT& WithDeviceInfo(u32 maj, u32 min, u64 ino) {
    maj_ = maj;
    min_ = min;
    ino_ = ino;
    return *this;
  }

  void WriteTo(std::ostream* out) const override;

 private:
  const u32 pid_;
  const u32 tid_;
  const u64 start_;
  const u64 len_;
  const u64 pgoff_;
  u32 maj_;
  u32 min_;
  u64 ino_;
  const string filename_;
  const SampleInfo sample_id_;
};

// Produces a PERF_RECORD_FORK or PERF_RECORD_EXIT event.
// Cannot be instantiated directly; use a derived class.
class ExampleForkExitEvent : public StreamWriteable {
 public:
  void WriteTo(std::ostream* out) const override;

 protected:
  ExampleForkExitEvent(u32 type, u32 pid, u32 ppid, u32 tid, u32 ptid, u64 time,
                       const SampleInfo& sample_id)
      : type_(type),
        pid_(pid),
        ppid_(ppid),
        tid_(tid),
        ptid_(ptid),
        time_(time),
        sample_id_(sample_id) {}
  const u32 type_;  // Either PERF_RECORD_FORK or PERF_RECORD_EXIT.
 private:
  const u32 pid_;
  const u32 ppid_;
  const u32 tid_;
  const u32 ptid_;
  const u64 time_;
  const SampleInfo sample_id_;
};

// Produces a PERF_RECORD_FORK event.
class ExampleForkEvent : public ExampleForkExitEvent {
 public:
  ExampleForkEvent(u32 pid, u32 ppid, u32 tid, u32 ptid, u64 time,
                   const SampleInfo& sample_id)
      : ExampleForkExitEvent(PERF_RECORD_FORK, pid, ppid, tid, ptid, time,
                             sample_id) {}
};

// Produces a PERF_RECORD_EXIT event.
class ExampleExitEvent : public ExampleForkExitEvent {
 public:
  ExampleExitEvent(u32 pid, u32 ppid, u32 tid, u32 ptid, u64 time,
                   const SampleInfo& sample_id)
      : ExampleForkExitEvent(PERF_RECORD_EXIT, pid, ppid, tid, ptid, time,
                             sample_id) {}
};

// Produces the PERF_RECORD_FINISHED_ROUND event. This event is just a header.
class FinishedRoundEvent : public StreamWriteable {
 public:
  void WriteTo(std::ostream* out) const override;
};

// Produces a simple PERF_RECORD_SAMPLE event with the given sample info.
// NB: The sample_info must match the sample_type of the relevant attr.
class ExamplePerfSampleEvent : public StreamWriteable {
 public:
  explicit ExamplePerfSampleEvent(const SampleInfo& sample_info)
      : sample_info_(sample_info) {}
  size_t GetSize() const;
  void WriteTo(std::ostream* out) const override;

 private:
  const SampleInfo sample_info_;
};

class ExamplePerfSampleEvent_BranchStack : public ExamplePerfSampleEvent {
 public:
  ExamplePerfSampleEvent_BranchStack();
  static const size_t kEventSize;
};

// Produces a struct sample_event matching ExamplePerfFileAttr_Tracepoint.
class ExamplePerfSampleEvent_Tracepoint : public StreamWriteable {
 public:
  ExamplePerfSampleEvent_Tracepoint() {}
  void WriteTo(std::ostream* out) const override;
  static const size_t kEventSize;
};

// Produces a struct perf_file_section suitable for use in the metadata index.
class MetadataIndexEntry : public StreamWriteable {
 public:
  MetadataIndexEntry(u64 offset, u64 size)
      : index_entry_{.offset = offset, .size = size} {}
  void WriteTo(std::ostream* out) const override {
    struct perf_file_section entry = {
        .offset = MaybeSwap64(index_entry_.offset),
        .size = MaybeSwap64(index_entry_.size),
    };
    out->write(reinterpret_cast<const char*>(&entry), sizeof(entry));
  }

 public:
  const perf_file_section index_entry_;
};

// Produces sample string metadata, and corresponding metadata index entry.
class ExampleStringMetadata : public StreamWriteable {
 public:
  // The input string gets zero-padded/truncated to |kStringAlignSize| bytes if
  // it is shorter/longer, respectively.
  explicit ExampleStringMetadata(const string& data, size_t offset)
      : data_(data), index_entry_(offset, sizeof(u32) + kStringAlignSize) {
    data_.resize(kStringAlignSize);
  }
  void WriteTo(std::ostream* out) const override;

  const MetadataIndexEntry& index_entry() { return index_entry_; }
  size_t size() const { return sizeof(u32) + data_.size(); }

  StreamWriteable& WithCrossEndianness(bool value) override {
    // Set index_entry_'s endianness since it is owned by this class.
    index_entry_.WithCrossEndianness(value);
    return StreamWriteable::WithCrossEndianness(value);
  }

 private:
  string data_;
  MetadataIndexEntry index_entry_;

  static const int kStringAlignSize = 64;
};

// Produces sample string metadata event for piped mode.
class ExampleStringMetadataEvent : public StreamWriteable {
 public:
  // The input string gets aligned to |kStringAlignSize|.
  explicit ExampleStringMetadataEvent(u32 type, const string& data)
      : type_(type), data_(data) {
    data_.resize(kStringAlignSize);
  }
  void WriteTo(std::ostream* out) const override;

 private:
  u32 type_;
  string data_;

  static const int kStringAlignSize = 64;
};

// Produces sample tracing metadata, and corresponding metadata index entry.
class ExampleTracingMetadata {
 public:
  class Data : public StreamWriteable {
   public:
    static const string kTraceMetadata;

    explicit Data(ExampleTracingMetadata* parent) : parent_(parent) {}

    const string& value() const { return kTraceMetadata; }

    void WriteTo(std::ostream* out) const override;

   private:
    ExampleTracingMetadata* parent_;
  };

  explicit ExampleTracingMetadata(size_t offset)
      : data_(this), index_entry_(offset, data_.value().size()) {}

  const Data& data() { return data_; }
  const MetadataIndexEntry& index_entry() { return index_entry_; }

 private:
  friend class Data;
  Data data_;
  MetadataIndexEntry index_entry_;
};

// Produces a PERF_RECORD_AUXTRACE event.
class ExampleAuxtraceEvent : public StreamWriteable {
 public:
  ExampleAuxtraceEvent(u64 size, u64 offset, u64 reference, u32 idx, u32 tid,
                       u32 cpu, u32 reserved, string trace_data)
      : size_(size),
        offset_(offset),
        reference_(reference),
        idx_(idx),
        tid_(tid),
        cpu_(cpu),
        reserved_(reserved),
        trace_data_(std::move(trace_data)) {}
  size_t GetSize() const;
  size_t GetTraceSize() const;
  void WriteTo(std::ostream* out) const override;

 private:
  const u64 size_;
  const u64 offset_;
  const u64 reference_;
  const u32 idx_;
  const u32 tid_;
  const u32 cpu_;
  const u32 reserved_;
  const string trace_data_;
};

// Produces PERF_RECORD_SWITCH or PERF_RECORD_SWITCH_CPU_WIDE events.
class ExampleContextSwitchEvent : public StreamWriteable {
 public:
  ExampleContextSwitchEvent(bool is_out, const SampleInfo& sample_id)
      : type_(PERF_RECORD_SWITCH),
        is_out_(is_out),
        next_prev_pid_(0),
        next_prev_tid_(0),
        sample_id_(sample_id) {}
  ExampleContextSwitchEvent(bool is_out, u32 next_prev_pid, u32 next_prev_tid,
                            const SampleInfo& sample_id)
      : type_(PERF_RECORD_SWITCH_CPU_WIDE),
        is_out_(is_out),
        next_prev_pid_(next_prev_pid),
        next_prev_tid_(next_prev_tid),
        sample_id_(sample_id) {}
  size_t GetSize() const;
  void WriteTo(std::ostream* out) const override;

 private:
  const u32 type_;
  const bool is_out_;
  const u32 next_prev_pid_;
  const u32 next_prev_tid_;
  const SampleInfo sample_id_;
};

// Produces PERF_RECORD_NAMESPACES event.
class ExampleNamespacesEvent : public StreamWriteable {
 public:
  ExampleNamespacesEvent(u32 pid, u32 tid,
                         std::vector<struct perf_ns_link_info> link_info,
                         const SampleInfo& sample_id)
      : pid_(pid),
        tid_(tid),
        link_info_(std::move(link_info)),
        sample_id_(sample_id) {}
  size_t GetSize() const;
  void WriteTo(std::ostream* out) const override;

 private:
  const u32 pid_;
  const u32 tid_;
  const std::vector<struct perf_ns_link_info> link_info_;
  const SampleInfo sample_id_;
};

// Produces PERF_RECORD_AUXTRACE_INFO event.
class ExampleAuxtraceInfoEvent : public StreamWriteable {
 public:
  ExampleAuxtraceInfoEvent(u32 type, std::vector<u64> priv)
      : type_(type), priv_(std::move(priv)) {}
  size_t GetSize() const;
  void WriteTo(std::ostream* out) const override;

 private:
  const u32 type_;
  const std::vector<u64> priv_;
};

// Produces PERF_RECORD_AUXTRACE_ERROR event.
class ExampleAuxtraceErrorEvent : public StreamWriteable {
 public:
  ExampleAuxtraceErrorEvent(u32 type, u32 code, u32 cpu, u32 pid, u32 tid,
                            u32 ip, string msg)
      : type_(type),
        code_(code),
        cpu_(cpu),
        pid_(pid),
        tid_(tid),
        ip_(ip),
        msg_(std::move(msg)) {}
  size_t GetSize() const;
  void WriteTo(std::ostream* out) const override;

 private:
  const u32 type_;
  const u32 code_;
  const u32 cpu_;
  const u32 pid_;
  const u32 tid_;
  const u64 ip_;
  const string msg_;
};

// Produces PERF_RECORD_THREAD_MAP event.
class ExampleThreadMapEvent : public StreamWriteable {
 public:
  ExampleThreadMapEvent(std::vector<struct thread_map_event_entry> entries)
      : entries_(std::move(entries)) {}
  size_t GetSize() const;
  void WriteTo(std::ostream* out) const override;

 private:
  const std::vector<struct thread_map_event_entry> entries_;
};

// Produces PERF_RECORD_TIME_CONV event.
class ExampleTimeConvEvent : public StreamWriteable {
 public:
  ExampleTimeConvEvent(u64 time_shift, u64 time_mult, u64 time_zero)
      : time_shift_(time_shift), time_mult_(time_mult), time_zero_(time_zero) {}
  size_t GetSize() const;
  void WriteTo(std::ostream* out) const override;

 private:
  const u64 time_shift_;
  const u64 time_mult_;
  const u64 time_zero_;
};

}  // namespace testing
}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_TEST_PERF_DATA_H_
