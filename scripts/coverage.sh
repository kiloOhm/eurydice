#!/bin/bash
# Builds the unit tests with llvm source coverage, runs them, and enforces a
# line-coverage threshold on the core (non-UI) sources.
#
# Usage: scripts/coverage.sh [threshold]   (default 80)
set -euo pipefail
cd "$(dirname "$0")/.."

THRESHOLD="${1:-80}"
BUILD_DIR=build-cov
COVER_FLAGS="-fprofile-instr-generate -fcoverage-mapping"

cmake -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="$COVER_FLAGS" \
    -DCMAKE_C_FLAGS="$COVER_FLAGS" \
    -DCMAKE_EXE_LINKER_FLAGS="$COVER_FLAGS" > /dev/null

cmake --build "$BUILD_DIR" --target EurydiceTests

TEST_BIN=$(find "$BUILD_DIR/EurydiceTests_artefacts" -name EurydiceTests -type f | head -1)
[ -n "$TEST_BIN" ] || { echo "test binary not found"; exit 1; }

PROFRAW="$BUILD_DIR/eurydice.profraw"
PROFDATA="$BUILD_DIR/eurydice.profdata"

LLVM_PROFILE_FILE="$PROFRAW" "$TEST_BIN"

xcrun llvm-profdata merge -sparse "$PROFRAW" -o "$PROFDATA"

# Core sources only: UI needs a GUI harness and is excluded from the goal.
CORE_SOURCES=$(find src/model src/engine src/control src/plugins \
    -name '*.cpp' -o -name '*.h' | grep -v PluginWindowManager)

echo
echo "=== Coverage report (core sources) ==="
xcrun llvm-cov report "$TEST_BIN" -instr-profile="$PROFDATA" $CORE_SOURCES

TOTAL_LINE_COV=$(xcrun llvm-cov report "$TEST_BIN" -instr-profile="$PROFDATA" $CORE_SOURCES \
    | awk '/^TOTAL/ { gsub("%","",$(NF-3)); print $(NF-3) }')

echo
echo "Total line coverage (core): ${TOTAL_LINE_COV}%  (goal: ${THRESHOLD}%)"
if awk -v c="$TOTAL_LINE_COV" -v t="$THRESHOLD" 'BEGIN { exit (c >= t) ? 0 : 1 }'; then
    echo "PASS"
else
    echo "FAIL: below threshold"
    exit 1
fi
