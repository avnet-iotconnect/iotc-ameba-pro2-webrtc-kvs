#!/bin/bash
# Build script for iotc-ameba-pro2-webrtc-kvs
#
# Usage:
#   ./build.sh              — configure and build (default)
#   ./build.sh clean        — remove the build directory and exit
#   ./build.sh --viewer     — build the viewer application instead of master
#   ./build.sh --no-sctp    — disable SCTP data channel support
#   ./build.sh --metrics    — enable WebRTC metrics logging
#
# The Realtek cross-compiler must be on PATH before running this script.
# Download from: https://github.com/Ameba-AIoT/ameba-toolchain/releases/tag/V10.3.0-amebe-rtos-pro2

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${REPO_ROOT}/project/realtek_amebapro2_webrtc_application/GCC-RELEASE/build"
CMAKE_DIR="${REPO_ROOT}/project/realtek_amebapro2_webrtc_application/GCC-RELEASE"
TOOLCHAIN="${CMAKE_DIR}/toolchain.cmake"
OUTPUT_BIN="${BUILD_DIR}/flash_ntz.bin"

# --- Parse arguments -------------------------------------------------------

CMAKE_EXTRA_ARGS=""
for arg in "$@"; do
    case "$arg" in
        clean)
            echo "Removing build directory: ${BUILD_DIR}"
            rm -rf "${BUILD_DIR}"
            echo "Done."
            exit 0
            ;;
        --viewer)
            CMAKE_EXTRA_ARGS="${CMAKE_EXTRA_ARGS} -DBUILD_VIEWER_APPLICATION=ON"
            ;;
        --no-sctp)
            CMAKE_EXTRA_ARGS="${CMAKE_EXTRA_ARGS} -DBUILD_USRSCTP_LIBRARY=OFF"
            ;;
        --metrics)
            CMAKE_EXTRA_ARGS="${CMAKE_EXTRA_ARGS} -DMETRIC_PRINT_ENABLED=ON"
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 [clean | --viewer | --no-sctp | --metrics]"
            exit 1
            ;;
    esac
done

# --- Check prerequisites ---------------------------------------------------

if ! command -v cmake &>/dev/null; then
    echo "Error: cmake not found."
    echo "Install CMake 3.22 or later: https://cmake.org/download/"
    exit 1
fi

if ! command -v arm-none-eabi-gcc &>/dev/null; then
    echo "Error: arm-none-eabi-gcc not found on PATH."
    echo ""
    echo "Download the Realtek cross-compiler and add it to PATH:"
    echo "  https://github.com/Ameba-AIoT/ameba-toolchain/releases/tag/V10.3.0-amebe-rtos-pro2"
    echo ""
    echo "After extracting, run (Linux):"
    echo "  export PATH=/path/to/asdk-10.3.0/linux/newlib/bin:\$PATH"
    echo ""
    echo "Then re-run this script."
    exit 1
fi

# --- Mark SDK helper binaries executable -----------------------------------

MP_DIR="${REPO_ROOT}/libraries/ambpro2_sdk/project/realtek_amebapro2_v0_example/GCC-RELEASE/mp"
if [ -d "${MP_DIR}" ]; then
    chmod +x "${MP_DIR}"/* 2>/dev/null || true
fi

FLASH_TOOL_DIR="${REPO_ROOT}/libraries/ambpro2_sdk/tools/Pro2_PG_tool _v1.4.3"
if [ -d "${FLASH_TOOL_DIR}" ]; then
    chmod +x "${FLASH_TOOL_DIR}"/uartfwburn.* 2>/dev/null || true
fi

# --- Configure (only if not already configured) ----------------------------

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    echo "Configuring..."
    mkdir -p "${BUILD_DIR}"
    cmake -S "${CMAKE_DIR}" -B "${BUILD_DIR}" \
        -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
        ${CMAKE_EXTRA_ARGS}
else
    echo "Build directory already configured. Skipping cmake configure."
    echo "(Run './build.sh clean' first to force reconfigure.)"
fi

# --- Build -----------------------------------------------------------------

echo ""
echo "Building..."
cmake --build "${BUILD_DIR}" --target flash

# --- Report ----------------------------------------------------------------

echo ""
if [ -f "${OUTPUT_BIN}" ]; then
    SIZE=$(du -h "${OUTPUT_BIN}" | cut -f1)
    echo "Build successful."
    echo "  Output: ${OUTPUT_BIN} (${SIZE})"
    echo ""
    echo "To flash (Linux):"
    echo "  cd \"${REPO_ROOT}/libraries/ambpro2_sdk/tools/Pro2_PG_tool _v1.4.3\""
    echo "  sudo ./uartfwburn.linux -p /dev/ttyUSB0 -f \"${OUTPUT_BIN}\" -b 2000000 -U"
else
    echo "Warning: build target finished but ${OUTPUT_BIN} was not found."
    exit 1
fi
