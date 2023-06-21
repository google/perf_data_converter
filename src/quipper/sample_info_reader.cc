// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sample_info_reader.h"

#include <string.h>

#include <cstdint>

#include "base/logging.h"
#include "buffer_reader.h"
#include "buffer_writer.h"
#include "kernel/perf_event.h"
#include "kernel/perf_internals.h"
#include "perf_data_utils.h"

namespace quipper {

namespace {

// Read read info from perf data.  Corresponds to sample format type
// PERF_SAMPLE_READ in the !PERF_FORMAT_GROUP case. Returns true when read info
// data is read completely. Otherwise, returns false.
bool ReadReadInfo(DataReader* reader, uint64_t read_format,
                  struct perf_sample* sample) {
  if (!reader->ReadUint64(&sample->read.one.value)) {
    return false;
  }
  if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED &&
      !reader->ReadUint64(&sample->read.time_enabled)) {
    return false;
  }
  if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING &&
      !reader->ReadUint64(&sample->read.time_running)) {
    return false;
  }
  if (read_format & PERF_FORMAT_ID &&
      !reader->ReadUint64(&sample->read.one.id)) {
    return false;
  }
  return true;
}

// Read read info from perf data.  Corresponds to sample format type
// PERF_SAMPLE_READ in the PERF_FORMAT_GROUP case. Returns true when group read
// info data is read completely. Otherwise, returns false.
bool ReadGroupReadInfo(DataReader* reader, uint64_t read_format,
                       struct perf_sample* sample) {
  uint64_t num = 0;
  if (!reader->ReadUint64(&num)) {
    return false;
  }
  if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED &&
      !reader->ReadUint64(&sample->read.time_enabled)) {
    return false;
  }

  if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING &&
      !reader->ReadUint64(&sample->read.time_running)) {
    return false;
  }

  // Make sure there is no existing allocated memory in
  // |sample->read.group.values|.
  CHECK_EQ(static_cast<void*>(NULL), sample->read.group.values);
  size_t entry_size = 0;
  if (read_format & PERF_FORMAT_ID) {
    // values { u64 value; u64 id };
    entry_size = sizeof(u64) * 2;
  } else {
    // values { u64 value; };
    entry_size = sizeof(u64);
  }
  // Calculate the maximum possible number of values assuming the rest of the
  // event as an array of the value struct.
  size_t max_num_values = (reader->size() - reader->Tell()) / entry_size;
  if (num > max_num_values) {
    LOG(ERROR) << "Number of group read info values " << num
               << " cannot exceed the maximum possible number of values "
               << max_num_values;
    return false;
  }

  sample_read_value* values = new sample_read_value[num];
  for (uint64_t i = 0; i < num; i++) {
    if (!reader->ReadUint64(&values[i].value)) {
      return false;
    }
    if (read_format & PERF_FORMAT_ID && !reader->ReadUint64(&values[i].id)) {
      return false;
    }
  }
  sample->read.group.nr = num;
  sample->read.group.values = values;
  return true;
}

// Read call chain info from perf data.  Corresponds to sample format type
// PERF_SAMPLE_CALLCHAIN. Returns true when callchain data is read completely.
// Otherwise, returns false.
bool ReadCallchain(DataReader* reader, struct perf_sample* sample) {
  // Make sure there is no existing allocated memory in |sample->callchain|.
  CHECK_EQ(static_cast<void*>(NULL), sample->callchain);

  // The callgraph data consists of a uint64_t value |nr| followed by |nr|
  // addresses.
  uint64_t callchain_size = 0;
  if (!reader->ReadUint64(&callchain_size)) {
    return false;
  }

  // Calculate the maximum possible number of callstack stack entries assuming
  // the rest of the event as an array of u64 entries.
  size_t max_callchain_entries =
      (reader->size() - reader->Tell()) / sizeof(u64);
  if (callchain_size > max_callchain_entries) {
    LOG(ERROR)
        << "Number of callchain entries " << callchain_size
        << " cannot exceed the maximum possible number of callchain entries "
        << max_callchain_entries;
    return false;
  }

  sample->callchain =
      reinterpret_cast<struct ip_callchain*>(new uint64_t[callchain_size + 1]);
  sample->callchain->nr = callchain_size;

  for (size_t i = 0; i < callchain_size; ++i) {
    if (!reader->ReadUint64(&sample->callchain->ips[i])) {
      LOG(ERROR) << "Failed to read the callchain entry: " << i;
      return false;
    }
  }

  return true;
}

