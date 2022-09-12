// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_COMPAT_PROTO_H_
#define CHROMIUMOS_WIDE_PROFILING_COMPAT_PROTO_H_

#include "net/proto2/io/public/zero_copy_stream_impl_lite.h"
#include "net/proto2/public/arena.h"
#include "net/proto2/public/message.h"
#include "net/proto2/public/repeated_field.h"
#include "net/proto2/public/text_format.h"
#include "net/proto2/util/public/message_differencer.h"
#include "perf_data.pb.h"
#include "perf_parser_options.pb.h"
#include "perf_stat.pb.h"

namespace quipper {

using ::google::protobuf::Arena;
using ::google::protobuf::Message;
using ::google::protobuf::RepeatedField;
using ::google::protobuf::RepeatedPtrField;
using ::google::protobuf::TextFormat;
using ::google::protobuf::io::ArrayInputStream;
using ::google::protobuf::util::MessageDifferencer;
using ::google::protobuf::uint64;
}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_COMPAT_PROTO_H_
