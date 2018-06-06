package(
    default_visibility = [
        "//src:__subpackages__",
    ],
)

licenses(["notice"])  # BSD

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
        "//src/compat:int_compat",
        "//src/compat:string_compat",
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
        "//src/compat:int_compat",
        "//src/compat:string_compat",
        "//src/quipper:kernel",
        "//src/quipper:perf_data_cc_proto",
        "//src/quipper:perf_data_utils",
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
        "//src/compat:int_compat",
    ],
)

cc_test(
    name = "perf_data_handler_test",
    size = "small",
    srcs = ["perf_data_handler_test.cc"],
    deps = [
        ":perf_data_handler",
        "//src/compat:string_compat",
        "//src/compat:test_compat",
        "//src/quipper:binary_data_utils",
        "//src/quipper:kernel",
        "//src/quipper:perf_data_utils",
    ],
)

cc_test(
    name = "perf_data_converter_test",
    size = "medium",
    srcs = ["perf_data_converter_test.cc"],
    copts = ["-DGITHUB_BAZEL"],
    data = [
        "//src/testdata:multi-event-single-process.perf.data",
        "//src/testdata:perf-overlapping-kernel-mapping.pb_proto",
        "//src/testdata:single-event-multi-process.perf.data",
        "//src/testdata:single-event-single-process.perf.data",
        "//src/testdata:with-callchain.perf.data",
    ],
    tags = ["client"],
    deps = [
        ":intervalmap",
        ":perf_data_converter",
        "//src/compat:int_compat",
        "//src/compat:string_compat",
        "//src/compat:test_compat",
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
        "//src/compat:int_compat",
        "//src/compat:string_compat",
        "//src/compat:test_compat",
    ],
)

cc_library(
    name = "perf_to_profile_lib",
    srcs = ["perf_to_profile_lib.cc"],
    hdrs = ["perf_to_profile_lib.h"],
    deps = [
        "//src/quipper:base",
        "//src/compat:string_compat",
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
    deps = [
        ":perf_to_profile_lib",
        "//src/quipper:base",
        "//src/compat:test_compat",
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
        "//external:zlib",
        "//src/quipper:base",
        "@com_google_protobuf//:protobuf",
    ],
)

test_suite(name = "AllTests")