// Read raw info from perf data.  Corresponds to sample format type
// PERF_SAMPLE_RAW. Returns true when raw data is read completely. Otherwise,
// return false.
bool ReadRawData(DataReader* reader, struct perf_sample* sample) {
  // Save the original read offset.
  size_t reader_offset = reader->Tell();

  if (!reader->ReadUint32(&sample->raw_size)) {
    return false;
  }

  size_t remaining_size = reader->size() - reader->Tell();
  if (sample->raw_size > remaining_size) {
    LOG(ERROR) << "Raw data size " << sample->raw_size
               << " cannot exceed the remaining event size " << remaining_size;
    return false;
  }
  // Allocate space for and read the raw data bytes.
  sample->raw_data = new uint8_t[sample->raw_size];
  if (!reader->ReadData(sample->raw_size, sample->raw_data)) {
    return false;
  }

  // Determine the bytes that were read, and align to the next 64 bits.
  reader_offset += Align<uint64_t>(sizeof(sample->raw_size) + sample->raw_size);
  if (!reader->SeekSet(reader_offset)) return false;

  return true;
}

// Read branch stack info from perf data. Corresponds to sample format type
// PERF_SAMPLE_BRANCH_STACK. Returns true when branch stack data is read
// completely. Otherwise, returns false.
bool ReadBranchStack(DataReader* reader, struct perf_sample* sample) {
  // Make sure there is no existing allocated memory in
  // |sample->branch_stack|.
  CHECK_EQ(static_cast<void*>(NULL), sample->branch_stack);

  // The branch stack data consists of a uint64_t value |nr| followed by
  // uint64_t hw_index which is optional and |nr| branch_entry structs.
  uint64_t branch_stack_size = 0;
  uint64_t branch_stack_hw_idx = 0;

  if (!reader->ReadUint64(&branch_stack_size)) {
    return false;
  }
  // no_hw_idx is cleared when branch stack contains an extra field.
  if (!sample->no_hw_idx) {
    if (!reader->ReadUint64(&branch_stack_hw_idx)) {
      return false;
    }
  }

  // Calculate the maximum possible number of branch stack entries assuming the
  // rest of the event as an array of the struct branch_entry.
  const u64 max_branch_stack_entries =
      (reader->size() - reader->Tell()) / sizeof(struct branch_entry);
  if (branch_stack_size > max_branch_stack_entries) {
    LOG(ERROR) << "Number of branch stack entries " << branch_stack_size
               << " cannot exceed the maximum possible number of branch stack"
               << " entries " << max_branch_stack_entries;
    return false;
  }

  // Refer to kernel/perf_internals.h for more details on the branch_stack
  // layout.
  struct branch_stack* branch_stack = reinterpret_cast<struct branch_stack*>(
      new uint8_t[sizeof(uint64_t) + sizeof(uint64_t) +
                  branch_stack_size * sizeof(struct branch_entry)]);
  branch_stack->nr = branch_stack_size;
  branch_stack->hw_idx = branch_stack_hw_idx;
  sample->branch_stack = branch_stack;
  for (size_t i = 0; i < branch_stack_size; ++i) {
    if (!reader->ReadUint64(&branch_stack->entries[i].from) ||
        !reader->ReadUint64(&branch_stack->entries[i].to) ||
        !reader->ReadData(sizeof(branch_stack->entries[i].flags),
                          &branch_stack->entries[i].flags)) {
      LOG(ERROR) << "Failed to read the branch stack entry: " << i;
      return false;
    }

    if (reader->is_cross_endian()) {
      LOG(ERROR) << "Byte swapping of branch stack flags is not yet supported.";
      return false;
    }
  }
  return true;
}

// Reads user sample regs info from perf data. Corresponds to sample format
// type PERF_SAMPLE_REGS_USER. Returns true when user sample regs data is
// read completely. Otherwise, returns false.
bool ReadSampleRegsUser(DataReader* reader,
                        const struct perf_event_attr& attr,
                        struct perf_sample* sample) {
  // Make sure there is no existing allocated memory in |sample->user_regs|.
  CHECK_EQ(static_cast<void*>(NULL), sample->user_regs);

  // abi can only be 0, 1, or 2.
  uint64_t abi = 3;
  if (!reader->ReadUint64(&abi) || abi > 2) {
    LOG(ERROR) << "Failed to read the abi.";
    return false;
  }

  // If abi is 0, there's no regs recorded. Refer to perf_sample_regs_user() and
  // perf_output_sample() in kernel/events/core.c.
  if (abi == 0) {
    return true;
  }

  std::bitset<64> b(attr.sample_regs_user);
  size_t regs_size = b.count();

  sample->user_regs =
      reinterpret_cast<struct regs_dump*>(new uint64_t[regs_size + 1]);
  sample->user_regs->abi = abi;

  for (size_t i = 0; i < regs_size; ++i) {
        if (!reader->ReadUint64(&sample->user_regs->regs[i])) {
          LOG(ERROR) << "Failed to read the cpu registers: " << i << " " << regs_size;
          return false;
        }
  }

  return true;
}

