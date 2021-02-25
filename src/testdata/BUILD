
# Description:
#   Test files for converting sampledb proto to profile proto.
licenses(["notice"])  # BSD

# When possible, add new testdata files in the Quipper *.textproto format, not
# as Linux perf *.data files. The latter are too much of a black box and are
# harder to recreate.
exports_files(
    glob([
        "*.perf_data.pb",  # Quipper (go/perf_data.proto) binary protos.
        "*.perf.data",  # Linux perf data files.
        "*.textproto",  # Text-formatted protos (see header for type).
    ]),
    visibility = [
        "//src:__subpackages__",
    ],
)
