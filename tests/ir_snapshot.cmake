# tests/ir_snapshot.cmake — IR snapshot comparison driver, run via `cmake -P`.
#
# Executes `${LS_EXE} emit-ir ${SAMPLE}` (LLVM IR text is written to
# **stderr**, not stdout — see src/main.c's emit-ir handler) and compares the
# result byte-for-byte against the golden file at ${GOLDEN}, after stripping
# the two header lines that embed the sample's absolute source path
# (`; ModuleID = '...'` and `source_filename = "..."`) — those vary with
# checkout location and carry no semantic content.
#
# This is the shared verification gate for "zero behavior change" refactors:
# regenerate golden files only when an IR change is *intentional* (see
# tests/regen_ir_golden.sh), never to silence a snapshot test.
#
# Usage:
#   cmake -DLS_EXE=<path to lls.exe> \
#         -DSAMPLE=<absolute path to .lls sample> \
#         -DGOLDEN=<absolute path to tests/ir_golden/<name>.ll> \
#         -DNAME=<snapshot name, used for diagnostics/actual-output file> \
#         -DWORK_DIR=<scratch dir for the .actual.ll dump on mismatch> \
#         -P tests/ir_snapshot.cmake
cmake_minimum_required(VERSION 3.20)

foreach(_required_var LS_EXE SAMPLE GOLDEN NAME WORK_DIR)
    if(NOT DEFINED ${_required_var})
        message(FATAL_ERROR "ir_snapshot.cmake: missing required -D${_required_var}=...")
    endif()
endforeach()

if(NOT EXISTS "${SAMPLE}")
    message(FATAL_ERROR "ir_snapshot(${NAME}): sample not found: ${SAMPLE}")
endif()
if(NOT EXISTS "${GOLDEN}")
    message(FATAL_ERROR "ir_snapshot(${NAME}): golden file not found: ${GOLDEN}\n"
        "Run tests/regen_ir_golden.sh to generate it (only when the IR change is intentional).")
endif()

execute_process(
    COMMAND "${LS_EXE}" emit-ir "${SAMPLE}"
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _ir
    RESULT_VARIABLE _rc
)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "ir_snapshot(${NAME}): emit-ir FAILED rc=${_rc}\n${_ir}\n${_out}")
endif()

file(READ "${GOLDEN}" _gold)

# Normalize: strip the ModuleID and source_filename header lines (embed the
# absolute sample path, differ across checkouts/machines). Everything else
# must match byte-for-byte, including line endings.
string(REGEX REPLACE "; ModuleID[^\n]*\n" "" _ir "${_ir}")
string(REGEX REPLACE "source_filename[^\n]*\n" "" _ir "${_ir}")
string(REGEX REPLACE "; ModuleID[^\n]*\n" "" _gold "${_gold}")
string(REGEX REPLACE "source_filename[^\n]*\n" "" _gold "${_gold}")

if(NOT _ir STREQUAL _gold)
    file(WRITE "${WORK_DIR}/${NAME}.actual.ll" "${_ir}")
    message(FATAL_ERROR "ir_snapshot(${NAME}): IR snapshot mismatch.\n"
        "  golden: ${GOLDEN}\n"
        "  actual: ${WORK_DIR}/${NAME}.actual.ll\n"
        "If this change is intentional, review the diff then re-run tests/regen_ir_golden.sh.")
endif()

message(STATUS "ir_snapshot(${NAME}): OK (matches ${GOLDEN})")
