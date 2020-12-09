
# Description:
#   Test files for converting sampledb proto to profile proto.
licenses(["notice"])  # BSD

# When possible, add new testdata files in the Quipper *.textproto format, not
# as Linux perf *.data files. The latter are too much of a black box and are
# harder to recreate.
exports_files(
    [
        "multi-event-single-process.perf.data",
        "multi-event-single-process.perf_data.pb.data",
        "perf-callchain-non-pebs.textproto",
        "perf-callchain-pebs.textproto",
        "perf-comm-and-task-comm.textproto",
        "perf-cros-kernel-3_18-mapping.textproto",
        "perf-include-comm-md5-prefix.textproto",
        "perf-java-classes-jsa.textproto",
        "perf-kernel-sample-before-mmap.textproto",
        "perf-non-exec-comm-events.textproto",
        "perf-overlapping-kernel-mapping.textproto",
        "perf-unmapped-callchain-ip.textproto",
        "profile-with-data-addresses-flat.textproto",
        "profile-with-data-addresses-callchain.textproto",
        "single-event-multi-process.perf.data",
        "single-event-multi-process-single-ip.textproto",
        "single-event-single-process.perf.data",
        "with-callchain.perf.data",
    ],
    visibility = [
        "//src:__subpackages__",
    ],
)
