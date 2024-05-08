#!/bin/bash
set -eo pipefail

echo "Leap Pinned Build"

if [[ "$(uname)" == "Linux" ]]; then
    if [[ -e /etc/os-release ]]; then
        # obtain NAME and other information
        . /etc/os-release
        if [[ "${NAME}" != "Ubuntu" ]]; then
            echo "Currently only supporting Ubuntu based builds. Proceed at your own risk."
        fi
    else
        echo "Currently only supporting Ubuntu based builds. /etc/os-release not found. Your Linux distribution is not supported. Proceed at your own risk."
    fi
else
    echo "Currently only supporting Ubuntu based builds. Your architecture is not supported. Proceed at your own risk."
fi

if [ $# -eq 0 ] || [ -z "$1" ]; then
    echo "Please supply a directory for the build dependencies to be placed and a directory for leap build and a value for the number of jobs to use for building."
    echo "The binary packages will be created and placed into the leap build directory."
    echo "./pinned_build.sh <dependencies directory> <leap build directory> <1-100>"
    exit 255
fi

export CORE_SYM='EOS'
# CMAKE_C_COMPILER requires absolute path
DEP_DIR="$(realpath "$1")"
LEAP_DIR="$2"
JOBS="$3"
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]:-$0}"; )" &> /dev/null && pwd 2> /dev/null; )";
START_DIR="$(pwd)"

# env
# CMAKE_BUILD_TYPE Relase | Debug | RelWithDebInfo | MinSizeRel, default is "Release", see https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html
CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-"Release"}
# CMAKE_ARGS more args for cmake, for example, below will enable compiling test contracts
# export CMAKE_ARGS="-DEOSIO_COMPILE_TEST_CONTRACTS=true"

source "${SCRIPT_DIR}/utils.sh"

pushdir "${DEP_DIR}"

install_clang "${DEP_DIR}/clang-${CLANG_VER}"
install_llvm "${DEP_DIR}/llvm-${LLVM_VER}"
install_boost "${DEP_DIR}/boost_${BOOST_VER//\./_}patched"

# go back to the directory where the script starts
popdir "${START_DIR}"

pushdir "${LEAP_DIR}"

# build Leap
echo "Building Leap ${SCRIPT_DIR}"
try cmake ${CMAKE_ARGS} -DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/pinned_toolchain.cmake" -DCMAKE_INSTALL_PREFIX=${LEAP_PINNED_INSTALL_PREFIX:-/usr/local} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_PREFIX_PATH="${LLVM_DIR}/lib/cmake" "${SCRIPT_DIR}/.."

try make -j "${JOBS}"
try cpack

# art generated with DALL-E (https://openai.com/blog/dall-e), then fed through ASCIIart.club (https://asciiart.club) with permission
cat <<'TXT' # BASH interpolation must remain disabled for the ASCII art to print correctly

                                ,▄▄A`
                             _╓▄██`
                          ╓▄▓▓▀▀`
                        ╓▓█▀╓▄▓
                        ▓▌▓▓▓▀
                      ,▄▓███▓H
                    _╨╫▀╚▀╠▌`╙¥,
                  ╓«    _╟▄▄   `½,                              ╓▄▄╦▄≥_
                 ╙▓╫╬▒R▀▀╙▀▀▓φ_ «_╙Y╥▄mmMM#╦▄,_          ,╓╦mM╩╨╙╙╙\`║═
                   ``       `▀▄__╫▓▓╨`    _```"""*ⁿⁿ^`````Ω,        `╟∩
                              ╙▌▓▓"`    ,«ñ`            ╔╬▓▌⌂        ╔▌
                               ╙█▌,,╔╗M╨,░             `  "╫▓m_      ╟H      _
                        _,,,,__,╠█▓▒`  .╣▌µ _       _.╓╔▄▄▓█▓▓N_     ╙▀╩KKM╙╟▓N
                      ,▄▓█▓████▀▀▀╙▀╓╔φ»█▓▓Ñ╦«, :»»µ╦▓▓█▀└╙▀███▓╥__   _,╓▄▓▓▓M▓`,
                  __╓Φ▓█╫▓▓▓▓▓▓▓▓▓▓▓▓▓▀K▀▀███▓▓▓▓▓▓▀▀╙       `▀▀▀▓▄▄K╨╙└   `▀▌╙█▄*.
                ,╓Φ▓▓▀▄▓▀`         ╙▀╙                                       ╙▓╙╙▓▄*
              .▄▓╫▀╦▄▀`                                                       ╙▓╙µ╙▀
             ▄▓▀╨▓▓╨_                                                          `▀▄▄M
            _█▌▄▌╙`                                                               `╙
             ╙└`


                Ñ▓▓▓▓          ¢▄▄▄▄▄▄▄▄▄▄         ,           ,,,,,,,,,,,_
               _╫▓▓█▌          ╟▓████████▀       ╓╣▓▌_         ╠╫▓█▓▓▓▓█▓▓▓▓▄
               _▓▓▓█▌          ╟╫██▄,,,,_       æ▄███▓▄        ▐║████▀╨╨▀▓███▌
               :╫▓▓█▌          ╟╢████████▓    ,╬███████▓,      ▐║████▓▄▄▓▓███▀
               :╫▓▓█▌_        _╟║███▀▀▀▀▀▀   ╓╫███╣╫╬███▓N_    j▐██▀▀▀▀▀▀▀▀▀`
    ___________]╫▓▓█▓φ╓╓╓╓╓╓,__╟╣█▌▌,,,,,_ _╬▓████████████▓▄_  ▐M█▌
    _ _________]╫▌▓█████████▓▓▄╟╣████████▓▄╣██▓▀^    _ ╙▓██▓▓╗__M█▓
      ___   _ _ ▀╣▀╣╩╩╩╩╩╩╩▀▀▀▀╙▀▀▀▀▀▀▀▀▀▀▀▀▀▀`          ╙▀▀▀▀╩═╩▀▀
     _ __ __ _____ ___  ____ _  __  _  ____ ___
    ____ ____ __  _ _  _ _ _ _ __   _ __ __
    __ _ ________  ___ ________   ___ _ _ _      _
       _ __  __     _ __ ____ _     _ ____ _   _ _       _
     _  _ _     ____ ____ _ _    _ __  _ _ _ __

---
Leap has successfully built and constructed its packages. You should be able to
find the packages at:
TXT
echo "${LEAP_DIR}"
echo
echo 'Thank you, fam!!'
echo
