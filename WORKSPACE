load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# GoogleTest/GoogleMock framework. Used by most unit-tests.
http_archive(
     name = "com_google_googletest",
     sha256 = "fcda881b5c49d3e299fecd0aaae188a19743c24330e947526adfc952c0ed6183",
     urls = ["https://github.com/google/googletest/archive/master.zip"],
     strip_prefix = "googletest-master",
)

# proto_library, cc_proto_library, and java_proto_library rules implicitly
# depend on @com_google_protobuf for protoc and proto runtimes.
http_archive(
    name = "com_google_protobuf",
    sha256 = "8eb5ca331ab8ca0da2baea7fc0607d86c46c80845deca57109a5d637ccb93bb4",
    urls = ["https://github.com/protocolbuffers/protobuf/archive/v3.9.0.zip"],
    strip_prefix = "protobuf-3.9.0",
)

# bazel_skylib is a dependency of protobuf.
http_archive(
    name = "bazel_skylib",
    sha256 = "bbccf674aa441c266df9894182d80de104cabd19be98be002f6d478aaa31574d",
    urls = ["https://github.com/bazelbuild/bazel-skylib/archive/2169ae1c374aab4a09aa90e65efe1a3aad4e279b.tar.gz"],
    strip_prefix = "bazel-skylib-2169ae1c374aab4a09aa90e65efe1a3aad4e279b",
)

http_archive(
    name = "boringssl",  # Must match upstream workspace name.
    # Gitiles creates gzip files with an embedded timestamp, so we cannot use
    # sha256 to validate the archives.  We must rely on the commit hash and
    # https. Commits must come from the master-with-bazel branch.
    urls = ["https://github.com/google/boringssl/archive/master-with-bazel.zip"],
    strip_prefix = "boringssl-master-with-bazel",
)

# zlib is a dependency of protobuf.
http_archive(
    name = "zlib",
    sha256 = "c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1",
    # This is the zlib BUILD file used in kythe:
    # https://github.com/kythe/kythe/blob/v0.0.30/third_party/zlib.BUILD
    build_file = "zlib.BUILD",
    urls = ["https://www.zlib.net/zlib-1.2.11.tar.gz"],
    strip_prefix = "zlib-1.2.11",
)

http_archive(
    name   = "com_github_gflags_gflags",
    sha256 = "d5e220a6f8c7d348d4cca4855a67f9046e3c5ededce73ee489fa43780ec80149",
    urls = ["https://github.com/gflags/gflags/archive/master.zip"],
    strip_prefix = "gflags-master",
)

http_archive(
    name = "com_google_re2",
    sha256 = "ea4425b9cd4f963e142f6f95826cae3f36f4fd6daba19485b17fbee4d9021801",
    urls = ["https://github.com/google/re2/archive/master.zip"],
    strip_prefix = "re2-master",
)
