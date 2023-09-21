package(
    default_visibility = [
        "//src:__subpackages__",
    ],
)

licenses(["notice"])

exports_files(["LICENSE"])

cc_library(
    name = "perf_data_handler",
    srcs = ["perf_data_handler.cc"],
    hdrs = [
        "path_matching.h",
        "perf_data_handler.h",
    ],
    deps = [
        ":intervalmap",
        "//src/quipper:binary_data_utils",
        "//src/quipper:dso",
        "//src/quipper:kernel",
        "//src/quipper:perf_data_cc_proto",
        "//src/quipper:perf_reader",
    ],
)

cc_library(
    name = "perf_data_converter",
    srcs = [
        "perf_data_converter.cc",
    ],
    hdrs = ["perf_data_converter.h"],
    deps = [
        ":perf_data_handler",
        ":builder",
        ":profile_cc_proto",
        "//src/quipper:kernel",
        "//src/quipper:perf_data_cc_proto",
        "//src/quipper:perf_parser",
        "//src/quipper:perf_reader",
    ],
)

cc_library(
    name = "intervalmap",
    hdrs = [
        "intervalmap.h",
    ],
    deps = [
        "//src/quipper:base",
    ],
)

cc_test(
    name = "perf_data_handler_test",
    size = "small",
    srcs = ["perf_data_handler_test.cc"],
    deps = [
        ":perf_data_handler",
        "@com_google_googletest//:gtest_main",
        "//src/quipper:binary_data_utils",
        "//src/quipper:kernel",
        "//src/quipper:perf_buildid",
    ],
)

cc_test(
    name = "perf_data_converter_test",
    size = "medium",
    srcs = ["perf_data_converter_test.cc"],
    data = [
        "//src/testdata:multi-event-single-process.perf.data",
        "//src/testdata:perf-buildid-mmap-events.textproto",
        "//src/testdata:perf-buildid-stats.textproto",
        "//src/testdata:perf-callchain-non-pebs.textproto",
        "//src/testdata:perf-callchain-pebs.textproto",
        "//src/testdata:perf-cgroup-events.textproto",
        "//src/testdata:perf-code-data-page-sizes.textproto",
        "//src/testdata:perf-comm-and-task-comm.textproto",
        "//src/testdata:perf-cpu.textproto",
        "//src/testdata:perf-cros-kernel-3_18-mapping.textproto",
        "//src/testdata:perf-datasrc.textproto",
        "//src/testdata:perf-include-comm-md5-prefix.textproto",
        "//src/testdata:perf-java-classes-jsa.textproto",
        "//src/testdata:perf-kernel-mapping-by-name.textproto",
        "//src/testdata:perf-kernel-sample-before-mmap.textproto",
        "//src/testdata:perf-lost-events.textproto",
        "//src/testdata:perf-non-exec-comm-events.textproto",
        "//src/testdata:perf-overlapping-kernel-mapping.textproto",
        "//src/testdata:perf-unmapped-callchain-ip.textproto",
        "//src/testdata:perf-unmapped-sample-and-branch-stack.textproto",
        "//src/testdata:perf-weight.textproto",
        "//src/testdata:perf-weight-struct.textproto",
        "//src/testdata:profile-with-data-addresses-callchain.textproto",
        "//src/testdata:profile-with-data-addresses-flat.textproto",
        "//src/testdata:single-event-multi-process.perf.data",
        "//src/testdata:single-event-multi-process-single-ip.textproto",
        "//src/testdata:single-event-single-process.perf.data",
        "//src/testdata:with-callchain.perf.data",
    ],
    shard_count = 8,
    tags = ["client"],
    deps = [
        ":intervalmap",
        ":perf_data_converter",
        ":perf_data_handler",
        "@com_google_googletest//:gtest_main",
        "//src/quipper:perf_data_cc_proto",
        "//src/quipper:perf_parser",
        "//src/quipper:perf_reader",
    ],
)

cc_test(
    name = "intervalmap_test",
    size = "small",
    srcs = ["intervalmap_test.cc"],
    deps = [
        ":intervalmap",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "perf_to_profile_lib",
    srcs = ["perf_to_profile_lib.cc"],
    hdrs = ["perf_to_profile_lib.h"],
    deps = [
        ":perf_data_converter",
        "//src/quipper:base",
        "//src/quipper:perf_data_cc_proto",
    ],
)

cc_binary(
    name = "perf_to_profile",
    srcs = ["perf_to_profile.cc"],
    deps = [
        ":perf_data_converter",
        ":perf_to_profile_lib",
        "//src/quipper:base",
    ],
)

cc_test(
    name = "perf_to_profile_lib_test",
    srcs = ["perf_to_profile_lib_test.cc"],
    data = [
        "//src/testdata:multi-event-single-process.perf.data",
        "//src/testdata:multi-event-single-process.perf_data.pb",
    ],
    deps = [
        ":perf_to_profile_lib",
        "//src/quipper:base",
        "@com_google_googletest//:gtest_main",
    ],
)

proto_library(
    name = "profile_proto",
    srcs = ["profile.proto"],
)

cc_proto_library(
    name = "profile_cc_proto",
    deps = [":profile_proto"],
)

cc_library(
    name = "builder",
    srcs = ["builder.cc"],
    hdrs = ["builder.h"],
    deps = [
        ":profile_cc_proto",
        "//src/quipper:base",
        "@com_google_protobuf//:protobuf",
        "@zlib//:zlib",
    ],
)

test_suite(name = "AllTests")