// Reads user sample stack info from perf data. Corresponds to sample format
// type PERF_SAMPLE_STACK_USER. Returns true when user sample stack data is
// read completely. Otherwise, returns false.
bool ReadSampleStackUser(DataReader* reader,
                        const struct perf_event_attr& attr,
                        struct perf_sample* sample) {
  // Save the original read offset.
  size_t reader_offset = reader->Tell();

  uint64_t size = 0;
  if (!reader->ReadUint64(&size)) {
    LOG(ERROR) << "Failed to read the stack size.";
    return false;
  }
  sample->user_stack.size = size;
  if (sample->user_stack.size == 0) {
    return true;
  }

  // Allocate space for and read the raw data bytes.
  sample->user_stack.data = new uint8_t[sample->user_stack.size];
  if (!reader->ReadData(sample->user_stack.size, sample->user_stack.data)) {
      LOG(ERROR) << "Failed to read the stack data.";
      return false;
  }

  sample->user_stack.dyn_size = new u64;
  if (!reader->ReadUint64(sample->user_stack.dyn_size)) {
    LOG(ERROR) << "Failed to read the dynamic size.";
    return false;
  }

  if (*sample->user_stack.dyn_size > sample->user_stack.size) {
    LOG(ERROR)
        << "Dynamic size " << *sample->user_stack.dyn_size
        << " cannot exceed the size of " << sample->user_stack.size;
    return false;
  }

  // Determine the bytes that were read, and align to the next 64 bits.
  reader_offset += Align<uint64_t>(sizeof(sample->user_stack.size) +
                                   sample->user_stack.size +
                                   sizeof(sample->user_stack.dyn_size));
  if (!reader->SeekSet(reader_offset)) return false;

  return true;
}

