#include "huge_page_deducer.h"

#include <limits>

#include "perf_data_utils.h"

#include "base/logging.h"

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

// IsVmaContiguous returns true if mmap |a| is immediately followed by |b|
// within a process' address space.
bool IsVmaContiguous(const MMapEvent& a, const MMapEvent& b) {
  return a.pid() == b.pid() && (a.start() + a.len()) == b.start();
}

// IsFileContiguous returns true if mmap offset of |a| is immediately followed
// by |b|.
bool IsFileContiguous(const MMapEvent& a, const MMapEvent& b) {
  return (a.pgoff() + a.len()) == b.pgoff();
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

// IsEquivalentFile returns true iff |a| and |b| have the same name, or if
// either of them are anonymous memory (and thus likely to be a --hugepage_text
// version of the same file).
bool IsEquivalentFile(const MMapEvent& a, const MMapEvent& b) {
  // perf attributes neighboring anonymous mappings under the argv[0]
  // filename rather than "//anon", so check filename equality, as well as
  // anonymous.
  return a.filename() == b.filename() || IsHugePage(a) || IsHugePage(b);
}

// Helper to correctly update a filename on a PerfEvent that contains an
// MMapEvent.
void SetMmapFilename(PerfEvent* event, const string& new_filename,
                     uint64_t new_filename_md5_prefix) {
  CHECK(event->has_mmap_event());
  event->mutable_header()->set_size(
      event->header().size() + GetUint64AlignedStringLength(new_filename) -
      GetUint64AlignedStringLength(event->mmap_event().filename()));

  event->mutable_mmap_event()->set_filename(new_filename);
  event->mutable_mmap_event()->set_filename_md5_prefix(new_filename_md5_prefix);
}
}  // namespace

namespace {

// MMapRange represents an index into a PerfEvents sequence that contains
// a contiguous region of mmaps that have all of the same filename and pgoff.
class MMapRange {
 public:
  // Default constructor is an invalid range.
  MMapRange()
      : first_(std::numeric_limits<int>::max()),
        last_(std::numeric_limits<int>::min()) {}

  // Construct a real range.
  MMapRange(int first_index, int last_index)
      : first_(first_index), last_(last_index) {}

  uint64 Len(const RepeatedPtrField<PerfEvent>& events) const {
    auto& first = events.Get(first_).mmap_event();
    auto& last = events.Get(last_).mmap_event();
    return last.start() - first.start() + last.len();
  }

  int FirstIndex() const { return first_; }
  int LastIndex() const { return last_; }
  bool IsValid() const { return first_ <= last_; }

  const MMapEvent& FirstMmap(const RepeatedPtrField<PerfEvent>& events) const {
    return events.Get(first_).mmap_event();
  }

  const MMapEvent& LastMmap(const RepeatedPtrField<PerfEvent>& events) const {
    return events.Get(last_).mmap_event();
  }

