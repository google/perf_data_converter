#!/bin/bash

# Copyright (c) 2021, Google Inc.
# All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euox pipefail

if [ -z "${PDC_ROOT:-}" ]; then
  PDC_ROOT="$(realpath $(dirname ${0})/..)"
fi

# This container is a reasonable start but is missing some prerequisite libs and
# an installation of linux_perf. We'll manually add them below.
readonly DOCKER_CONTAINER="gcr.io/google.com/absl-177019/linux_hybrid-latest:20210617"

time docker run \
    --volume="${PDC_ROOT}:/perf_data_converter:ro" \
    --workdir=/perf_data_converter \
    --cap-add=SYS_PTRACE \
    --rm \
    -i \
    -e CC="/opt/llvm/clang/bin/clang" \
    -e BAZEL_CXXOPTS="-nostdinc++" \
    -e BAZEL_LINKOPTS="-L/opt/llvm/libcxx/lib:-lc++:-lc++abi:-lm:-Wl,-rpath=/opt/llvm/libcxx/lib" \
    -e CPLUS_INCLUDE_PATH="/opt/llvm/libcxx/include/c++/v1" \
    ${DOCKER_EXTRA_ARGS:-} \
    ${DOCKER_CONTAINER} \
    bash <<EOF
      apt-get -y update
      apt-get -y install linux-perf
      # /usr/bin/perf has heuristics that reason perf_5.0 exists in this image.
      ln -s /usr/bin/perf_5.10 /usr/bin/perf_5.0
      rm -rf ~/.cache/bazel
      apt-get -y install g++ git libelf-dev libcap-dev
      git submodule init
      git submodule update
      /usr/local/bin/bazel build src:all
      /usr/local/bin/bazel build src/quipper:all
      /usr/local/bin/bazel test ... \
        --compilation_mode=fastbuild \
        --copt=-fexceptions \
        --copt=-Werror \
        --distdir="/bazel-distdir" \
        --keep_going \
        --show_timestamps \
        --test_env="GTEST_INSTALL_FAILURE_SIGNAL_HANDLER=1" \
        --test_output=errors \
        ${BAZEL_EXTRA_ARGS:-}
EOF