// Reads perf sample info data from |event| into |sample|.
// |attr| is the event attribute struct, which contains info such as which
// sample info fields are present.
// |is_cross_endian| indicates that the data is cross-endian and that the byte
// order should be reversed for each field according to its size.
// Returns true when sample info data is read completely and updates the
// |read_size| with the size of read or skipped perf sample info data. On
// failure, returns false.
bool ReadPerfSampleFromData(const event_t& event,
                            const struct perf_event_attr& attr,
                            bool is_cross_endian, struct perf_sample* sample,
                            size_t* read_size) {
  BufferReader reader(&event, event.header.size);
  reader.set_is_cross_endian(is_cross_endian);
  size_t seek = GetEventDataSize(event);
  if (seek == 0) {
    LOG(ERROR) << "Couldn't get event data size for event "
               << GetEventName(event.header.type);
    return false;
  }
  if (!reader.SeekSet(seek)) return false;

  if (!(event.header.type == PERF_RECORD_SAMPLE || attr.sample_id_all)) {
    *read_size = reader.Tell();
    return true;
  }

  uint64_t sample_fields = SampleInfoReader::GetSampleFieldsForEventType(
      event.header.type, attr.sample_type);

  // See structure for PERF_RECORD_SAMPLE in kernel/perf_event.h
  // and compare sample_id when sample_id_all is set.

  // NB: For sample_id, sample_fields has already been masked to the set
  // of fields in that struct by GetSampleFieldsForEventType. That set
  // of fields is mostly in the same order as PERF_RECORD_SAMPLE, with
  // the exception of PERF_SAMPLE_IDENTIFIER.

  // PERF_SAMPLE_IDENTIFIER is in a different location depending on
  // if this is a SAMPLE event or the sample_id of another event.
  if (event.header.type == PERF_RECORD_SAMPLE) {
    // { u64                   id;       } && PERF_SAMPLE_IDENTIFIER
    if (sample_fields & PERF_SAMPLE_IDENTIFIER &&
        !reader.ReadUint64(&sample->id)) {
      LOG(ERROR) << "Couldn't read PERF_SAMPLE_IDENTIFIER";
      return false;
    }
  }

  // { u64                   ip;       } && PERF_SAMPLE_IP
  if (sample_fields & PERF_SAMPLE_IP && !reader.ReadUint64(&sample->ip)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_IP";
    return false;
  }

  // { u32                   pid, tid; } && PERF_SAMPLE_TID
  if (sample_fields & PERF_SAMPLE_TID) {
    if (!reader.ReadUint32(&sample->pid) || !reader.ReadUint32(&sample->tid)) {
      LOG(ERROR) << "Couldn't read PERF_SAMPLE_TID";
      return false;
    }
  }

  // { u64                   time;     } && PERF_SAMPLE_TIME
  if (sample_fields & PERF_SAMPLE_TIME && !reader.ReadUint64(&sample->time)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_TIME";
    return false;
  }

  // { u64                   addr;     } && PERF_SAMPLE_ADDR
  if (sample_fields & PERF_SAMPLE_ADDR && !reader.ReadUint64(&sample->addr)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_ADDR";
    return false;
  }

  // { u64                   id;       } && PERF_SAMPLE_ID
  if (sample_fields & PERF_SAMPLE_ID && !reader.ReadUint64(&sample->id)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_ID";
    return false;
  }

  // { u64                   stream_id;} && PERF_SAMPLE_STREAM_ID
  if (sample_fields & PERF_SAMPLE_STREAM_ID &&
      !reader.ReadUint64(&sample->stream_id)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_STREAM_ID";
    return false;
  }

  // { u32                   cpu, res; } && PERF_SAMPLE_CPU
  if (sample_fields & PERF_SAMPLE_CPU) {
    if (!reader.ReadUint32(&sample->cpu)) {
      LOG(ERROR) << "Couldn't read the CPU field of PERF_SAMPLE_CPU";
      return false;
    }

    // The PERF_SAMPLE_CPU format bit specifies 64-bits of data, but the actual
    // CPU number is really only 32 bits. There is an extra 32-bit word of
    // reserved padding, as the whole field is aligned to 64 bits.

    // reader.ReadUint32(&sample->res);  // reserved
    u32 reserved;
    if (!reader.ReadUint32(&reserved)) {
      LOG(ERROR) << "Couldn't read the reserved bits of PERF_SAMPLE_CPU";
      return false;
    }
  }

  // This is the location of PERF_SAMPLE_IDENTIFIER in struct sample_id.
  if (event.header.type != PERF_RECORD_SAMPLE) {
    // { u64                   id;       } && PERF_SAMPLE_IDENTIFIER
    if (sample_fields & PERF_SAMPLE_IDENTIFIER &&
        !reader.ReadUint64(&sample->id)) {
      LOG(ERROR) << "Couldn't read PERF_SAMPLE_IDENTIFIER";
      return false;
    }
  }

  //
  // The remaining fields are only in PERF_RECORD_SAMPLE
  //

  // { u64                   period;   } && PERF_SAMPLE_PERIOD
  if (sample_fields & PERF_SAMPLE_PERIOD &&
      !reader.ReadUint64(&sample->period)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_PERIOD";
    return false;
  }

  // { struct read_format    values;   } && PERF_SAMPLE_READ
  if (sample_fields & PERF_SAMPLE_READ) {
    if (attr.read_format & PERF_FORMAT_GROUP) {
      if (!ReadGroupReadInfo(&reader, attr.read_format, sample)) {
        LOG(ERROR) << "Couldn't read group read info of PERF_SAMPLE_READ";
        return false;
      }
    } else {
      if (!ReadReadInfo(&reader, attr.read_format, sample)) {
        LOG(ERROR) << "Couldn't read read info of PERF_SAMPLE_READ";
        return false;
      }
    }
  }

  // { u64                   nr,
  //   u64                   ips[nr];  } && PERF_SAMPLE_CALLCHAIN
  if (sample_fields & PERF_SAMPLE_CALLCHAIN &&
      !ReadCallchain(&reader, sample)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_CALLCHAIN";
    return false;
  }

  // { u32                   size;
  //   char                  data[size];}&& PERF_SAMPLE_RAW
  if (sample_fields & PERF_SAMPLE_RAW && !ReadRawData(&reader, sample)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_RAW";
    return false;
  }

  // { u64                   nr;
  //   { u64 hw_idx; } && PERF_SAMPLE_BRANCH_HW_INDEX
  //   { u64 from, to, flags } lbr[nr];} && PERF_SAMPLE_BRANCH_STACK
  if (sample_fields & PERF_SAMPLE_BRANCH_STACK) {
    sample->no_hw_idx = false;
    if (!(attr.branch_sample_type & PERF_SAMPLE_BRANCH_HW_INDEX))
      sample->no_hw_idx = true;
    if (!ReadBranchStack(&reader, sample)) {
      LOG(ERROR) << "Couldn't read PERF_SAMPLE_BRANCH_STACK";
      return false;
    }
  }

  // { u64                   abi; # enum perf_sample_regs_abi
  //   u64                   regs[weight(mask)]; } && PERF_SAMPLE_REGS_USER
  if (sample_fields & PERF_SAMPLE_REGS_USER) {
    if (!ReadSampleRegsUser(&reader, attr, sample)) {
      LOG(ERROR) << "Couldn't read PERF_SAMPLE_REGS_USER";
      return false;
    }
  }

  // { u64                   size;
  //   char                  data[size];
  //   u64                   dyn_size; } && PERF_SAMPLE_STACK_USER
  if (sample_fields & PERF_SAMPLE_STACK_USER) {
    //if (!ReadSampleStackUser(&reader, sample)) {
    if (!ReadSampleStackUser(&reader, attr, sample)) {
      LOG(ERROR) << "Couldn't read PERF_SAMPLE_STACK_USER";
      return false;
    }
  }

  /*
   * 	{ union perf_sample_weight
   *	 {
   *		u64		full; && PERF_SAMPLE_WEIGHT
   *	#if defined(__LITTLE_ENDIAN_BITFIELD)
   *		struct {
   *			u32	var1_dw;
   *			u16	var2_w;
   *			u16	var3_w;
   *		} && PERF_SAMPLE_WEIGHT_STRUCT
   *	#elif defined(__BIG_ENDIAN_BITFIELD)
   *		struct {
   *			u16	var3_w;
   *			u16	var2_w;
   *			u32	var1_dw;
   *		} && PERF_SAMPLE_WEIGHT_STRUCT
   *	#endif
   *	 }
   *	}
   */
  if (sample_fields & PERF_SAMPLE_WEIGHT &&
      sample_fields & PERF_SAMPLE_WEIGHT_STRUCT) {
    LOG(ERROR) << "Invalid combination: both PERF_SAMPLE_WEIGHT and "
                  "PERF_SAMPLE_WEIGHT_STRUCT are set at the same time.";
    return false;
  }
  if (sample_fields & PERF_SAMPLE_WEIGHT &&
      !reader.ReadUint64(&sample->weight.full)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_WEIGHT";
    return false;
  } else if (sample_fields & PERF_SAMPLE_WEIGHT_STRUCT) {
    if (reader.is_cross_endian()) {
      LOG(ERROR) << "Byte swapping of weight struct is not yet supported.";
      return false;
    }
    if (!(reader.ReadUint32(&sample->weight.var1_dw) &&
          reader.ReadUint16(&sample->weight.var2_w) &&
          reader.ReadUint16(&sample->weight.var3_w))) {
      LOG(ERROR) << "Couldn't read PERF_SAMPLE_WEIGHT_STRUCT";
      return false;
    }
  }

  // { u64                   data_src; } && PERF_SAMPLE_DATA_SRC
  if (sample_fields & PERF_SAMPLE_DATA_SRC &&
      !reader.ReadUint64(&sample->data_src)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_DATA_SRC";
    return false;
  }

  // { u64                   transaction; } && PERF_SAMPLE_TRANSACTION
  if (sample_fields & PERF_SAMPLE_TRANSACTION &&
      !reader.ReadUint64(&sample->transaction)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_TRANSACTION";
    return false;
  }

  // { u64                   abi; # enum perf_sample_regs_abi
  //   u64                   regs[weight(mask)]; } && PERF_SAMPLE_REGS_INTR
  if (sample_fields & PERF_SAMPLE_REGS_INTR) {
    LOG(ERROR) << "PERF_SAMPLE_REGS_INTR is not yet supported.";
    return false;
  }

  // { u64                   phys_addr; } && PERF_SAMPLE_PHYS_ADDR
  if (sample_fields & PERF_SAMPLE_PHYS_ADDR &&
      !reader.ReadUint64(&sample->physical_addr)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_PHYS_ADDR";
    return false;
  }

  // { u64                   cgroup; } && PERF_SAMPLE_CGROUP
  if (sample_fields & PERF_SAMPLE_CGROUP &&
      !reader.ReadUint64(&sample->cgroup)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_CGROUP";
    return false;
  }

  // { u64                   data_page_size; } && PERF_SAMPLE_DATA_PAGE_SIZE
  if (sample_fields & PERF_SAMPLE_DATA_PAGE_SIZE &&
      !reader.ReadUint64(&sample->data_page_size)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_DATA_PAGE_SIZE";
    return false;
  }

  // { u64                   code_page_size; } && PERF_SAMPLE_CODE_PAGE_SIZE
  if (sample_fields & PERF_SAMPLE_CODE_PAGE_SIZE &&
      !reader.ReadUint64(&sample->code_page_size)) {
    LOG(ERROR) << "Couldn't read PERF_SAMPLE_CODE_PAGE_SIZE";
    return false;
  }

  if (sample_fields & ~(PERF_SAMPLE_MAX - 1)) {
    LOG(ERROR) << "Unrecognized sample fields 0x" << std::hex
               << (sample_fields & ~(PERF_SAMPLE_MAX - 1));
    return false;
  }

  *read_size = reader.Tell();
  return true;
}

