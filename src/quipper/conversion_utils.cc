// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "conversion_utils.h"

#include <stdlib.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "base/logging.h"

#include "compat/proto.h"
#include "compat/string.h"
#include "file_utils.h"
#include "perf_parser.h"
#include "perf_protobuf_io.h"
#include "perf_reader.h"

namespace quipper {

namespace {

// Parse options from the format strings, set the options, and return the base
// format. Returns the empty string if options are not recognized.
std::string ParseFormatOptions(std::string format, PerfParserOptions* options) {
  auto dot = format.find('.');
  if (dot != std::string::npos) {
    std::string opt = format.substr(dot + 1);
    format = format.substr(0, dot);
    if (opt == "remap") {
      options->do_remap = true;
    } else if (opt == "discard") {
      options->discard_unused_events = true;
    } else if (opt == "remap.discard") {
      options->do_remap = true;
      options->discard_unused_events = true;
    } else {
      LOG(ERROR) << "Unknown option: " << opt;
      return "";
    }
  }
  return format;
}

// ReadInput reads the input and stores it within |reader|.
bool ReadInput(const FormatAndFile& input, PerfReader* reader,
               PerfParserOptions* options) {
  LOG(INFO) << "Reading input.";

  std::string format = ParseFormatOptions(input.format, options);
  if (format == kPerfFormat) {
    return reader->ReadFile(input.filename);
  }

  if (format == kProtoTextFormat || format == kProtoBinaryFormat) {
    PerfDataProto perf_data_proto;
    std::vector<char> data;
    if (!FileToBuffer(input.filename, &data)) return false;
    if (format == kProtoTextFormat) {
      std::string text(data.begin(), data.end());
      if (!TextFormat::ParseFromString(text, &perf_data_proto)) return false;
    } else {
      if (!perf_data_proto.ParseFromArray(data.data(), data.size())) {
        return false;
      }
    }

    return reader->Deserialize(perf_data_proto);
  }

  LOG(ERROR) << "Unimplemented read format: " << input.format;
  return false;
}

// WriteOutput reads from |reader| and writes the output to the file
// within |output|.
bool WriteOutput(const FormatAndFile& output, const PerfParserOptions& options,
                 PerfReader* reader) {
  LOG(INFO) << "Writing output.";

  // Apply use PerfParser to modify data in reader, applying hacks all hacks,
  // regardless of output format.
  PerfParser parser(reader, options);
  if (!parser.ParseRawEvents()) return false;

  std::string output_string;
  if (output.format == kPerfFormat) {
    return reader->WriteFile(output.filename);
  }

  if (output.format == kProtoTextFormat ||
      output.format == kProtoBinaryFormat) {
    PerfDataProto perf_data_proto;
    reader->Serialize(&perf_data_proto);

    // Serialize the parser stats as well.
    PerfSerializer::SerializeParserStats(parser.stats(), &perf_data_proto);

    // Reset the timestamp field since it causes reproducability issues when
    // testing.
    perf_data_proto.set_timestamp_sec(0);

    if (output.format == kProtoTextFormat) {
      if (!TextFormat::PrintToString(perf_data_proto, &output_string))
        return false;
    } else {
      output_string = perf_data_proto.SerializeAsString();
    }
    std::vector<char> data(output_string.begin(), output_string.end());
    return BufferToFile(output.filename, data);
  }

  LOG(ERROR) << "Unimplemented write format: " << output.format;
  return false;
}

}  // namespace

// Format string for perf.data.
constexpr char kPerfFormat[] = "perf";

// Format string for protobuf text format.
constexpr char kProtoTextFormat[] = "text";

// Format string for protobuf binary format.
constexpr char kProtoBinaryFormat[] = "proto";

bool ConvertFile(const FormatAndFile& input, const FormatAndFile& output) {
  PerfReader reader;
  PerfParserOptions options;
  if (!ReadInput(input, &reader, &options)) return false;
  if (!WriteOutput(output, options, &reader)) return false;
  return true;
}

}  // namespace quipper
