#!/bin/bash

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]:-$0}"; )" &> /dev/null && pwd 2> /dev/null; )";

if [[ "X$1" == "X" ]]; then
    echo "Missing arg: DEP_DIR"
    echo "./install_deps.sh <DEP_DIR>"
    exit 1
fi

# CMAKE_C_COMPILER requires absolute path
DEP_DIR="$(realpath "$1")"
APT_CMD=${2:-"apt-get"}

${APT_CMD} update
${APT_CMD} update --fix-missing
export DEBIAN_FRONTEND='noninteractive'
export TZ='Etc/UTC'
${APT_CMD} install -y \
    build-essential \
    bzip2 \
    cmake \
    curl \
    file \
    git \
    libbz2-dev \
    libcurl4-openssl-dev \
    libgmp-dev \
    libncurses5 \
    libssl-dev \
    libtinfo-dev \
    libzstd-dev \
    python3 \
    python3-numpy \
    time \
    tzdata \
    unzip \
    wget \
    zip \
    zlib1g-dev

source "${SCRIPT_DIR}/utils.sh"
install_clang "${DEP_DIR}/clang-${CLANG_VER}"
install_llvm "${DEP_DIR}/llvm-${LLVM_VER}"
install_boost "${DEP_DIR}/boost_${BOOST_VER//\./_}patched"