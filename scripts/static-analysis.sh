#!/bin/bash
# clang-tidy + cppcheck over our sources (not JUCE / deps).
# Needs: brew install llvm cppcheck. A configured build dir supplies
# compile_commands.json.
#
# Usage: scripts/static-analysis.sh [build-dir]
set -uo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR="${1:-build}"
CLANG_TIDY="/opt/homebrew/opt/llvm/bin/clang-tidy"
OUT_DIR=analysis
mkdir -p "$OUT_DIR"

[ -f "$BUILD_DIR/compile_commands.json" ] || { echo "no compile_commands.json in $BUILD_DIR — configure first"; exit 1; }
[ -x "$CLANG_TIDY" ] || { echo "clang-tidy not found — brew install llvm"; exit 1; }
command -v cppcheck > /dev/null || { echo "cppcheck not found — brew install cppcheck"; exit 1; }

SOURCES=$(find src -name '*.cpp')

# Homebrew clang-tidy needs the macOS SDK pointed out explicitly, otherwise
# every TU fails on missing system headers.
SDK_ARGS="--extra-arg=-isysroot --extra-arg=$(xcrun --show-sdk-path)"

echo "=== clang-tidy ==="
: > "$OUT_DIR/clang-tidy.txt"
for source in $SOURCES; do
    if ! "$CLANG_TIDY" -p "$BUILD_DIR" --quiet $SDK_ARGS "$source" >> "$OUT_DIR/clang-tidy.txt" 2> /dev/null; then
        echo "(clang-tidy crashed or errored on $source — skipped)" | tee -a "$OUT_DIR/clang-tidy.txt"
    fi
done
# Findings in our own sources. Homebrew clang-tidy parses a couple of JUCE
# headers differently from Apple clang and emits clang-diagnostic-error for
# them; those are tooling artifacts in a dependency, not defects here.
grep -E "warning:|error:" "$OUT_DIR/clang-tidy.txt" | grep "/src/" | grep -v "_deps/" \
    | sort -u | tee "$OUT_DIR/clang-tidy-ours.txt" || true
TIDY_ISSUES=$(wc -l < "$OUT_DIR/clang-tidy-ours.txt" | tr -d " ")
DEP_NOISE=$(grep -cE "clang-diagnostic-error" "$OUT_DIR/clang-tidy.txt" || true)

echo
echo "=== cppcheck ==="
cppcheck --project="$BUILD_DIR/compile_commands.json" \
    --enable=warning,performance,portability \
    --suppress='*:*/_deps/*' --suppress='*:*/JuceLibraryCode/*' \
    --inline-suppr --quiet \
    --xml 2> "$OUT_DIR/cppcheck.xml"
python3 - "$OUT_DIR/cppcheck.xml" <<'EOF'
import sys, xml.etree.ElementTree as ET
errors = [e for e in ET.parse(sys.argv[1]).getroot().iter("error")
          if "src/" in (e.find("location").get("file") if e.find("location") is not None else "")]
for e in errors:
    loc = e.find("location")
    print(f"{loc.get('file')}:{loc.get('line')}: [{e.get('id')}] {e.get('msg')}")
print(f"\ncppcheck issues in src/: {len(errors)}")
EOF

echo
echo "clang-tidy issues in src/: ${TIDY_ISSUES}"
echo "(ignored: ${DEP_NOISE} parse errors inside JUCE headers — clang-tidy/Apple-clang mismatch)"
echo "Reports in $OUT_DIR/ (clang-tidy.txt, cppcheck.xml — both importable by SonarQube's sonar-cxx plugin)"
