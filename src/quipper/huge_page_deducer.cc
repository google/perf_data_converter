// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "huge_page_deducer.h"

#include <sys/mman.h>

#include <limits>

#include "base/logging.h"
#include "perf_data_utils.h"

using PerfEvent = quipper::PerfDataProto::PerfEvent;
using MMapEvent = quipper::PerfDataProto::MMapEvent;

namespace quipper {
namespace {

const char kAnonFilename[] = "//anon";
const char kAnonHugepageFilename[] = "/anon_hugepage";
const char kAnonHugepageDeletedFilename[] = "/anon_hugepage (deleted)";

bool IsAnon(const MMapEvent& event) {
  bool is_anon = (event.filename() == kAnonFilename ||
                  event.filename() == kAnonHugepageFilename ||
                  event.filename() == kAnonHugepageDeletedFilename);
  if (is_anon && event.pgoff() != 0) {
    LOG(WARNING) << "//anon should have offset=0 for mmap"
                 << event.ShortDebugString();
  }
  return is_anon;
}

// HasExecuteProtection returns if the given |mmap| has the execute protection.
bool HasExecuteProtection(const MMapEvent& mmap) {
  return (mmap.prot() & PROT_EXEC) != 0;
}

// IsVmaContiguous returns true if mmap |a| is immediately followed by |b|
// within a process' address space.
bool IsVmaContiguous(const MMapEvent& a, const MMapEvent& b) {
  return a.pid() == b.pid() && (a.start() + a.len()) == b.start();
}

// Does mmap look like it comes from a huge page source?  Note: this will return
// true for sufficiently large anonymous JIT caches. It does not return true if
// the huge page has already been deduced by perf.
// TODO(b/73721724): unify approaches to huge pages in Google 3.
bool IsHugePage(const MMapEvent& mmap) {
  if (IsAnon(mmap)) {
    // A transparent huge page.
    return true;
  }
  // TODO(irogers): filter pages that can't be huge because of size?
  // Filesystem backed huge pages encode the build id in the form:
  // <filesystem path>/<prefix>.buildid_<hash>
  const char kBuildIdStr[] = ".buildid_";
  size_t file_start = mmap.filename().rfind('/');
  if (file_start == string::npos) {
    return false;
  }
  size_t buildid_start = mmap.filename().find(kBuildIdStr, file_start);
  if (buildid_start == string::npos) {
    return false;
  }
  return mmap.filename().length() > buildid_start + strlen(kBuildIdStr);
}

// IsFileContiguous returns true if the mmap offset of |a| is immediately
// followed by |b|, or if |a| is file backed and |b| is an anonymous mapping,
// and therefore the file offset is meaningless for it. We don't combine
// anonymous mappings followed by file backed mappings, since they don't
// correspond to any known segment splitting scenarios that we handle.
bool IsFileContiguous(const MMapEvent& a, const MMapEvent& b) {
  // Huge page mappings are identified by DeduceHugePages and their backing
  // file names and file offsets are adjusted at that point, so they fall under
  // the first condition below. Data segments that include initialized and
  // uninitialized data are split into contiguous file backed and anonymous
  // mappings, and they are captured by the last condition below. We explicitly
  // disallow the execute protection for this case to make the test more narrow.
  return ((a.pgoff() + a.len()) == b.pgoff() && !IsAnon(a)) ||
         (!HasExecuteProtection(a) && !IsAnon(a) && IsAnon(b));
}

// IsEquivalentFile returns true iff |a| and |b| have the same name, or if
// either of them are anonymous memory (and thus likely to be a --hugepage_text
// version of the same file).
bool IsEquivalentFile(const MMapEvent& a, const MMapEvent& b) {
  // perf attributes neighboring anonymous mappings under the argv[0]
  // filename rather than "//anon", so check filename equality, as well as
  // anonymous.
  return a.filename() == b.filename() || IsHugePage(a) || IsHugePage(b);
}

// IsEquivalentProtection returns true iff |a| and |b| have the same protection
// mask. It's also possible for a RW segment to be split into a read-only part
// and a RW part. Assume such cases don't have the executable bit set, and have
// the same sharing flags (shared vs. private).
bool IsEquivalentProtection(const MMapEvent& a, const MMapEvent& b) {
  constexpr uint32_t ro_prot = PROT_READ;
  constexpr uint32_t rw_prot = PROT_READ | PROT_WRITE;
  // The flags have multiple bits of information, but we compare only the map
  // type, i.e. shared or private.
  uint32_t a_type = a.flags();
  uint32_t b_type = b.flags();
#ifdef MAP_TYPE
  a_type &= MAP_TYPE;
  b_type &= MAP_TYPE;
#endif
  // Don't match sharing flags for executable mappings, because some uses of
  // hugepage text, e.g. the hugetlbfs flavor, don't preserve the sharing flags
  // of the original file backed mapping.
  return (a_type == b_type || HasExecuteProtection(a)) &&
         (a.prot() == b.prot() ||
          (a.prot() == ro_prot && b.prot() == rw_prot) ||
          (a.prot() == rw_prot && b.prot() == ro_prot));
}

// Returns if the file associated with the given mapping is combinable. For
// example, device files are special files, so we shouldn't try to combine
// mappings associated with such files.
bool IsCombinableFile(const MMapEvent& mmap) {
  if (mmap.filename().rfind("/dev/", 0) == 0) {
    return false;
  }
  return true;
}

// Helper to correctly update a filename on a PerfEvent that contains an
// MMapEvent.
void SetMmapFilename(PerfEvent* event, const string& new_filename,
                     uint64_t new_filename_md5_prefix) {
  CHECK(event->has_mmap_event());
  event->mutable_header()->set_size(
      event->header().size() +
      GetUint64AlignedStringLength(new_filename.size()) -
      GetUint64AlignedStringLength(event->mmap_event().filename().size()));

  event->mutable_mmap_event()->set_filename(new_filename);
  event->mutable_mmap_event()->set_filename_md5_prefix(new_filename_md5_prefix);
}
}  // namespace

namespace {

// A map from PID to ranges of mmap events associated with the PID.
class PerPidMMapEventRange {
 public:
  using event_itr_t = RepeatedPtrField<PerfEvent>::iterator;
  using key_t = int32_t;
  using value_t = std::pair<event_itr_t, event_itr_t>;
  using container = std::unordered_map<key_t, value_t>;

