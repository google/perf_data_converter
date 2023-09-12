#include "arm_spe_decoder.h"

#include <cstdint>
#include <cstring>
#include <ios>
#include <string_view>

#include "base/logging.h"
#include "binary_data_utils.h"

namespace quipper {

namespace {

// Mask(11, 4) = 0x00 00 00 00 00 00 0f f0
constexpr uint64_t Mask(uint64_t h, uint64_t l) {
  return ((~0ULL) << l) & (~0ULL >> (64 - 1 - h));
}
// Bit(4) = 0x00 00 00 00 00 00 00 10
constexpr uint64_t Bit(uint64_t n) { return 1UL << n; }

// Mask for event packet header & data source packet header
constexpr uint64_t HdrMaskEvSrc() { return Mask(7, 6) | Mask(3, 0); }
// Mask for context packet header & operation packet header & checking extended
// header
constexpr uint64_t HdrMaskCtxOpExt() { return Mask(7, 2); }
// Mask for address header & counter header
constexpr uint64_t HdrMaskAddrCtr() { return Mask(7, 3); }

// Header index
constexpr uint64_t HdrIndex(uint64_t hdr) { return hdr & Mask(2, 0); }
constexpr uint64_t HdrExtendedIndex(uint64_t hdr0, uint64_t hdr1) {
  return (hdr0 & Mask(1, 0)) << 3 | HdrIndex(hdr1);
}

// Address packet header
const uint64_t kAddrPktHdrIndexIns = 0x0;
const uint64_t kAddrPktHdrIndexBr = 0x1;
const uint64_t kAddrPktHdrIndexDataVirt = 0x2;
const uint64_t kAddrPktHdrIndexDataPhys = 0x3;
const uint64_t kAddrPktHdrIndexPrevBr = 0x4;

// Returns the payload size according to the given header.
inline size_t GetPayloadSize(uint8_t header) {
  return 1U << ((header & Mask(5, 4)) >> 4);
}

}  // namespace

ArmSpeDecoder::ArmSpeDecoder(std::string_view buf, bool is_cross_endian)
    : buf_(buf),
      buf_i_(0),
      seen_pk_idx_mask_(0),
      is_cross_endian_(is_cross_endian) {}

bool ArmSpeDecoder::NextRecord(struct Record* ret_record) {
  if (ret_record == nullptr || buf_i_ >= buf_.size()) {
    return false;
  }

  Record record{};
  // One record consists of several packets. So loop till finding the end-type
  // packet or meeting the end of the trace.
  for (Packet p{}; !p.is_end_type && buf_i_ < buf_.size(); buf_i_ += p.size) {
    p = Packet{};
    p.header = buf_[buf_i_];

    // Padding packet
    if (p.header == 0x0) {
      if (!HandlePacketPadding(&p)) {
        return false;
      }
      continue;
    }

    // End packet
    if (p.header == 0x1) {
      if (!HandlePacketEnd(&p)) {
        return false;
      }
      continue;
    }

    // Timestamp packet
    if (p.header == 0x71) {
      if (!HandlePacketTimestamp(&p, &record)) {
        return false;
      }
      continue;
    }

    // Event packet
    if ((p.header & HdrMaskEvSrc()) == 0x42) {
      if (!HandlePacketEvent(&p, &record)) {
        return false;
      }
      continue;
    }

    // Data source packet
    if ((p.header & HdrMaskEvSrc()) == 0x43) {
      if (!HandlePacketDataSource(&p, &record)) {
        return false;
      }
      continue;
    }

    // Context packet
    if ((p.header & HdrMaskCtxOpExt()) == 0x64) {
      if (!HandlePacketContext(&p, &record)) {
        return false;
      }
      continue;
    }

    // Operation packet
    if ((p.header & HdrMaskCtxOpExt()) == 0x48) {
      if (!HandlePacketOperation(&p, &record)) {
        return false;
      }
      continue;
    }

    // Check if the header is an extended header, it has effect to later types
    // of packets.
    if ((p.header & HdrMaskCtxOpExt()) == 0x20) {
      if (buf_.size() - buf_i_ == 1) {
        LOG(ERROR) << "Bad binary trace for extended header";
        return false;
      }

      p.ext_header = buf_[buf_i_ + 1];
      if (p.ext_header == 0x0) {
        // If the extended header is empty, then it is due to alignment, move on
        // to next packet.
        unsigned int alignment = 1 << ((p.header & 0xf) + 1);

        if (buf_.size() - buf_i_ < alignment) {
          LOG(ERROR) << "Binary trace needs more bytes for extended header";
          return false;
        }

        p.size =
            alignment - (((uintptr_t)(buf_.data() + buf_i_)) & (alignment - 1));
        continue;
      }
    }

    // Address packet
    if ((GetCorrectHeader(p) & HdrMaskAddrCtr()) == 0xb0) {
      if (!HandlePacketAddress(&p, &record)) {
        return false;
      }
      continue;
    }

    // Counter packet.
    if ((GetCorrectHeader(p) & HdrMaskAddrCtr()) == 0x98) {
      if (!HandlePacketCounter(&p, &record)) {
        return false;
      }
      continue;
    }

    // Reaching here means the header does not match any known packet. So report
    // an error.
    LOG(ERROR) << "Unknown SPE packet header " << std::hex << (int)p.header
               << std::dec;
    return false;
  }

  *ret_record = record;
  return true;
}

bool ArmSpeDecoder::HandlePacketPadding(struct Packet* p) {
  p->size = 1;
  return true;
}

bool ArmSpeDecoder::HandlePacketEnd(struct Packet* p) {
  p->size = 1;
  p->is_end_type = true;
  return true;
}

bool ArmSpeDecoder::HandlePacketTimestamp(struct Packet* p, struct Record* r) {
  if (!SetPayloadAndSize(p)) {
    LOG(ERROR) << "Error parsing timestamp packet.";
    return false;
  }
  r->timestamp = p->payload;
  p->is_end_type = true;
  return true;
}

bool ArmSpeDecoder::HandlePacketEvent(struct Packet* p, struct Record* r) {
  if (!SetPayloadAndSize(p)) {
    LOG(ERROR) << "Error parsing event packet.";
    return false;
  }

  r->event.gen_exception = (p->payload & Bit(0)) != 0;
  r->event.retired = (p->payload & Bit(1)) != 0;
  r->event.l1d_access = (p->payload & Bit(2)) != 0;
  r->event.l1d_refill = (p->payload & Bit(3)) != 0;
  r->event.tlb_access = (p->payload & Bit(4)) != 0;
  r->event.tlb_walk = (p->payload & Bit(5)) != 0;
  r->event.cond_not_taken = (p->payload & Bit(6)) != 0;
  r->event.br_mis_pred = (p->payload & Bit(7)) != 0;
  r->event.llc_access = (p->payload & Bit(8)) != 0;
  r->event.llc_miss = (p->payload & Bit(9)) != 0;
  r->event.remote_access = (p->payload & Bit(10)) != 0;
  r->event.ldst_alignment = (p->payload & Bit(11)) != 0;
  r->event.sve_partial_pred = (p->payload & Bit(17)) != 0;
  r->event.sve_empty_pred = (p->payload & Bit(18)) != 0;
  return true;
}

bool ArmSpeDecoder::HandlePacketDataSource(struct Packet* p, struct Record* r) {
  if (!SetPayloadAndSize(p)) {
    LOG(ERROR) << "Error parsing source packet.";
    return false;
  }
  r->source = p->payload;
  return true;
}

bool ArmSpeDecoder::HandlePacketContext(struct Packet* p, struct Record* r) {
  if (!SetPayloadAndSize(p)) {
    LOG(ERROR) << "Error parsing context packet.";
    return false;
  }

  r->context.id = p->payload;
  r->context.el1 = (p->header & Mask(1, 0)) == 0x0;
  r->context.el2 = (p->header & Mask(1, 0)) == 0x1;
  return true;
}

bool ArmSpeDecoder::HandlePacketOperation(struct Packet* p, struct Record* r) {
  if (!SetPayloadAndSize(p)) {
    LOG(ERROR) << "Error parsing op packet.";
    return false;
  }

  uint64_t cls = p->header & Mask(1, 0);
  switch (cls) {
    // Other
    case 0x0: {
      r->op.is_other = true;
      r->op.other.cond = (p->payload & Bit(0)) != 0;
      r->op.other.sve = (p->payload & (Bit(7) | Bit(3) | Bit(0))) == 0x8;
      r->op.other.sve_pred = (p->payload & Bit(2)) != 0;
      break;
    }
    // Load or store
    case 0x1: {
      r->op.is_ldst = true;

      r->op.ldst.st = (p->payload & Bit(0)) != 0;
      r->op.ldst.ld = !r->op.ldst.st;

      r->op.ldst.gp_reg = (p->payload & Mask(7, 1)) == 0x0;
      r->op.ldst.atomic = (p->payload & (Mask(7, 5) | Bit(1))) == 0x2;
      if (r->op.ldst.atomic) {
        r->op.ldst.atomic_at = (p->payload & Bit(2)) != 0;
        r->op.ldst.atomic_excl = (p->payload & Bit(3)) != 0;
        r->op.ldst.atomic_ar = (p->payload & Bit(4)) != 0;
      }
      r->op.ldst.simd_fp = (p->payload & Mask(7, 1)) == 0x4;
      r->op.ldst.sve = (p->payload & (Bit(3) | Bit(1))) == 0x8;
      r->op.ldst.unsp_reg = (p->payload & Mask(7, 1)) == 0x10;
      r->op.ldst.mrs = (p->payload & Mask(7, 1)) == 0x30;
      r->op.ldst.alloc_tag = (p->payload & Mask(7, 1)) == 0x12;
      r->op.ldst.memcpy = (p->payload & Mask(7, 1)) == 0x20;
      r->op.ldst.memset = (p->payload & Mask(7, 0)) == 0x25;

      break;
    }
    // Branch or exception return
    case 0x2: {
      r->op.is_br_eret = true;
      r->op.br_eret.br_cond = (p->payload & Bit(0)) == 0x1;
      r->op.br_eret.br_indirect = (p->payload & Mask(7, 1)) == 0x2;
      break;
    }
    default: {
      LOG(ERROR) << "Op packet error, invalid class " << std::hex << cls
                 << std::dec;
      return false;
    }
  }

  return true;
}

bool ArmSpeDecoder::HandlePacketAddress(struct Packet* p, struct Record* r) {
  if (!SetPayloadAndSize(p)) {
    LOG(ERROR) << "Error parsing address packet.";
    return false;
  }

  uint64_t index = p->ext_header != 0x0
                       ? HdrExtendedIndex(buf_[buf_i_], buf_[buf_i_ + 1])
                       : HdrIndex(buf_[buf_i_]);

  if (index == kAddrPktHdrIndexIns || index == kAddrPktHdrIndexBr ||
      index == kAddrPktHdrIndexPrevBr) {
    // Instruction virtual address or branch target address
    RecordIP ip{};
    ip.el = (p->payload & Mask(62, 61)) >> 61;
    ip.ns = (p->payload & Bit(63)) >> 63;
    // Fill highest byte for EL1 or EL2 (VHE) mode
    if (ip.ns && (ip.el == 1 || ip.el == 2)) {
      ip.addr = p->payload | (0xffULL << 56);
    } else {
      // Clear the highest byte
      ip.addr = p->payload & Mask(55, 0);
    }

    if (index == kAddrPktHdrIndexIns) {
      r->ip = ip;
    } else if (index == kAddrPktHdrIndexBr) {
      r->tgt_br_ip = ip;
    } else if (index == kAddrPktHdrIndexPrevBr) {
      r->prev_br_ip = ip;
    }
  } else if (index == kAddrPktHdrIndexDataVirt) {
    // Data access virtual address
    uint64_t value = (p->payload & Mask(55, 0)) >> 48;
    if ((value & 0xf0ULL) == 0xf0ULL) {
      r->virt.addr = p->payload | (0xffULL << 56);
    } else {
      r->virt.addr = p->payload & Mask(55, 0);
    }
    r->virt.tag = (p->payload & Mask(63, 56)) >> 56;
  } else if (index == kAddrPktHdrIndexDataPhys) {
    // Data access physical address
    r->phys.addr = p->payload & Mask(55, 0);
    r->phys.ns = ((p->payload & Bit(63)) >> 63);
    r->phys.ch = ((p->payload & Bit(62)) >> 62);
    r->phys.pat = ((p->payload & Mask(59, 56)) >> 56);
  } else if ((seen_pk_idx_mask_ & Bit(index)) == 0) {
    seen_pk_idx_mask_ |= Bit(index);
    LOG(WARNING) << "ignoring unsupported address packet index: " << std::hex
                 << index << std::dec;
  }
  return true;
}

bool ArmSpeDecoder::HandlePacketCounter(struct Packet* p, struct Record* r) {
  if (!SetPayloadAndSize(p)) {
    LOG(ERROR) << "Error parsing counter packet.";
    return false;
  }

  uint64_t index = p->ext_header != 0x0
                       ? HdrExtendedIndex(buf_[buf_i_], buf_[buf_i_ + 1])
                       : HdrIndex(buf_[buf_i_]);

  switch (index) {
    case 0x0: {
      r->total_lat = p->payload;
      break;
    }
    case 0x1: {
      r->issue_lat = p->payload;
      break;
    }
    case 0x2: {
      r->trans_lat = p->payload;
      break;
    }
  }

  return true;
}

bool ArmSpeDecoder::SetPayloadAndSize(struct Packet* p) {
  if (p == nullptr) {
    return false;
  }

  size_t header_size = p->ext_header != 0x0 ? 2 : 1;
  uint64_t payload = 0;
  size_t payload_size = GetPayloadSize(buf_[buf_i_ + header_size - 1]);

  if (buf_.size() - buf_i_ < header_size + payload_size) {
    LOG(WARNING) << "Arm SPE trace does not have enough bytes";
    return false;
  }

  size_t pos = buf_i_ + header_size;
  switch (payload_size) {
    case 1: {
      // No need to swap when there is only one byte.
      payload = *(reinterpret_cast<const uint8_t*>(buf_.data() + pos));
      break;
    }
    case 2: {
      payload = MaybeSwap<uint16_t>(
          *(reinterpret_cast<const uint16_t*>(buf_.data() + pos)),
          is_cross_endian_);
      break;
    }
    case 4: {
      payload = MaybeSwap<uint32_t>(
          *(reinterpret_cast<const uint32_t*>(buf_.data() + pos)),
          is_cross_endian_);
      break;
    }
    case 8: {
      payload = MaybeSwap<uint64_t>(
          *(reinterpret_cast<const uint64_t*>(buf_.data() + pos)),
          is_cross_endian_);
      break;
    }
    default: {
      LOG(ERROR) << "Bad packet payload size " << payload_size;
      return false;
    }
  }

  p->payload = payload;
  p->payload_size = payload_size;
  p->size = header_size + p->payload_size;
  return true;
}

uint8_t ArmSpeDecoder::GetCorrectHeader(const struct Packet& p) {
  return p.ext_header == 0x0 ? p.header : p.ext_header;
}

}  // namespace quipper
