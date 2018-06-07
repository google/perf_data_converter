/*
 * Copyright (c) 2016, Google Inc.
 * All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/compat/string_compat.h"
#include "src/compat/test_compat.h"
#include "src/path_matching.h"
#include "src/perf_data_handler.h"
#include "src/quipper/binary_data_utils.h"
#include "src/quipper/kernel/perf_event.h"
#include "src/quipper/perf_data_utils.h"

namespace perftools {

TEST(PathMatching, DeletedSharedObjectMatching) {
  const std::vector<string> paths = {
      "lib.so.v1(deleted)",
      "lib.so.v1(deleted)junk",
      "lib.so (deleted)",
      "lib.so_junk_(deleted)",
      "lib.so   .so junk_(deleted)",
  };
  for (const auto& path : paths) {
    ASSERT_TRUE(IsDeletedSharedObject(path));
  }
}

TEST(PathMatching, DeletedSharedObjectNotMatching) {
  const std::vector<string> paths = {
      "abc",
      "lib.so ",
      "lib.so(deleted)",
      ".so (deleted)",
      "lib.sojunk(deleted)",
      "",
  };

  for (const auto& path : paths) {
    ASSERT_FALSE(IsDeletedSharedObject(path));
  }
}

TEST(PathMatching, VersionedSharedObjectMatching) {
  const std::vector<string> paths = {
      "lib.so.", "lib.so.abc", "lib.so.1", "lib.so.v1",
  };
  for (const auto& path : paths) {
    ASSERT_TRUE(IsVersionedSharedObject(path));
  }
}

TEST(PathMatching, VersionedSharedObjectNotMatching) {
  const std::vector<string> paths = {
      "abc", "lib.so(deleted)", ".so.v1", ".so.", "",
  };
  for (const auto& path : paths) {
    ASSERT_FALSE(IsDeletedSharedObject(path));
  }
}

class PerfDataHandlerTest : public ::testing::Test {
 protected:
  PerfDataHandlerTest() {}
};

class MMapTestHandler : public perftools::PerfDataHandler {
 public:
  MMapTestHandler(
      std::unordered_map<string, string> expected_filename_to_build_id)
      : _expected_filename_to_build_id(
            std::move(expected_filename_to_build_id)) {}
  MMapTestHandler(const MMapTestHandler&) = delete;
  MMapTestHandler& operator=(const MMapTestHandler&) = delete;
  ~MMapTestHandler() override {}

  // Callbacks for PerfDataHandler
  void Sample(const PerfDataHandler::SampleContext& sample) override {}
  void Comm(const CommContext& comm) override {}
  void MMap(const MMapContext& mmap) override {
    const string* actual_build_id = mmap.mapping->build_id;
    const string* actual_filename = mmap.mapping->filename;
    const auto expected_build_id_it =
        _expected_filename_to_build_id.find(*actual_filename);
    if (expected_build_id_it != _expected_filename_to_build_id.end()) {
      EXPECT_TRUE(actual_build_id != nullptr)
          << "Expected build id " << expected_build_id_it->second
          << " for the filename " << *actual_filename;
      if (actual_build_id != nullptr) {
        EXPECT_EQ(expected_build_id_it->second, *actual_build_id);
        _seen_filenames.insert(*actual_filename);
      }
    } else {
      EXPECT_TRUE(actual_build_id == nullptr)
          << "Actual build id " << *actual_build_id << " for the filename "
          << *actual_filename;
    }
  }

  void CheckSeenFilenames() {
    EXPECT_EQ(_expected_filename_to_build_id.size(), _seen_filenames.size());
    for (auto const& filename : _seen_filenames) {
      EXPECT_TRUE(_expected_filename_to_build_id.find(filename) !=
                  _expected_filename_to_build_id.end());
    }
  }

 private:
  std::unordered_map<string, string> _expected_filename_to_build_id;
  std::unordered_set<string> _seen_filenames;
};

TEST(PerfDataHandlerTest, KernelBuildIdWithDifferentFilename) {
  const int kHexCharsPerByte = 2;
  quipper::PerfDataProto proto;
  // Add a guest kernel build id.
  auto* build_id = proto.add_build_ids();
  build_id->set_misc(quipper::PERF_RECORD_MISC_GUEST_KERNEL);
  build_id->set_pid(-1);
  build_id->set_filename("[guest.kernel.kallsyms]");
  build_id->set_filename_md5_prefix(
      quipper::Md5Prefix("[guest.kernel.kallsyms]"));

  string guest_kernel_build_id = "27357e645f";
  uint8_t guest_kernel_build_id_bytes[quipper::kBuildIDArraySize];
  ASSERT_TRUE(quipper::HexStringToRawData(guest_kernel_build_id,
                                          guest_kernel_build_id_bytes,
                                          sizeof(guest_kernel_build_id_bytes)));
  build_id->set_build_id_hash(guest_kernel_build_id_bytes,
                              guest_kernel_build_id.size() / kHexCharsPerByte);

  // Add kernel build id with a different filename.
  build_id = proto.add_build_ids();
  build_id->set_misc(quipper::PERF_RECORD_MISC_KERNEL);
  build_id->set_pid(-1);
  build_id->set_filename("kernel");
  build_id->set_filename_md5_prefix(quipper::Md5Prefix("kernel"));

  string kernel_build_id = "17937e648e";
  uint8_t kernel_build_id_bytes[quipper::kBuildIDArraySize];
  ASSERT_TRUE(quipper::HexStringToRawData(
      kernel_build_id, kernel_build_id_bytes, sizeof(kernel_build_id_bytes)));
  build_id->set_build_id_hash(kernel_build_id_bytes,
                              kernel_build_id.size() / kHexCharsPerByte);

  // Add a non-kernel build id.
  build_id = proto.add_build_ids();
  build_id->set_misc(quipper::PERF_RECORD_MISC_USER);
  build_id->set_pid(7568);
  build_id->set_filename("chrome");
  build_id->set_filename_md5_prefix(quipper::Md5Prefix("chrome"));

  string chrome_build_id = "cac4b36db4d0";
  uint8_t chrome_build_id_bytes[quipper::kBuildIDArraySize];
  ASSERT_TRUE(quipper::HexStringToRawData(
      chrome_build_id, chrome_build_id_bytes, sizeof(chrome_build_id_bytes)));
  build_id->set_build_id_hash(chrome_build_id_bytes,
                              chrome_build_id.size() / kHexCharsPerByte);

  // Add MMaps for kernel and non-kernel filenames.
  auto* mmap_event = proto.add_events()->mutable_mmap_event();
  mmap_event->set_filename("[kernel.kallsyms]");
  mmap_event->set_pid(100);
  mmap_event->set_tid(100);
  mmap_event->set_start(0);
  mmap_event->set_len(20000);
  mmap_event->set_pgoff(4000);

  mmap_event = proto.add_events()->mutable_mmap_event();
  mmap_event->set_filename("chrome");
  mmap_event->set_pid(100);
  mmap_event->set_tid(100);
  mmap_event->set_start(1000);
  mmap_event->set_len(1000);
  mmap_event->set_pgoff(0);

  mmap_event = proto.add_events()->mutable_mmap_event();
  mmap_event->set_filename("unknown");
  mmap_event->set_pid(3400);
  mmap_event->set_tid(3400);
  mmap_event->set_start(2000);
  mmap_event->set_len(1000);
  mmap_event->set_pgoff(0);

  // Map of expected filenames to buildids. Unknown is expected not to have a
  // build id. Since the guest kernel doesn't have a mapping, it is not expected
  // to appear in the MMap() call.
  std::unordered_map<string, string> expected_filename_to_build_id;
  expected_filename_to_build_id["[kernel.kallsyms]"] = "17937e648e";
  expected_filename_to_build_id["chrome"] = "cac4b36db4d0";

  MMapTestHandler handler(expected_filename_to_build_id);
  perftools::PerfDataHandler::Process(proto, &handler);
  handler.CheckSeenFilenames();
}

}  // namespace perftools

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
