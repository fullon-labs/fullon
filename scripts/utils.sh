#!/bin/bash

# CMAKE_C_COMPILER requires absolute path
# DEP_DIR=

CLANG_VER=11.0.1
BOOST_VER=1.70.0
LLVM_VER=7.1.0

pushdir() {
    DIR="$1"
    mkdir -p "${DIR}"
    pushd "${DIR}" &> /dev/null
}

popdir() {
    EXPECTED="$1"
    D="$(popd)"
    popd &> /dev/null
    echo "${D}"
    D="$(eval echo "$D" | head -n1 | cut -d " " -f1)"

    # -ef compares absolute paths
    if ! [[ "${D}" -ef "${EXPECTED}" ]]; then
        echo "Directory is not where expected EXPECTED=${EXPECTED} at ${D}"
        exit 1
    fi
}

try(){
    "$@"
    res=$?
    if [[ ${res} -ne 0 ]]; then
        exit 255
    fi
}

install_clang() {
    CLANG_DIR="$1"
    if [ ! -d "${CLANG_DIR}" ]; then
        echo "Installing Clang ${CLANG_VER} @ ${CLANG_DIR}"
        mkdir -p "${CLANG_DIR}"
        CLANG_FN="clang+llvm-${CLANG_VER}-x86_64-linux-gnu-ubuntu-16.04.tar.xz"
        try wget -O "${CLANG_FN}" "https://github.com/llvm/llvm-project/releases/download/llvmorg-${CLANG_VER}/${CLANG_FN}"
        try tar -xvf "${CLANG_FN}" -C "${CLANG_DIR}"
        pushdir "${CLANG_DIR}"
        mv clang+*/* .
        popdir "${DEP_DIR}"
        rm "${CLANG_FN}"
    fi
    export PATH="${CLANG_DIR}/bin:$PATH"
    export CLANG_DIR="${CLANG_DIR}"
}

install_llvm() {
    LLVM_DIR="$1"
    if [ ! -d "${LLVM_DIR}" ]; then
        echo "Installing LLVM ${LLVM_VER} @ ${LLVM_DIR}"
        mkdir -p "${LLVM_DIR}"
        try wget -O "llvm-${LLVM_VER}.src.tar.xz" "https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VER}/llvm-${LLVM_VER}.src.tar.xz"
        try tar -xvf "llvm-${LLVM_VER}.src.tar.xz"
        pushdir "${LLVM_DIR}.src"
        pushdir build
        try cmake -DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/pinned_toolchain.cmake" -DCMAKE_INSTALL_PREFIX="${LLVM_DIR}" -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD=host -DLLVM_BUILD_TOOLS=Off -DLLVM_ENABLE_RTTI=On -DLLVM_ENABLE_TERMINFO=Off -DCMAKE_EXE_LINKER_FLAGS=-pthread -DCMAKE_SHARED_LINKER_FLAGS=-pthread -DLLVM_ENABLE_PIC=NO ..
        try make -j "${JOBS}"
        try make -j "${JOBS}" install
        popdir "${LLVM_DIR}.src"
        popdir "${DEP_DIR}"
        rm -rf "${LLVM_DIR}.src"
        rm "llvm-${LLVM_VER}.src.tar.xz"
    fi
    export LLVM_DIR="${LLVM_DIR}"
}

install_boost() {
    BOOST_DIR="$1"

    if [ ! -d "${BOOST_DIR}" ]; then
        echo "Installing Boost ${BOOST_VER} @ ${BOOST_DIR}"
        try wget -O "boost_${BOOST_VER//\./_}.tar.gz" "https://boostorg.jfrog.io/artifactory/main/release/${BOOST_VER}/source/boost_${BOOST_VER//\./_}.tar.gz"
        try tar --transform="s:^boost_${BOOST_VER//\./_}:boost_${BOOST_VER//\./_}patched:" -xvzf "boost_${BOOST_VER//\./_}.tar.gz" -C "${DEP_DIR}"
        pushdir "${BOOST_DIR}"
        patch -p1 < "${SCRIPT_DIR}/0001-beast-fix-moved-from-executor.patch"
        patch -p1 < "${SCRIPT_DIR}/0002-hana-fix-compile-warning-of-config.patch"
        try ./bootstrap.sh -with-toolset=clang --prefix="${BOOST_DIR}/boost_root"
        ./b2 toolset=clang cxxflags="-stdlib=libc++ -D__STRICT_ANSI__ -nostdinc++ -I\${CLANG_DIR}/include/c++/v1 -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE" linkflags='-stdlib=libc++ -pie' link=static threading=multi --with-iostreams --with-date_time --with-filesystem --with-system --with-program_options --with-chrono --with-test -q -j "${JOBS}" install
        popdir "${DEP_DIR}"
        rm "boost_${BOOST_VER//\./_}.tar.gz"
    fi
    export BOOST_ROOT="${BOOST_DIR}/boost_root"
}

