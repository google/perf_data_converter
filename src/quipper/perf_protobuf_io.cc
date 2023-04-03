// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "perf_protobuf_io.h"

#include <vector>

#include "base/logging.h"

#include "file_utils.h"

namespace quipper {

bool SerializeFromString(const std::string& contents,
                         PerfDataProto* perf_data_proto) {
  return SerializeFromStringWithOptions(contents, PerfParserOptions(),
                                        perf_data_proto);
}

bool SerializeFromStringWithOptions(const std::string& contents,
                                    const PerfParserOptions& options,
                                    PerfDataProto* perf_data_proto) {
  PerfReader reader;
  if (!reader.ReadFromString(contents)) return false;

  PerfParser parser(&reader, options);
  if (!parser.ParseRawEvents()) return false;

  if (!reader.Serialize(perf_data_proto)) return false;

  // Append parser stats to protobuf.
  PerfSerializer::SerializeParserStats(parser.stats(), perf_data_proto);
  return true;
}

bool SerializeFromStringWithOptions(const std::string& contents,
                                    const PerfParserOptionsProto& options,
                                    PerfDataProto* proto) {
  PerfParserOptions opts;
  opts.do_remap = options.do_remap();
  opts.discard_unused_events = options.discard_unused_events();
  opts.sample_mapping_percentage_threshold =
      options.sample_mapping_percentage_threshold();
  opts.sort_events_by_time = options.sort_events_by_time();
  opts.read_missing_buildids = options.read_missing_buildids();
  opts.deduce_huge_page_mappings = options.deduce_huge_page_mappings();
  opts.combine_mappings = options.combine_mappings();
  return SerializeFromStringWithOptions(contents, opts, proto);
}

bool SerializeFromFile(const std::string& filename,
                       PerfDataProto* perf_data_proto) {
  return SerializeFromFileWithOptions(filename, PerfParserOptions(),
                                      perf_data_proto);
}

bool SerializeFromFileWithOptions(const std::string& filename,
                                  const PerfParserOptions& options,
                                  PerfDataProto* perf_data_proto) {
  PerfReader reader;
  if (!reader.ReadFile(filename)) return false;

  PerfParser parser(&reader, options);
  if (!parser.ParseRawEvents()) return false;

  if (!reader.Serialize(perf_data_proto)) return false;

  // Append parser stats to protobuf.
  PerfSerializer::SerializeParserStats(parser.stats(), perf_data_proto);
  return true;
}

bool DeserializeToFile(const PerfDataProto& perf_data_proto,
                       const std::string& filename) {
  PerfReader reader;
  return reader.Deserialize(perf_data_proto) && reader.WriteFile(filename);
}

bool WriteProtobufToFile(const PerfDataProto& perf_data_proto,
                         const std::string& filename) {
  std::string output;
  perf_data_proto.SerializeToString(&output);

  return BufferToFile(filename, output);
}

bool ReadProtobufFromFile(PerfDataProto* perf_data_proto,
                          const std::string& filename) {
  std::vector<char> buffer;
  if (!FileToBuffer(filename, &buffer)) return false;

  bool ret = perf_data_proto->ParseFromArray(buffer.data(), buffer.size());

  LOG(INFO) << "#events" << perf_data_proto->events_size();

  return ret;
}

}  // namespace quipper
