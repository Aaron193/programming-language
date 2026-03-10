#!/bin/bash
set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE=Release
RUN_AFTER_BUILD=0
RUN_FILE=""
ENABLE_LTO=0
ENABLE_PGO_GENERATE=0
ENABLE_PGO_USE=0
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
        --runfile=*)
            RUN_AFTER_BUILD=1
            RUN_FILE="${arg#*=}"
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Usage: $0 [--debug|--release] [--lto] [--pgo-generate[=PATH]|--pgo-use=PATH] [--runfile=FILENAME]"
            exit 1
            ;;
    esac
done

if [[ $ENABLE_PGO_GENERATE -eq 1 && $ENABLE_PGO_USE -eq 1 ]]; then
    echo "Error: --pgo-generate and --pgo-use cannot be used together."
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
    "-DENABLE_PGO_GENERATE=OFF"
    "-DENABLE_PGO_USE=OFF"
)

if [[ $ENABLE_LTO -eq 1 ]]; then
    cmake_args+=("-DENABLE_LTO=ON")
fi

if [[ $ENABLE_PGO_GENERATE -eq 1 ]]; then
    cmake_args+=("-DENABLE_PGO_GENERATE=ON" "-DPGO_PROFILE_PATH=$PGO_PROFILE_PATH")
fi

if [[ $ENABLE_PGO_USE -eq 1 ]]; then
    cmake_args+=("-DENABLE_PGO_USE=ON" "-DPGO_PROFILE_PATH=$PGO_PROFILE_PATH")
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
# ./build.sh --release --pgo-generate :       builds with PGO instrumentation (default profile path build/pgo-data)
# ./build.sh --release --pgo-use=/tmp/pgo :   builds using collected PGO profile data
# ./build.sh --release --runfile=test.txt :   builds in Release mode and runs with test.txt
# ./build.sh --debug --runfile=example.src :  builds in Debug mode and runs with example.src
