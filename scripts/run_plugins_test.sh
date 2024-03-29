#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")"; pwd -P)"
REPOS_DIR="$(cd "${SCRIPT_DIR}/.."; pwd -P)"
BUILD_DIR="${BUILD_DIR:-"${REPOS_DIR}/build"}"

cd "${BUILD_DIR}/plugins" && \
  find "${BUILD_DIR}/plugins" -type f -executable -print | grep '/test_[^/.]*$'  | xargs -I '{}' bash -c "echo ç {} && {}"