class PerfSampleDataWriter {
 public:
  // PerfSampleDataWriter writes perf sample data to the given array if skip
  // write is not set. The caller maintains the ownership of the array.
  PerfSampleDataWriter(uint64_t* array, bool skip_write) {
    initial_array_ptr_ = array;
    array_ = array;
    skip_write_ = skip_write;
    size_ = 0;
    if (!skip_write_) CHECK(array);
  }

  // Writes perf sample data from |sample| into the event pointed by the
  // underlying array if skip_write_ is not set. |attr| is the event attribute
  // struct, which contains info such as which sample info fields are present.
  // Returns the number of bytes written or skipped.
  size_t Write(const struct perf_sample& sample,
               const struct perf_event_attr& attr, uint32_t event_type);

 private:
  void WriteData(uint64_t data) {
    if (!skip_write_) *array_++ = data;
    size_ += sizeof(uint64_t);
  }

  void WriteSizeAndData(uint32_t size, void* data) {
    // Update the data read pointer after aligning to the next 64 bytes.
    int num_bytes = Align<uint64_t>(sizeof(size) + size);

    if (!skip_write_) {
      uint32_t* ptr = reinterpret_cast<uint32_t*>(array_);
      *ptr++ = size;
      memcpy(ptr, data, size);
      array_ += num_bytes / sizeof(uint64_t);
    }

    size_ += num_bytes;
  }