  // Abstracts a range of mmap events between [begin, end).
  class MMapEventIterator {
   public:
    MMapEventIterator(key_t pid, event_itr_t begin, event_itr_t end)
        : itr_(begin), end_(end), pid_(pid), has_events_(true) {}
    MMapEventIterator() : has_events_(false) {}

    bool operator==(const MMapEventIterator& x) const {
      if (!has_events_) {
        return !x.has_events_;
      }
      if (!x.has_events_) {
        return false;
      }
      assert(pid_ == x.pid_);
      return itr_ == x.itr_;
    }

    bool operator!=(const MMapEventIterator& x) const { return !(*this == x); }

    PerfEvent* operator*() const { return &*itr_; }

    PerfEvent* operator->() const { return &*itr_; }

    MMapEventIterator& operator++() {
      for (++itr_; itr_ != end_; ++itr_) {
        if (!itr_->has_mmap_event()) {
          continue;
        }
        key_t itr_pid = -1;
        if (itr_->mmap_event().has_pid()) {
          itr_pid = itr_->mmap_event().pid();
        }
        if (itr_pid == pid_) {
          // Found next matching mmap_event for pid.
          return *this;
        }
      }
      has_events_ = false;
      return *this;
    }

   private:
    // Current iterator position.
    event_itr_t itr_;
    // 1 beyond the last event in this range.
    event_itr_t end_;
    // PID of events this iterator is going through.
    key_t pid_;
    // Cleared when the iterator runs out of events.
    bool has_events_;
  };

  class MMapEventsForPid {
   public:
    MMapEventsForPid(container::const_iterator itr) : itr_(itr) {}

    MMapEventIterator begin() const {
      return MMapEventIterator(itr_->first, itr_->second.first,
                               itr_->second.second);
    }
    MMapEventIterator end() const { return MMapEventIterator(); }

    const MMapEventsForPid& operator*() const { return *this; }

    bool operator==(const MMapEventsForPid& x) const { return itr_ == x.itr_; }

    bool operator!=(const MMapEventsForPid& x) const { return !(*this == x); }

    MMapEventsForPid& operator++() {
      ++itr_;
      return *this;
    }

   private:
    container::const_iterator itr_;
  };

