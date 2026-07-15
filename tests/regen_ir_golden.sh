#!/bin/bash
# Regenerate the golden .ll files for the IR snapshot tests (tests/ir_snapshot.cmake).
#
# ONLY run this when the IR change is INTENTIONAL — review the diff (`git diff
# tests/ir_golden/`) before committing regenerated goldens. This script exists
# so "zero behavior change" refactors have a single place to re-baseline when
# a change genuinely is expected to alter emitted IR (e.g. adding a new
# codegen feature), never as a reflex to make a red snapshot test go green.
#
# Registered snapshot names + their sample sources live in the array below;
# keep it in sync with the `ls_ir_snapshot(...)` registrations in
# tests/tests.cmake.
#
# Windows-only facility: golden files embed the host's target datalayout /
# triple (x86_64-pc-windows-msvc), so they are only generated and compared on
# Windows (the snapshot tests are gated `if(WIN32)` in tests/tests.cmake).
#
# IMPORTANT: run this from bash (Git Bash on Windows), never from PowerShell.
# PowerShell's `2>` redirection writes UTF-16 files; bash's grep/diff/cmake
# string comparisons expect UTF-8/ASCII and would silently mismatch against
# a PowerShell-generated golden file.
#
#   LS_EXE=/path/to/lls.exe ./regen_ir_golden.sh
set -e
cd "$(dirname "$0")"

LS_EXE="${LS_EXE:-../build/Release/lls.exe}"
GOLDEN_DIR="ir_golden"
SAMPLE_DIR="samples"

if [ ! -x "$LS_EXE" ]; then
    echo "error: LS_EXE not found or not executable: $LS_EXE" >&2
    echo "Build it first (cmake --build build --config Release --target ls) or set LS_EXE=..." >&2
    exit 1
fi

mkdir -p "$GOLDEN_DIR"

# name -> sample file (relative to tests/samples/)
declare -A SNAPSHOTS=(
    [enum_basic_test]="enum_basic_test.lls"
    [closure_g]="closure_g.lls"
    [match_own_stress_test]="match_own_stress_test.lls"
)

for name in "${!SNAPSHOTS[@]}"; do
    sample="$SAMPLE_DIR/${SNAPSHOTS[$name]}"
    golden="$GOLDEN_DIR/$name.ll"
    if [ ! -f "$sample" ]; then
        echo "error: sample not found: $sample" >&2
        exit 1
    fi
    # IR is written to stderr, not stdout — see src/main.c's emit-ir handler.
    "$LS_EXE" emit-ir "$sample" 2> "$golden" 1> /dev/null
    echo "regenerated $golden"
done

echo "Done. Review with: git diff -- $GOLDEN_DIR"
