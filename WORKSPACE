load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# GoogleTest/GoogleMock framework. Used by most unit-tests.
http_archive(
     name = "com_google_googletest",
     urls = ["https://github.com/google/googletest/archive/master.zip"],
     strip_prefix = "googletest-master",
)

# proto_library, cc_proto_library, and java_proto_library rules implicitly
# depend on @com_google_protobuf for protoc and proto runtimes.
#
# N.B. We have a near-clone of the protobuf BUILD file overriding upstream so
# that we can set the unexported config variable to enable zlib. Without this,
# protobuf silently yields link errors.
http_archive(
    name = "com_google_protobuf",
    build_file = "protobuf.BUILD",
    sha256 = "9510dd2afc29e7245e9e884336f848c8a6600a14ae726adb6befdb4f786f0be2",
    strip_prefix = "protobuf-3.6.1.3",
    urls = ["https://github.com/protocolbuffers/protobuf/archive/v3.6.1.3.zip"],
)

http_archive(
    name = "boringssl",  # Must match upstream workspace name.
    # Gitiles creates gzip files with an embedded timestamp, so we cannot use
    # sha256 to validate the archives.  We must rely on the commit hash and
    # https. Commits must come from the master-with-bazel branch.
    urls = ["https://github.com/google/boringssl/archive/master-with-bazel.zip"],
    strip_prefix = "boringssl-master-with-bazel",
)

http_archive(
    name = "io_bazel",
    urls = ["https://github.com/bazelbuild/bazel/archive/master.zip"],
    strip_prefix = "bazel-master",
)

bind(
    name = "zlib",  # required by @com_google_protobuf
    actual = "@io_bazel//third_party/zlib:zlib",
)

http_archive(
    name   = "com_github_gflags_gflags",
    urls = ["https://github.com/gflags/gflags/archive/master.zip"],
    strip_prefix = "gflags-master",
)

http_archive(
    name = "com_google_re2",
    urls = ["https://github.com/google/re2/archive/master.zip"],
    strip_prefix = "re2-master",
)