 private:
  int first_;
  int last_;
};

// MMapRange version of IsVmaContiguous(MMapEvent, MMapEvent).
bool IsVmaContiguous(const RepeatedPtrField<PerfEvent>& events,
                     const MMapRange& a, const MMapRange& b) {
  return IsVmaContiguous(a.LastMmap(events), b.FirstMmap(events));
}

// MMapRange version of IsIsEquivalent(MMapEvent, MMapEvent).
bool IsEquivalentFile(const RepeatedPtrField<PerfEvent>& events,
                      const MMapRange& a, const MMapRange& b) {
  // Because a range has the same file for all mmaps within it, assume that
  // checking any mmap in |a| with any in |b| is sufficient.
  return IsEquivalentFile(a.LastMmap(events), b.FirstMmap(events));
}

// FindRange returns a maximal MMapRange such that:
//  - all mmap events are from the same PID and filename,
//  - all mmap events are VMA contiguous,
//  - either:
//     - all file offsets are contiguous,
//     - all file offsets are 0. Note, this allows for anonymous pages to be in
//     a maximal range but allows the filename to not be anonymous to work
//     around an issue with perf.
MMapRange FindRange(const RepeatedPtrField<PerfEvent>& events, int start) {
  const MMapEvent* prev_mmap = nullptr;
  MMapRange range;
  bool range_pgoffs_are_all_zero = true;
  for (int i = start; i < events.size(); i++) {
    const PerfEvent& event = events.Get(i);
    // Skip irrelevant events
    if (!event.has_mmap_event()) {
      continue;
    }
    // Skip dynamic mmap() events. Hugepage deduction only works on mmaps as
    // synthesized by perf from /proc/${pid}/maps, which have timestamp==0.
    // Support for deducing hugepages from a sequence of mmap()/mremap() calls
    // would require additional deduction logic.
    if (event.timestamp() != 0) {
      continue;
    }
    const MMapEvent& mmap = events.Get(i).mmap_event();
    range_pgoffs_are_all_zero =
        range_pgoffs_are_all_zero && (mmap.pgoff() == 0);
    if (prev_mmap == nullptr) {
      // First entry of the range.
      prev_mmap = &mmap;
      range = MMapRange(i, i);
      continue;
    }
    // Ranges match exactly: //anon,//anon, or file,file; If they use different
    // names, then deduction needs to consider them independently.
    if (prev_mmap->filename() != mmap.filename()) {
      break;
    }
    // If they're not virtually contiguous they're not a single range.
    if (!IsVmaContiguous(*prev_mmap, mmap)) {
      break;
    }
    // If the file offsets aren't contiguous they're not a single range unless
    // the pages are all possibly anonymous. Note, pgoff is 0 is tested rather
    // than IsAnon as some versions of Google's perf will rename //anon to
    // argv[0].
    if (!range_pgoffs_are_all_zero && !IsFileContiguous(*prev_mmap, mmap)) {
      break;
    }
    prev_mmap = &mmap;
    range = MMapRange(range.FirstIndex(), i);
  }
  // Range has:
  // - single file
  // - virtually contiguous
  // - either: all entries are either pgoff=0 or file contiguous
  return range;
}

// FindNextRange will return the next range after the given |prev_range| if
// there is one; otherwise it will return an invalid range.
MMapRange FindNextRange(const RepeatedPtrField<PerfEvent>& events,
                        const MMapRange& prev_range) {
  MMapRange ret;
  if (prev_range.IsValid() && prev_range.LastIndex() < events.size()) {
    ret = FindRange(events, prev_range.LastIndex() + 1);
  }
  return ret;
}

// UpdateRangeFromNext will set the filename / pgoff of all mmaps within |range|
// to be pgoff-contiguous with |next_range|, and match its file information.
void UpdateRangeFromNext(const MMapRange& range, const MMapRange& next_range,
                         RepeatedPtrField<PerfEvent>* events) {
  CHECK(range.LastIndex() < events->size());
  CHECK(next_range.LastIndex() < events->size());
  const MMapEvent& src = next_range.FirstMmap(*events);
  const uint64 start_pgoff = src.pgoff() - range.Len(*events);
  uint64 pgoff = start_pgoff;
  for (int i = range.FirstIndex(); i <= range.LastIndex(); i++) {
    if (!events->Get(i).has_mmap_event()) {
      continue;
    }
    PerfEvent* event = events->Mutable(i);
    MMapEvent* mmap = event->mutable_mmap_event();
    // As perf will rename huge pages to the executable name but not update the
    // pgoff, treat any offset of 0 as a huge page.
    const bool is_hugepage = IsHugePage(*mmap) || mmap->pgoff() == 0;
    // Replace pages mapped from a huge page source with a regular name if
    // possible.
    if (is_hugepage) {
      SetMmapFilename(event, src.filename(), src.filename_md5_prefix());
      if (mmap->pgoff() == 0) {
        // Correct anonymous page file offsets.
        mmap->set_pgoff(pgoff);
      }
      // Correct other fields where mmap is associated with a huge page text
      // source.
      if (src.has_maj()) {
        mmap->set_maj(src.maj());
      }
      if (src.has_min()) {
        mmap->set_min(src.min());
      }
      if (src.has_ino()) {
        mmap->set_ino(src.ino());
      }
      if (src.has_ino_generation()) {
        mmap->set_ino_generation(src.ino_generation());
      }
    }
    pgoff += mmap->len();
  }
  CHECK_EQ(pgoff, start_pgoff + range.Len(*events));
}
}  // namespace

