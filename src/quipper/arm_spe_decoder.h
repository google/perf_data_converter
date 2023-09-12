#ifndef PERF_DATA_CONVERTER_SRC_QUIPPER_ARM_SPE_DECODER_H_
#define PERF_DATA_CONVERTER_SRC_QUIPPER_ARM_SPE_DECODER_H_

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace quipper {

// Decode SPE records from the given binary trace buffer. The decoder is
// implemented according to the Arm Architecture Reference Manual for A-profile
// architecture (ARM DDI 0487I.a ID081822).
class ArmSpeDecoder {
 public:
  struct RecordEvent {
    bool gen_exception : 1;
    bool retired : 1;
    bool l1d_access : 1;
    bool l1d_refill : 1;
    bool tlb_access : 1;
    bool tlb_walk : 1;
    // cond_not_taken: A conditional instruction that failed its condition code
    //   check. This includes conditional branches, compare-and-branch,
    //   conditional select, and conditional compares.
    bool cond_not_taken : 1;
    bool br_mis_pred : 1;
    bool llc_access : 1;
    bool llc_miss : 1;
    bool remote_access : 1;
    bool ldst_alignment : 1;
    bool sve_partial_pred : 1;
    bool sve_empty_pred : 1;
  };

  struct RecordOp {
    // Op type: Other
    bool is_other : 1;
    struct {
      // Conditional select or conditional compare operation
      bool cond : 1;
      bool sve : 1;
      bool sve_pred : 1;
    } other;
    // Op type: Load, store, or atomic
    bool is_ldst : 1;
    struct {
      bool ld : 1;
      bool st : 1;
      bool gp_reg : 1;
      bool atomic : 1;
      bool atomic_at : 1;
      bool atomic_excl : 1;
      bool atomic_ar : 1;
      bool simd_fp : 1;
      bool sve : 1;
      bool unsp_reg : 1;
      bool mrs : 1;
      bool alloc_tag : 1;
      bool memcpy : 1;
      bool memset : 1;
    } ldst;
    // Op type: Branch or exception return
    bool is_br_eret : 1;
    struct {
      bool br_cond : 1;
      bool br_indirect : 1;
    } br_eret;
  };

  // Instruction pointer (i.e. Program counter, or PC)
  struct RecordIP {
    uint64_t addr;
    uint8_t el;
    uint8_t ns;
  };

  struct RecordVA {
    uint64_t addr;
    // The top-byte tag of the virtual address
    uint8_t tag;
  };

  // Physical Address
  struct RecordPA {
    uint64_t addr;
    uint8_t ns;
    // For Arm Memory Tagging Extension (MTE), ch=1 means checked access
    uint8_t ch;
    // For Arm Memory Tagging Extension (MTE), pat: physical address tag
    uint8_t pat;
  };

  // Record context.
  struct RecordContext {
    uint64_t id;
    bool el1;
    bool el2;
  };

  struct Record {
    RecordEvent event;
    RecordOp op;
    uint32_t total_lat;
    uint32_t issue_lat;
    uint32_t trans_lat;
    RecordIP ip;
    RecordIP tgt_br_ip;
    RecordIP prev_br_ip;
    RecordVA virt;
    RecordPA phys;
    uint64_t timestamp;
    RecordContext context;
    uint64_t source;
  };

  ArmSpeDecoder(std::string_view buf, bool is_cross_endian);

  // Sets fields of the given record to the next record parsed from the
  // previously given SPE trace. It will return false if it encounters invalid
  // data or reaching the end of the trace.
  bool NextRecord(struct Record* record);

 private:
  struct Packet {
    uint8_t header;
    uint8_t ext_header;
    uint64_t payload;
    size_t payload_size;  // payload size (in bytes)
    size_t size;          // total packet size (in bytes).
    bool is_end_type;
  };

  // Handlers for various types of packets. Returns true upon success; returns
  // false when meeting an error handling the packet.
  bool HandlePacketPadding(struct Packet* p);
  bool HandlePacketEnd(struct Packet* p);
  bool HandlePacketTimestamp(struct Packet* p, struct Record* r);
  bool HandlePacketEvent(struct Packet* p, struct Record* r);
  bool HandlePacketDataSource(struct Packet* p, struct Record* r);
  bool HandlePacketContext(struct Packet* p, struct Record* r);
  bool HandlePacketOperation(struct Packet* p, struct Record* r);
  bool HandlePacketAddress(struct Packet* p, struct Record* r);
  bool HandlePacketCounter(struct Packet* p, struct Record* r);

  // Sets the payland and the size for the given input packet according to the
  // current trace data at the current buffer location.
  // Returns true upon success. Returns false upon parsing failure.
  bool SetPayloadAndSize(struct Packet* p);

  // Returns the extended header if it is not zero, otherwise returns the normal
  // header.
  uint8_t GetCorrectHeader(const ArmSpeDecoder::Packet& p);

  // The SPE trace buffer. Note that this class doesn't own the buffer.
  std::string_view buf_;

  // Current position of the SPE trace.
  size_t buf_i_;

  // Mask to record the unsupported SPE packet index.
  uint32_t seen_pk_idx_mask_;
  // Represents that if the given SPE trace is encoded in a machine that has
  // different endian than the current one.
  bool is_cross_endian_;
};

}  // namespace quipper

#endif  // PERF_DATA_CONVERTER_SRC_QUIPPER_ARM_SPE_DECODER_H_