  void WriteData(uint32_t size, void* data) {
    // Update the data read pointer after aligning to the next 64 bytes.
    int num_bytes = Align<uint64_t>(size);

    if (!skip_write_) {
      memcpy(array_, data, size);
      array_ += num_bytes / sizeof(uint64_t);
    }

    size_ += num_bytes;
  }

  size_t GetWrittenSize() {
    if (!skip_write_) {
      CHECK((array_ - initial_array_ptr_) * sizeof(uint64_t) == size_);
    }
    return size_;
  }

  uint64_t* initial_array_ptr_;
  uint64_t* array_;
  bool skip_write_;
  size_t size_;
};

size_t PerfSampleDataWriter::Write(const struct perf_sample& sample,
                                   const struct perf_event_attr& attr,
                                   uint32_t event_type) {
  array_ = initial_array_ptr_;
  if (!(event_type == PERF_RECORD_SAMPLE || attr.sample_id_all)) {
    return 0;
  }

  uint64_t sample_fields = SampleInfoReader::GetSampleFieldsForEventType(
      event_type, attr.sample_type);

  union {
    uint32_t val32[sizeof(uint64_t) / sizeof(uint32_t)];
    uint64_t val64;
  };

  // See notes at the top of ReadPerfSampleFromData regarding the structure
  // of PERF_RECORD_SAMPLE, sample_id, and PERF_SAMPLE_IDENTIFIER, as they
  // all apply here as well.

  // PERF_SAMPLE_IDENTIFIER is in a different location depending on
  // if this is a SAMPLE event or the sample_id of another event.
  if (event_type == PERF_RECORD_SAMPLE) {
    // { u64                   id;       } && PERF_SAMPLE_IDENTIFIER
    if (sample_fields & PERF_SAMPLE_IDENTIFIER) {
      WriteData(sample.id);
    }
  }

  // { u64                   ip;       } && PERF_SAMPLE_IP
  if (sample_fields & PERF_SAMPLE_IP) {
    WriteData(sample.ip);
  }

  // { u32                   pid, tid; } && PERF_SAMPLE_TID
  if (sample_fields & PERF_SAMPLE_TID) {
    val32[0] = sample.pid;
    val32[1] = sample.tid;
    WriteData(val64);
  }

  // { u64                   time;     } && PERF_SAMPLE_TIME
  if (sample_fields & PERF_SAMPLE_TIME) {
    WriteData(sample.time);
  }

  // { u64                   addr;     } && PERF_SAMPLE_ADDR
  if (sample_fields & PERF_SAMPLE_ADDR) {
    WriteData(sample.addr);
  }

  // { u64                   id;       } && PERF_SAMPLE_ID
  if (sample_fields & PERF_SAMPLE_ID) {
    WriteData(sample.id);
  }

  // { u64                   stream_id;} && PERF_SAMPLE_STREAM_ID
  if (sample_fields & PERF_SAMPLE_STREAM_ID) {
    WriteData(sample.stream_id);
  }

  // { u32                   cpu, res; } && PERF_SAMPLE_CPU
  if (sample_fields & PERF_SAMPLE_CPU) {
    val32[0] = sample.cpu;
    // val32[1] = sample.res;  // reserved
    val32[1] = 0;
    WriteData(val64);
  }

  // This is the location of PERF_SAMPLE_IDENTIFIER in struct sample_id.
  if (event_type != PERF_RECORD_SAMPLE) {
    // { u64                   id;       } && PERF_SAMPLE_IDENTIFIER
    if (sample_fields & PERF_SAMPLE_IDENTIFIER) {
      WriteData(sample.id);
    }
  }

  //
  // The remaining fields are only in PERF_RECORD_SAMPLE
  //

  // { u64                   period;   } && PERF_SAMPLE_PERIOD
  if (sample_fields & PERF_SAMPLE_PERIOD) {
    WriteData(sample.period);
  }

  // { struct read_format    values;   } && PERF_SAMPLE_READ
  if (sample_fields & PERF_SAMPLE_READ) {
    if (attr.read_format & PERF_FORMAT_GROUP) {
      WriteData(sample.read.group.nr);
      if (attr.read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
        WriteData(sample.read.time_enabled);
      if (attr.read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
        WriteData(sample.read.time_running);
      for (size_t i = 0; i < sample.read.group.nr; i++) {
        WriteData(sample.read.group.values[i].value);
        if (attr.read_format & PERF_FORMAT_ID) WriteData(sample.read.one.id);
      }
    } else {
      WriteData(sample.read.one.value);
      if (attr.read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
        WriteData(sample.read.time_enabled);
      if (attr.read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
        WriteData(sample.read.time_running);
      if (attr.read_format & PERF_FORMAT_ID) WriteData(sample.read.one.id);
    }
  }

  // { u64                   nr,
  //   u64                   ips[nr];  } && PERF_SAMPLE_CALLCHAIN
  if (sample_fields & PERF_SAMPLE_CALLCHAIN) {
    if (!sample.callchain) {
      // When no callchain data is available, write the callchain size as 0.
      WriteData(0);
      LOG(ERROR) << "Expecting callchain data, but none was found.";
    } else {
      WriteData(sample.callchain->nr);
      for (size_t i = 0; i < sample.callchain->nr; ++i)
        WriteData(sample.callchain->ips[i]);
    }
  }

  // { u32                   size;
  //   char                  data[size];}&& PERF_SAMPLE_RAW
  if (sample_fields & PERF_SAMPLE_RAW) {
    WriteSizeAndData(sample.raw_size, sample.raw_data);
  }

  // { u64                   nr;
  //   { u64 hw_idx; } && PERF_SAMPLE_BRANCH_HW_INDEX
  //   { u64 from, to, flags } lbr[nr];} && PERF_SAMPLE_BRANCH_STACK
  if (sample_fields & PERF_SAMPLE_BRANCH_STACK) {
    uint64_t branch_stack_size = 0;
    uint64_t branch_stack_hw_idx = 0;

    if (sample.branch_stack) {
      branch_stack_size = sample.branch_stack->nr;
      branch_stack_hw_idx = sample.branch_stack->hw_idx;
    }

    WriteData(branch_stack_size);
    if (!sample.no_hw_idx) WriteData(branch_stack_hw_idx);
    for (size_t i = 0; i < branch_stack_size; ++i) {
      WriteData(sample.branch_stack->entries[i].from);
      WriteData(sample.branch_stack->entries[i].to);
      WriteData(sizeof(uint64_t), &sample.branch_stack->entries[i].flags);
    }
  }

  // { u64                   abi; # enum perf_sample_regs_abi
  //   u64                   regs[weight(mask)]; } && PERF_SAMPLE_REGS_USER
  if (sample_fields & PERF_SAMPLE_REGS_USER) {
    LOG(ERROR) << "PERF_SAMPLE_REGS_USER is not yet supported.";
    return GetWrittenSize();
  }

  // { u64                   size;
  //   char                  data[size];
  //   u64                   dyn_size; } && PERF_SAMPLE_STACK_USER
  if (sample_fields & PERF_SAMPLE_STACK_USER) {
    LOG(ERROR) << "PERF_SAMPLE_STACK_USER is not yet supported.";
    return GetWrittenSize();
  }

  /*
   * 	{ union perf_sample_weight
   *	 {
   *		u64		full; && PERF_SAMPLE_WEIGHT
   *	#if defined(__LITTLE_ENDIAN_BITFIELD)
   *		struct {
   *			u32	var1_dw;
   *			u16	var2_w;
   *			u16	var3_w;
   *		} && PERF_SAMPLE_WEIGHT_STRUCT
   *	#elif defined(__BIG_ENDIAN_BITFIELD)
   *		struct {
   *			u16	var3_w;
   *			u16	var2_w;
   *			u32	var1_dw;
   *		} && PERF_SAMPLE_WEIGHT_STRUCT
   *	#endif
   *	 }
   *	}
   */
  if (sample_fields & PERF_SAMPLE_WEIGHT ||
      sample_fields & PERF_SAMPLE_WEIGHT_STRUCT) {
    // For PERF_SAMPLE_WEIGHT_STRUCT write all three values at once.
    WriteData(sample.weight.full);
  }

  // { u64                   data_src; } && PERF_SAMPLE_DATA_SRC
  if (sample_fields & PERF_SAMPLE_DATA_SRC) {
    WriteData(sample.data_src);
  }

  // { u64                   transaction; } && PERF_SAMPLE_TRANSACTION
  if (sample_fields & PERF_SAMPLE_TRANSACTION) {
    WriteData(sample.transaction);
  }

  // { u64                   abi; # enum perf_sample_regs_abi
  //   u64                   regs[weight(mask)]; } && PERF_SAMPLE_REGS_INTR
  if (sample_fields & PERF_SAMPLE_REGS_INTR) {
    LOG(ERROR) << "PERF_SAMPLE_REGS_INTR is not yet supported.";
    return GetWrittenSize();
  }

  // { u64                   phys_addr; } && PERF_SAMPLE_PHYS_ADDR
  if (sample_fields & PERF_SAMPLE_PHYS_ADDR) {
    WriteData(sample.physical_addr);
  }

  // { u64                   cgroup; } && PERF_SAMPLE_CGROUP
  if (sample_fields & PERF_SAMPLE_CGROUP) {
    WriteData(sample.cgroup);
  }

  // { u64                   data_page_size; } && PERF_SAMPLE_DATA_PAGE_SIZE
  if (sample_fields & PERF_SAMPLE_DATA_PAGE_SIZE) {
    WriteData(sample.data_page_size);
  }

  // { u64                   code_page_size; } && PERF_SAMPLE_CODE_PAGE_SIZE
  if (sample_fields & PERF_SAMPLE_CODE_PAGE_SIZE) {
    WriteData(sample.code_page_size);
  }

  return GetWrittenSize();
}

}  // namespace

bool SampleInfoReader::IsSupportedEventType(uint32_t type) {
  switch (type) {
    case PERF_RECORD_SAMPLE:
    case PERF_RECORD_MMAP:
    case PERF_RECORD_MMAP2:
    case PERF_RECORD_FORK:
    case PERF_RECORD_EXIT:
    case PERF_RECORD_COMM:
    case PERF_RECORD_LOST:
    case PERF_RECORD_THROTTLE:
    case PERF_RECORD_UNTHROTTLE:
    case PERF_RECORD_AUX:
    case PERF_RECORD_ITRACE_START:
    case PERF_RECORD_LOST_SAMPLES:
    case PERF_RECORD_SWITCH:
    case PERF_RECORD_SWITCH_CPU_WIDE:
    case PERF_RECORD_NAMESPACES:
    case PERF_RECORD_CGROUP:
      return true;
  }
  return false;
}

bool SampleInfoReader::ReadPerfSampleInfo(const event_t& event,
                                          struct perf_sample* sample) const {
  CHECK(sample);

  if (!SampleInfoReader::IsSupportedEventType(event.header.type)) {
    LOG(ERROR) << "Unsupported event " << GetEventName(event.header.type);
    return false;
  }

  size_t size_read_or_skipped = 0;
  if (!ReadPerfSampleFromData(event, event_attr_, read_cross_endian_, sample,
                              &size_read_or_skipped)) {
    return false;
  }

  if (size_read_or_skipped != event.header.size) {
    LOG(ERROR) << "Read/skipped " << size_read_or_skipped << " bytes, expected "
               << event.header.size << " bytes.";
  }

  return (size_read_or_skipped == event.header.size);
}

bool SampleInfoReader::WritePerfSampleInfo(const perf_sample& sample,
                                           event_t* event) const {
  CHECK(event);

  if (!SampleInfoReader::IsSupportedEventType(event->header.type)) {
    LOG(ERROR) << "Unsupported event " << GetEventName(event->header.type);
    return false;
  }

  size_t offset = GetEventDataSize(*event);
  if (offset == 0) {
    LOG(ERROR) << "Couldn't get event data size for event "
               << GetEventName(event->header.type);
    return false;
  }

  PerfSampleDataWriter writer(
      /*array=*/reinterpret_cast<uint64_t*>(event) + offset / sizeof(uint64_t),
      /*skip_write=*/false);

  size_t size_written_or_skipped =
      offset + writer.Write(sample, event_attr_, event->header.type);

  if (size_written_or_skipped != event->header.size) {
    LOG(ERROR) << "Wrote/skipped " << size_written_or_skipped
               << " bytes, expected " << event->header.size << " bytes.";
  }

  return (size_written_or_skipped == event->header.size);
}

size_t SampleInfoReader::GetPerfSampleDataSize(const perf_sample& sample,
                                               uint32_t event_type) const {
  PerfSampleDataWriter writer(/*array=*/nullptr, /*skip_write=*/true);

  return writer.Write(sample, event_attr_, event_type);
}

// static
uint64_t SampleInfoReader::GetSampleFieldsForEventType(uint32_t event_type,
                                                       uint64_t sample_type) {
  uint64_t mask = UINT64_MAX;
  switch (event_type) {
    case PERF_RECORD_MMAP:
    case PERF_RECORD_LOST:
    case PERF_RECORD_COMM:
    case PERF_RECORD_EXIT:
    case PERF_RECORD_THROTTLE:
    case PERF_RECORD_UNTHROTTLE:
    case PERF_RECORD_FORK:
    case PERF_RECORD_MMAP2:
    case PERF_RECORD_AUX:
    case PERF_RECORD_ITRACE_START:
    case PERF_RECORD_LOST_SAMPLES:
    case PERF_RECORD_SWITCH:
    case PERF_RECORD_SWITCH_CPU_WIDE:
    case PERF_RECORD_NAMESPACES:
    case PERF_RECORD_CGROUP:
      // See perf_event.h "struct" sample_id and sample_id_all.
      mask = PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_ID |
             PERF_SAMPLE_STREAM_ID | PERF_SAMPLE_CPU | PERF_SAMPLE_IDENTIFIER;
      break;
    case PERF_RECORD_SAMPLE:
      break;
    default:
      LOG(FATAL) << "Unknown event " << GetEventName(event_type);
  }
  return sample_type & mask;
}

}  // namespace quipper
