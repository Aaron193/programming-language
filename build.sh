#!/bin/bash
set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE=Release
RUN_AFTER_BUILD=0
RUN_FILE=""
ENABLE_LTO=0
ENABLE_ASAN=0
ENABLE_UBSAN=0
ENABLE_PGO_GENERATE=0
ENABLE_PGO_USE=0
ENABLE_PROFILING=0
PGO_PROFILE_PATH=""

for arg in "$@"; do
    case $arg in
        --debug)
            BUILD_TYPE=Debug
            ;;
        --release)
            BUILD_TYPE=Release
            ;;
        --lto)
            ENABLE_LTO=1
            ;;
        --asan)
            ENABLE_ASAN=1
            ;;
        --ubsan)
            ENABLE_UBSAN=1
            ;;
        --pgo-generate)
            ENABLE_PGO_GENERATE=1
            ;;
        --pgo-generate=*)
            ENABLE_PGO_GENERATE=1
            PGO_PROFILE_PATH="${arg#*=}"
            ;;
        --pgo-use=*)
            ENABLE_PGO_USE=1
            PGO_PROFILE_PATH="${arg#*=}"
            ;;
        --profiling)
            ENABLE_PROFILING=1
            ;;
        --runfile=*)
            RUN_AFTER_BUILD=1
            RUN_FILE="${arg#*=}"
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Usage: $0 [--debug|--release] [--lto] [--asan] [--ubsan] [--profiling] [--pgo-generate[=PATH]|--pgo-use=PATH] [--runfile=FILENAME]"
            exit 1
            ;;
    esac
done

if [[ $ENABLE_PGO_GENERATE -eq 1 && $ENABLE_PGO_USE -eq 1 ]]; then
    echo "Error: --pgo-generate and --pgo-use cannot be used together."
    exit 1
fi

if [[ $ENABLE_PROFILING -eq 1 && ($ENABLE_PGO_GENERATE -eq 1 || $ENABLE_PGO_USE -eq 1) ]]; then
    echo "Error: --profiling cannot be combined with --pgo-generate or --pgo-use."
    exit 1
fi

if [[ ($ENABLE_ASAN -eq 1 || $ENABLE_UBSAN -eq 1) && $ENABLE_LTO -eq 1 ]]; then
    echo "Error: sanitizers cannot be combined with --lto."
    exit 1
fi

if [[ ($ENABLE_ASAN -eq 1 || $ENABLE_UBSAN -eq 1) && ($ENABLE_PGO_GENERATE -eq 1 || $ENABLE_PGO_USE -eq 1) ]]; then
    echo "Error: sanitizers cannot be combined with --pgo-generate or --pgo-use."
    exit 1
fi

if [[ $ENABLE_PGO_USE -eq 1 && -z "$PGO_PROFILE_PATH" ]]; then
    echo "Error: --pgo-use requires a profile path."
    exit 1
fi

if [[ $ENABLE_PGO_GENERATE -eq 1 && -z "$PGO_PROFILE_PATH" ]]; then
    PGO_PROFILE_PATH="$ROOT_DIR/build/pgo-data"
fi

mkdir -p build
cd build
cmake_args=(
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
    "-DENABLE_LTO=OFF"
    "-DENABLE_ASAN=OFF"
    "-DENABLE_UBSAN=OFF"
    "-DENABLE_PGO_GENERATE=OFF"
    "-DENABLE_PGO_USE=OFF"
    "-DENABLE_PROFILING=OFF"
)

if [[ $ENABLE_LTO -eq 1 ]]; then
    cmake_args+=("-DENABLE_LTO=ON")
fi

if [[ $ENABLE_ASAN -eq 1 ]]; then
    cmake_args+=("-DENABLE_ASAN=ON")
fi

if [[ $ENABLE_UBSAN -eq 1 ]]; then
    cmake_args+=("-DENABLE_UBSAN=ON")
fi

if [[ $ENABLE_PGO_GENERATE -eq 1 ]]; then
    cmake_args+=("-DENABLE_PGO_GENERATE=ON" "-DPGO_PROFILE_PATH=$PGO_PROFILE_PATH")
fi

if [[ $ENABLE_PGO_USE -eq 1 ]]; then
    cmake_args+=("-DENABLE_PGO_USE=ON" "-DPGO_PROFILE_PATH=$PGO_PROFILE_PATH")
fi

if [[ $ENABLE_PROFILING -eq 1 ]]; then
    cmake_args+=("-DENABLE_PROFILING=ON")
fi

cmake "${cmake_args[@]}" ..
cmake --build . --parallel

if [[ $RUN_AFTER_BUILD -eq 1 ]]; then
    if [[ -n "$RUN_FILE" && -f "$RUN_FILE" ]]; then
        ./interpreter "$RUN_FILE"
    else
        echo "Error: File '$RUN_FILE' not found or no file specified"
        echo "Usage: $0 [--debug|--release] --runfile=FILENAME"
        exit 1
    fi
fi

# Usage:
# ./build.sh --release :                      builds in Release mode
# ./build.sh --debug :                        builds in Debug mode
# ./build.sh --release --lto :                builds with LTO enabled
# ./build.sh --debug --asan :                 builds with AddressSanitizer
# ./build.sh --debug --ubsan :                builds with UndefinedBehaviorSanitizer
# ./build.sh --debug --asan --ubsan :         builds with both sanitizers
# ./build.sh --release --profiling :          builds a profiler-friendly optimized binary
# ./build.sh --release --pgo-generate :       builds with PGO instrumentation (default profile path build/pgo-data)
# ./build.sh --release --pgo-use=/tmp/pgo :   builds using collected PGO profile data
# ./build.sh --release --runfile=test.txt :   builds in Release mode and runs with test.txt
# ./build.sh --debug --runfile=example.src :  builds in Debug mode and runs with example.src
