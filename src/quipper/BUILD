
licenses(["notice"])  # BSD

exports_files(["LICENSE"])

perf_test_data = glob([
    "testdata/hello_world.txt.gz",
    "testdata/perf",
    "testdata/perf.callgraph.pb_text",
    "testdata/perf.data.*",
])

binary_data_utils_test_data = [
    "testdata/hello_world.txt.gz",
]

proto_library(
    name = "perf_data_proto",
    srcs = ["perf_data.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "perf_data_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":perf_data_proto"],
)

proto_library(
    name = "perf_stat_proto",
    srcs = ["perf_stat.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "perf_stat_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":perf_stat_proto"],
)

cc_library(
    name = "kernel",
    hdrs = [
        "kernel/perf_event.h",
        "kernel/perf_internals.h",
    ],
    visibility = [
        "//src:__subpackages__",
    ],
)

cc_library(
    name = "compat",
    srcs = ["compat/log_level.cc"],
    hdrs = [
        "compat/log_level.h",
        "compat/proto.h",
        "compat/string.h",
    ],
    deps = [
        ":perf_data_cc_proto",
        ":perf_stat_cc_proto",
        ":base",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "compat_gunit",
    testonly = 1,
    hdrs = ["compat/test.h"],
    deps = [
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
   name = "base",
   srcs = ["base/logging.cc"],
   hdrs = [
       "base/logging.h",
       "base/macros.h",
   ],
   includes = ["."],
   visibility = ["//src:__subpackages__"],
)

cc_library(
    name = "dso",
    srcs = ["dso.cc"],
    hdrs = ["dso.h"],
    visibility = ["//src:__subpackages__"],
    deps = [
        ":compat",
        ":data_reader",
        ":file_reader",
        ":base",
    ],
    linkopts = ["-lelf"],
)

cc_library(
    name = "dso_test_utils",
    testonly = 1,
    srcs = ["dso_test_utils.cc"],
    hdrs = ["dso_test_utils.h"],
    deps = [
        ":binary_data_utils",
        ":compat",
        ":base",
    ],
    linkopts = ["-lelf"],
)

cc_library(
    name = "perf_serializer",
    srcs = [
        "dso.h",
        "perf_data_structures.h",
        "perf_parser.h",
        "perf_reader.h",
        "perf_serializer.cc",
    ],
    hdrs = ["perf_serializer.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":binary_data_utils",
        ":compat",
        ":perf_data_utils",
        ":sample_info_reader",
        ":base",
    ],
)

cc_library(
    name = "perf_stat_parser",
    srcs = ["perf_stat_parser.cc"],
    hdrs = ["perf_stat_parser.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":compat",
        ":file_utils",
        ":string_utils",
        ":base",
    ],
)

cc_library(
    name = "perf_parser",
    srcs = ["perf_parser.cc"],
    hdrs = ["perf_parser.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":address_mapper",
        ":binary_data_utils",
        ":compat",
        ":dso",
        ":huge_page_deducer",
        ":perf_reader",
        ":base",
    ],
)

cc_library(
    name = "huge_page_deducer",
    srcs = ["huge_page_deducer.cc"],
    hdrs = ["huge_page_deducer.h"],
    deps = [
        ":compat",
        ":perf_data_utils",
        ":base",
    ],
)

cc_library(
    name = "perf_reader",
    srcs = ["perf_reader.cc"],
    hdrs = ["perf_reader.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":binary_data_utils",
        ":buffer_reader",
        ":buffer_writer",
        ":compat",
        ":file_reader",
        ":file_utils",
        ":kernel",
        ":perf_data_utils",
        ":perf_serializer",
        ":sample_info_reader",
        ":base",
    ],
)

cc_library(
    name = "perf_protobuf_io",
    srcs = ["perf_protobuf_io.cc"],
    hdrs = ["perf_protobuf_io.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":compat",
        ":file_utils",
        ":perf_parser",
        ":perf_reader",
        ":perf_serializer",
    ],
)

cc_library(
    name = "address_mapper",
    srcs = ["address_mapper.cc"],
    hdrs = ["address_mapper.h"],
    deps = [":base"],
)

cc_library(
    name = "binary_data_utils",
    srcs = ["binary_data_utils.cc"],
    hdrs = ["binary_data_utils.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":compat",
        ":kernel",
        ":base",
        "@boringssl//:crypto",
    ],
)

cc_library(
    name = "buffer_reader",
    srcs = ["buffer_reader.cc"],
    hdrs = ["buffer_reader.h"],
    deps = [
        ":data_reader",
        ":base",
    ],
)

cc_library(
    name = "buffer_writer",
    srcs = ["buffer_writer.cc"],
    hdrs = ["buffer_writer.h"],
    deps = [
        ":data_writer",
        ":base",
    ],
)

cc_library(
    name = "conversion_utils",
    srcs = ["conversion_utils.cc"],
    hdrs = ["conversion_utils.h"],
    deps = [
        ":file_utils",
        ":perf_data_cc_proto",
        ":perf_parser",
        ":perf_protobuf_io",
        ":perf_reader",
    ],
)

cc_library(
    name = "data_reader",
    srcs = ["data_reader.cc"],
    hdrs = ["data_reader.h"],
    deps = [
        ":binary_data_utils",
        ":compat",
        ":base",
    ],
)

cc_library(
    name = "data_writer",
    srcs = ["data_writer.cc"],
    hdrs = ["data_writer.h"],
    deps = [
        ":compat",
        ":perf_data_utils",
        ":base",
    ],
)

cc_library(
    name = "file_reader",
    srcs = ["file_reader.cc"],
    hdrs = ["file_reader.h"],
    deps = [
        ":data_reader",
        ":base",
    ],
)

cc_library(
    name = "file_utils",
    srcs = ["file_utils.cc"],
    hdrs = ["file_utils.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":compat",
        ":file_reader",
    ],
)

cc_binary(
    name = "perf_converter",
    srcs = ["perf_converter.cc"],
    deps = [
        ":compat",
        ":conversion_utils",
    ],
)

cc_library(
    name = "perf_data_utils",
    srcs = ["perf_data_utils.cc"],
    hdrs = ["perf_data_utils.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":binary_data_utils",
        ":compat",
        ":base",
    ],
)

cc_library(
    name = "perf_option_parser",
    srcs = ["perf_option_parser.cc"],
    hdrs = ["perf_option_parser.h"],
    deps = [
        ":compat",
        ":base",
    ],
)

cc_library(
    name = "run_command",
    srcs = ["run_command.cc"],
    hdrs = ["run_command.h"],
    deps = [
        ":compat",
        ":base",
    ],
)

cc_library(
    name = "sample_info_reader",
    srcs = ["sample_info_reader.cc"],
    hdrs = ["sample_info_reader.h"],
    deps = [
        ":buffer_reader",
        ":buffer_writer",
        ":kernel",
        ":base",
    ],
)

cc_library(
    name = "scoped_temp_path",
    srcs = ["scoped_temp_path.cc"],
    hdrs = ["scoped_temp_path.h"],
    deps = [
        ":compat",
        ":base",
    ],
)

cc_library(
    name = "string_utils",
    srcs = ["string_utils.cc"],
    hdrs = ["string_utils.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":compat",
    ],
)

cc_library(
    name = "test_utils",
    testonly = 1,
    srcs = [
        "test_perf_data.cc",
        "test_utils.cc",
    ],
    hdrs = [
        "test_perf_data.h",
        "test_utils.h",
    ],
    deps = [
        ":compat",
        ":compat_gunit",
        ":file_utils",
        ":kernel",
        ":perf_data_cc_proto",
        ":perf_parser",
        ":perf_protobuf_io",
        ":run_command",
        ":string_utils",
        ":base",
    ],
    copts = ["-DGITHUB_BAZEL"]
)

cc_library(
    name = "test_runner",
    testonly = 1,
    srcs = ["test_runner.cc"],
    deps = [
        ":compat_gunit",
    ],
)

cc_library(
    name = "perf_recorder",
    srcs = ["perf_recorder.cc"],
    hdrs = ["perf_recorder.h"],
    deps = [
        ":compat",
        ":perf_option_parser",
        ":perf_protobuf_io",
        ":perf_stat_parser",
        ":run_command",
        ":scoped_temp_path",
    ],
)

cc_test(
    name = "perf_recorder_test",
    srcs = ["perf_recorder_test.cc"],
    deps = [
        ":compat_gunit",
        ":perf_recorder",
        ":test_utils",
        ":base",
    ],
)

cc_binary(
    name = "quipper",
    srcs = ["quipper.cc"],
    deps = [
        ":perf_recorder",
    ],
)

cc_library(
    name = "perf_test_files",
    testonly = 1,
    srcs = ["perf_test_files.cc"],
    hdrs = ["perf_test_files.h"],
    data = perf_test_data,
)

cc_test(
    name = "address_mapper_test",
    srcs = ["address_mapper_test.cc"],
    deps = [
        ":address_mapper",
        ":compat_gunit",
        ":test_runner",
        ":base",
    ],
)

cc_test(
    name = "binary_data_utils_test",
    srcs = ["binary_data_utils_test.cc"],
    data = binary_data_utils_test_data,
    deps = [
        ":binary_data_utils",
        ":compat_gunit",
        ":perf_data_cc_proto",
        ":test_runner",
        ":test_utils",
    ],
)

cc_test(
    name = "buffer_reader_test",
    srcs = ["buffer_reader_test.cc"],
    deps = [
        ":buffer_reader",
        ":compat_gunit",
        ":test_runner",
    ],
)

cc_test(
    name = "buffer_writer_test",
    srcs = ["buffer_writer_test.cc"],
    deps = [
        ":buffer_writer",
        ":compat_gunit",
        ":test_runner",
    ],
)

cc_test(
    name = "conversion_utils_test",
    srcs = ["conversion_utils_test.cc"],
    deps = [
        ":conversion_utils",
        ":perf_test_files",
        ":scoped_temp_path",
        ":test_runner",
        ":test_utils",
    ],
)

cc_test(
    name = "dso_test",
    srcs = ["dso_test.cc"],
    deps = [
        ":buffer_reader",
        ":compat_gunit",
        ":dso",
        ":dso_test_utils",
        ":scoped_temp_path",
        ":test_runner",
        ":base",
    ],
    linkopts = ["-lelf"],
)

cc_test(
    name = "file_reader_test",
    srcs = ["file_reader_test.cc"],
    deps = [
        ":compat_gunit",
        ":file_reader",
        ":file_utils",
        ":scoped_temp_path",
        ":test_runner",
        ":test_utils",
    ],
)

cc_test(
    name = "perf_option_parser_test",
    srcs = ["perf_option_parser_test.cc"],
    deps = [
        ":compat",
        ":compat_gunit",
        ":perf_option_parser",
        ":test_runner",
        ":test_utils",
    ],
)

cc_test(
    name = "perf_data_utils_test",
    srcs = ["perf_data_utils_test.cc"],
    deps = [
        ":compat",
        ":compat_gunit",
        ":perf_data_utils",
        ":test_runner",
    ],
)

cc_test(
    name = "perf_parser_test",
    size = "large",
    srcs = [
        "compat/non_cros/detail/thread.h",
        "compat/thread.h",
        "perf_parser_test.cc",
    ],
    includes = ["compat/non_cros"],
    deps = [
        ":compat",
        ":compat_gunit",
        ":dso_test_utils",
        ":perf_parser",
        ":perf_reader",
        ":perf_test_files",
        ":scoped_temp_path",
        ":test_runner",
        ":test_utils",
        ":base",
    ],
    linkopts = ["-lcap"],
)

cc_test(
    name = "perf_reader_test",
    srcs = [
        "perf_reader_test.cc",
        "scoped_temp_path.h",
    ],
    deps = [
        ":compat",
        ":compat_gunit",
        ":file_utils",
        ":perf_reader",
        ":perf_test_files",
        ":test_runner",
        ":test_utils",
        ":base",
    ],
)

cc_test(
    name = "perf_serializer_test",
    size = "large",
    srcs = ["perf_serializer_test.cc"],
    deps = [
        ":compat",
        ":compat_gunit",
        ":file_utils",
        ":perf_protobuf_io",
        ":perf_reader",
        ":perf_serializer",
        ":perf_test_files",
        ":scoped_temp_path",
        ":test_runner",
        ":test_utils",
        ":base",
    ],
)

cc_test(
    name = "perf_stat_parser_test",
    size = "small",
    srcs = ["perf_stat_parser_test.cc"],
    deps = [
        ":compat",
        ":compat_gunit",
        ":file_utils",
        ":perf_stat_parser",
        ":scoped_temp_path",
        ":test_runner",
        ":base",
    ],
)

cc_test(
    name = "run_command_test",
    srcs = ["run_command_test.cc"],
    deps = [
        ":compat",
        ":compat_gunit",
        ":run_command",
        ":test_runner",
    ],
)

cc_test(
    name = "sample_info_reader_test",
    srcs = ["sample_info_reader_test.cc"],
    deps = [
        ":compat_gunit",
        ":kernel",
        ":sample_info_reader",
        ":test_runner",
        ":test_utils",
    ],
)

cc_test(
    name = "scoped_temp_path_test",
    srcs = ["scoped_temp_path_test.cc"],
    deps = [
        ":compat_gunit",
        ":scoped_temp_path",
        ":test_runner",
        ":base",
    ],
)

test_suite(name = "AllTests")
