#!/bin/bash

# Copyright (c) 2021, Google Inc.
# All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euox pipefail

if [ -z "${PDC_ROOT:-}" ]; then
  PDC_ROOT="$(realpath $(dirname ${0})/..)"
fi

if [ -z "${STD:-}" ]; then
  STD="c++17"
fi

if [ -z "${COMPILATION_MODE:-}" ]; then
  COMPILATION_MODE="fastbuild opt"
fi

if [ -z "${EXCEPTIONS_MODE:-}" ]; then
  EXCEPTIONS_MODE="-fno-exceptions -fexceptions"
fi

readonly DOCKER_CONTAINER="gcr.io/google.com/absl-177019/linux_hybrid-latest:20210525"

for std in ${STD}; do
  for compilation_mode in ${COMPILATION_MODE}; do
    for exceptions_mode in ${EXCEPTIONS_MODE}; do
      echo "--------------------------------------------------------------------"
      time docker run \
        --volume="${PDC_ROOT}:/perf_data_converter:ro" \
        --workdir=/perf_data_converter\
        --cap-add=SYS_PTRACE \
        --rm \
        -e CC="/opt/llvm/clang/bin/clang" \
        -e BAZEL_CXXOPTS="-std=${std}:-nostdinc++" \
        -e BAZEL_LINKOPTS="-L/opt/llvm/libcxx/lib:-lc++:-lc++abi:-lm:-Wl,-rpath=/opt/llvm/libcxx/lib" \
        -e CPLUS_INCLUDE_PATH="/opt/llvm/libcxx/include/c++/v1" \
        ${DOCKER_EXTRA_ARGS:-} \
        ${DOCKER_CONTAINER} \
        /usr/local/bin/bazel test ... \
          --compilation_mode="${compilation_mode}" \
          --copt="${exceptions_mode}" \
          --copt=-Werror \
          --distdir="/bazel-distdir" \
          --keep_going \
          --show_timestamps \
          --test_env="GTEST_INSTALL_FAILURE_SIGNAL_HANDLER=1" \
          --test_output=errors \
          ${BAZEL_EXTRA_ARGS:-}
    done
  done
done