void DeduceHugePages(RepeatedPtrField<PerfEvent>* events) {
  // |prev_range|, if IsValid(), represents the preview mmap range seen (and
  // already processed / updated).
  MMapRange prev_range;
  // |range| contains the currently-being-processed mmap range, which will have
  // its hugepages ranges deduced.
  MMapRange range = FindRange(*events, 0);
  // |next_range| contains the next range to process, possibily containing
  // a huge page from which the current range can be updated.
  MMapRange next_range = FindNextRange(*events, range);

  for (; range.IsValid(); prev_range = range, range = next_range,
                          next_range = FindNextRange(*events, range)) {
    const bool have_next =
        (next_range.IsValid() && IsVmaContiguous(*events, range, next_range) &&
         IsEquivalentFile(*events, range, next_range));

    // If there's no mmap after this, then we assume that this is *not* viable
    // a hugepage_text mapping. This is true unless we're really unlucky. If:
    // - the binary is mapped such that the limit is hugepage aligned
    //   (presumably 4Ki/2Mi chance == p=0.03125)
    // - and the entire binaryis hugepage_text mapped
    if (!have_next) {
      continue;
    }

    const bool have_prev =
        (prev_range.IsValid() && IsVmaContiguous(*events, prev_range, range) &&
         IsEquivalentFile(*events, prev_range, range) &&
         IsEquivalentFile(*events, prev_range, next_range));

    uint64 start_pgoff = 0;
    if (have_prev) {
      const auto& prev = prev_range.LastMmap(*events);
      start_pgoff = prev.pgoff() + prev.len();
    }
    const auto& next = next_range.FirstMmap(*events);
    // prev.pgoff should be valid now, so let's double-check that
    // if next has a non-zero pgoff, that {prev,curr,next} will have
    // contiguous pgoff once updated.
    if (!IsHugePage(next) && next.pgoff() >= range.Len(*events) &&
        (next.pgoff() - range.Len(*events)) == start_pgoff) {
      UpdateRangeFromNext(range, next_range, events);
    }
  }
}

void CombineMappings(RepeatedPtrField<PerfEvent>* events) {
  // Combine mappings
  RepeatedPtrField<PerfEvent> new_events;
  new_events.Reserve(events->size());

  // |prev| is the index of the last mmap_event in |new_events| (or
  // |new_events.size()| if no mmap_events have been inserted yet).
  int prev = 0;
  for (int i = 0; i < events->size(); ++i) {
    PerfEvent* event = events->Mutable(i);
    if (!event->has_mmap_event()) {
      new_events.Add()->Swap(event);
      continue;
    }

    const MMapEvent& mmap = event->mmap_event();
    // Try to merge mmap with |new_events[prev]|.
    while (prev < new_events.size() && !new_events.Get(prev).has_mmap_event()) {
      prev++;
    }

    if (prev >= new_events.size()) {
      new_events.Add()->Swap(event);
      continue;
    }

    MMapEvent* prev_mmap = new_events.Mutable(prev)->mutable_mmap_event();

    // Don't use IsEquivalentFile(); we don't want to combine //anon with
    // files if DeduceHugepages didn't already fix up the mappings.
    const bool file_match = prev_mmap->filename() == mmap.filename();
    const bool pgoff_contiguous =
        file_match && IsFileContiguous(*prev_mmap, mmap);

    const bool combine_mappings =
        IsVmaContiguous(*prev_mmap, mmap) && pgoff_contiguous;
    if (!combine_mappings) {
      new_events.Add()->Swap(event);
      prev++;
      continue;
    }

    // Combine the lengths of the two mappings.
    prev_mmap->set_len(prev_mmap->len() + mmap.len());
  }

  events->Swap(&new_events);
}

}  // namespace quipper
