// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_COMPAT_PROTO_H_
#define CHROMIUMOS_WIDE_PROFILING_COMPAT_PROTO_H_

#include "perf_data.pb.h"
#include "perf_parser_options.pb.h"
#include "perf_stat.pb.h"
#include "google/protobuf//arena.h"
#include "google/protobuf//io/zero_copy_stream_impl_lite.h"
#include "google/protobuf//message.h"
#include "google/protobuf//repeated_field.h"
#include "google/protobuf//text_format.h"
#include "google/protobuf//util/message_differencer.h"

namespace quipper {

using ::google::protobuf::Arena;
using ::google::protobuf::Message;
using ::google::protobuf::RepeatedField;
using ::google::protobuf::RepeatedPtrField;
using ::google::protobuf::TextFormat;
using ::google::protobuf::io::ArrayInputStream;
using ::google::protobuf::util::MessageDifferencer;

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_COMPAT_PROTO_H_
