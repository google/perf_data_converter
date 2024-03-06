load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# bazel_skylib is a dependency of protobuf; this declaration must come before
# protobuf to override its internally-declared version.
http_archive(
    name = "bazel_skylib",
    urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz"],
    sha256 = "1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c",
)

# GoogleTest/GoogleMock framework. Used by most unit-tests.
# TODO(b/210576094): Unpin dependency after fixing compatibility.
http_archive(
     name = "com_google_googletest",
     urls = ["https://github.com/google/googletest/archive/release-1.11.0.zip"],
     strip_prefix = "googletest-release-1.11.0",
)

# zlib is a dependency of protobuf.
# Before proto, so that it is found with the latest version.
http_archive(
    name = "zlib",
    sha256 = "9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23",
    # This is the zlib BUILD file used in kythe:
    # https://github.com/kythe/kythe/blob/v0.0.30/third_party/zlib.BUILD
    build_file = "zlib.BUILD",
    urls = ["https://www.zlib.net/zlib-1.3.1.tar.gz"],
    strip_prefix = "zlib-1.3.1",
)

# Proto rules for Bazel and Protobuf
# TODO(b/210576094): Unpin dependency after fixing compatibility.
http_archive(
    name = "com_google_protobuf",
    sha256 = "8b28fdd45bab62d15db232ec404248901842e5340299a57765e48abe8a80d930",
    strip_prefix = "protobuf-3.20.1",
    urls = ["https://github.com/protocolbuffers/protobuf/archive/v3.20.1.tar.gz"],
)
load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")
protobuf_deps()

http_archive(
    name = "rules_proto",
    sha256 = "66bfdf8782796239d3875d37e7de19b1d94301e8972b3cbd2446b332429b4df1",
    strip_prefix = "rules_proto-4.0.0",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/rules_proto/archive/refs/tags/4.0.0.tar.gz",
        "https://github.com/bazelbuild/rules_proto/archive/refs/tags/4.0.0.tar.gz",
    ],
)

load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies", "rules_proto_toolchains")
rules_proto_dependencies()
rules_proto_toolchains()

# TODO(b/303695580): Unpin this dependency after fixing compatibility.
http_archive(
    name = "boringssl",
    sha256 = "0a2b7a10fdce3d5ccdc6abf4f5701dca24b97efa75b00d203c50221269605476",
    strip_prefix = "boringssl-ea4425fbb276871cfec5c4e19c12796b3cd1c9ab",
    urls = ["https://github.com/google/boringssl/archive/ea4425fbb276871cfec5c4e19c12796b3cd1c9ab.tar.gz"],
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