  PerPidMMapEventRange(RepeatedPtrField<PerfEvent>* events) {
    auto cur = events->begin();
    auto end = events->end();
    for (; cur != end; ++cur) {
      if (!cur->has_mmap_event()) {
        continue;
      }
      // Skip dynamic mmap() events. Hugepage deduction only works on mmaps as
      // synthesized by perf from /proc/${pid}/maps, which have timestamp==0.
      // Support for deducing hugepages from a sequence of mmap()/mremap() calls
      // would require additional deduction logic.
      if (cur->timestamp() != 0) {
        continue;
      }
      key_t pid = -1;
      if (cur->mmap_event().has_pid()) {
        pid = cur->mmap_event().pid();
      }
      auto entry = map_.find(pid);
      if (entry == map_.end()) {
        map_[pid] = {cur, cur + 1};
      } else {
        entry->second.second = cur + 1;
      }
    }
  }

  MMapEventsForPid begin() const { return MMapEventsForPid(map_.begin()); }

  MMapEventsForPid end() const { return MMapEventsForPid(map_.end()); }

 private:
  container map_;
};

void UpdateRangeFromNext(PerPidMMapEventRange::MMapEventIterator first,
                         PerPidMMapEventRange::MMapEventIterator last,
                         PerPidMMapEventRange::MMapEventIterator end,
                         const MMapEvent& next) {
  const uint64_t range_length = last->mmap_event().start() -
                                first->mmap_event().start() +
                                last->mmap_event().len();
  const uint64 start_pgoff = next.pgoff() - range_length;
  uint64 pgoff = start_pgoff;
  for (; first != end; ++first) {
    PerfEvent* event = *first;
    MMapEvent* mmap = event->mutable_mmap_event();
    // As perf will rename huge pages to the executable name but not update the
    // pgoff, treat any offset of 0 as a huge page.
    const bool is_hugepage = IsHugePage(*mmap) || mmap->pgoff() == 0;
    // Replace pages mapped from a huge page source with a regular name if
    // possible.
    if (is_hugepage) {
      SetMmapFilename(event, next.filename(), next.filename_md5_prefix());
      if (mmap->pgoff() == 0) {
        // Correct anonymous page file offsets.
        mmap->set_pgoff(pgoff);
      }
      // Correct other fields where mmap is associated with a huge page text
      // source.
      if (next.has_maj()) {
        mmap->set_maj(next.maj());
      }
      if (next.has_min()) {
        mmap->set_min(next.min());
      }
      if (next.has_ino()) {
        mmap->set_ino(next.ino());
      }
      if (next.has_ino_generation()) {
        mmap->set_ino_generation(next.ino_generation());
      }
    }
    pgoff += mmap->len();
  }
  CHECK_EQ(pgoff, start_pgoff + range_length);
}

}  // namespace

void DeduceHugePages(RepeatedPtrField<PerfEvent>* events) {
  PerPidMMapEventRange ranges(events);

  for (auto mmap_range : ranges) {
    // mmap_range is a set of mmap events associated with a PID. We care here
    // just about fixing huge pages to look like regular pages from a file for a
    // PID.

    using iterator = PerPidMMapEventRange::MMapEventIterator;
    // Assigned the start of a set of huge mmapped pages.
    iterator huge_mmap_range_first = mmap_range.end();
    // Assigned the last of a set of huge mmapped pages.
    iterator huge_mmap_range_last = mmap_range.end();
    // Assigned to the last non-mmap entry.
    iterator pre_mmap_range_last = mmap_range.end();
    for (auto itr = mmap_range.begin(); itr != mmap_range.end(); ++itr) {
      const auto& cur_mmap = itr->mmap_event();
      if (IsHugePage(cur_mmap)) {
        if (huge_mmap_range_first == mmap_range.end()) {
          // New range.
          huge_mmap_range_first = itr;
          huge_mmap_range_last = itr;
        } else {
          const auto& mmap_range_last = huge_mmap_range_last->mmap_event();
          if (IsVmaContiguous(mmap_range_last, cur_mmap) &&
              mmap_range_last.filename() == cur_mmap.filename() &&
              (IsFileContiguous(mmap_range_last, cur_mmap) ||
               (mmap_range_last.pgoff() == 0 && cur_mmap.pgoff() == 0))) {
            // Ranges match exactly: //anon,//anon, or file,file; If they use
            // different names, then deduction needs to consider them
            // independently.
            huge_mmap_range_last = itr;
          } else {
            // Discontiguous range, start a new range.
            huge_mmap_range_first = itr;
            huge_mmap_range_last = itr;
            pre_mmap_range_last = mmap_range.end();
          }
        }
      } else {
        if (huge_mmap_range_first != mmap_range.end()) {
          const auto& mmap_range_first = huge_mmap_range_first->mmap_event();
          const auto& mmap_range_last = huge_mmap_range_last->mmap_event();
          // Not a huge page but there's a pending range to process.
          uint64_t huge_mmap_range_length = mmap_range_last.start() -
                                            mmap_range_first.start() +
                                            mmap_range_last.len();
          uint64 start_pgoff = 0;
          if (pre_mmap_range_last != mmap_range.end()) {
            const auto& pre_mmap = pre_mmap_range_last->mmap_event();
            if (IsVmaContiguous(pre_mmap, mmap_range_first) &&
                IsEquivalentFile(pre_mmap, mmap_range_first) &&
                IsEquivalentFile(pre_mmap, cur_mmap)) {
              start_pgoff = pre_mmap.pgoff() + pre_mmap.len();
            }
          }
          if (IsVmaContiguous(mmap_range_last, cur_mmap) &&
              IsEquivalentFile(mmap_range_last, cur_mmap) &&
              cur_mmap.pgoff() >= huge_mmap_range_length &&
              cur_mmap.pgoff() - huge_mmap_range_length == start_pgoff) {
            UpdateRangeFromNext(huge_mmap_range_first, huge_mmap_range_last,
                                itr, cur_mmap);
          }
          huge_mmap_range_first = mmap_range.end();
          huge_mmap_range_last = mmap_range.end();
        }
        pre_mmap_range_last = itr;
      }
    }
  }
}

void CombineMappings(RepeatedPtrField<PerfEvent>* events) {
  // Combine mappings
  RepeatedPtrField<PerfEvent> new_events;
  new_events.Reserve(events->size());
  std::unordered_map<int32_t, int> pid_to_prev_map;

  // |prev| is the index of the last mmap_event in |new_events| (or
  // |new_events.size()| if no mmap_events have been inserted yet).
  for (int i = 0; i < events->size(); ++i) {
    PerfEvent* event = events->Mutable(i);
    bool should_merge = false;

    const MMapEvent* mmap = nullptr;
    MMapEvent* prev_mmap = nullptr;
    PerfEvent* prev_mmap_event = nullptr;
    int32_t pid = -1;
    if (event->has_mmap_event()) {
      mmap = &event->mmap_event();
      pid = mmap->has_pid() ? mmap->pid() : -1;
      auto itr = pid_to_prev_map.find(pid);
      should_merge = itr != pid_to_prev_map.end();
      if (should_merge) {
        prev_mmap_event = new_events.Mutable(itr->second);
        prev_mmap = prev_mmap_event->mutable_mmap_event();
      }
    }

    // TODO(b/169891636):  For hugetlbfs-backed files, We should verify that the
    // build IDs match as well.
    should_merge = should_merge && IsCombinableFile(*mmap) &&
                   IsCombinableFile(*prev_mmap) &&
                   IsEquivalentFile(*prev_mmap, *mmap) &&
                   IsEquivalentProtection(*prev_mmap, *mmap) &&
                   IsFileContiguous(*prev_mmap, *mmap) &&
                   IsVmaContiguous(*prev_mmap, *mmap);
    if (should_merge) {
      if (IsHugePage(*prev_mmap) && !IsHugePage(*mmap)) {
        // Update the file offset and filename for the anon mapping.
        prev_mmap->set_pgoff(mmap->pgoff() - prev_mmap->len());
        SetMmapFilename(prev_mmap_event, mmap->filename(),
                        mmap->filename_md5_prefix());
      }
      // Combine the lengths of the two mappings.
      prev_mmap->set_len(prev_mmap->len() + mmap->len());
    } else {
      // Remember the last mmap event for a PID.
      if (mmap != nullptr) {
        pid_to_prev_map[pid] = new_events.size();
      }
      new_events.Add()->Swap(event);
    }
  }

  events->Swap(&new_events);
}

}  // namespace quipper
