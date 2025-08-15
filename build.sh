#!/bin/bash
set -e

BUILD_TYPE=Release
RUN_AFTER_BUILD=0
RUN_FILE=""

for arg in "$@"; do
    case $arg in
        --debug)
            BUILD_TYPE=Debug
            ;;
        --release)
            BUILD_TYPE=Release
            ;;
        --runfile=*)
            RUN_AFTER_BUILD=1
            RUN_FILE="${arg#*=}"
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Usage: $0 [--debug|--release] [--runfile=FILENAME]"
            exit 1
            ;;
    esac
done

mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE ..
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
# ./build.sh --release --runfile=test.txt :   builds in Release mode and runs with test.txt
# ./build.sh --debug --runfile=example.src :  builds in Debug mode and runs with example.src
