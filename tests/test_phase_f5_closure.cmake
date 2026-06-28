# Phase F.5 closure test: enum capture in closures
# Tests: Direction (disc-only by-copy), Option(int) (by-copy), Result(int,string) (by-move),
#        factory pattern, Color (POD payload by-copy), vec(OptGetter), repeated call.
# Expected output (one per line):
#   None
#   42
#   error: not found
#   hello
#   200
#   Red
#   1
#   10

cmake_minimum_required(VERSION 3.20)

set(LS_EXE "${CMAKE_CURRENT_LIST_DIR}/../build/Release/ls.exe")
set(TEST_LS "${CMAKE_CURRENT_LIST_DIR}/samples/closure_f5_test.ls")
set(OUT_EXE "${CMAKE_CURRENT_LIST_DIR}/../build/closure_f5_test.exe")
set(OUT_MC  "${CMAKE_CURRENT_LIST_DIR}/../build/closure_f5_test_mc.exe")

set(EXPECTED "None\n42\nerror: not found\nhello\n200\nRed\n1\n10")

# ── Step 1: JIT run ──────────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS_EXE}" run "${TEST_LS}"
    OUTPUT_VARIABLE JIT_OUT
    ERROR_VARIABLE  JIT_ERR
    RESULT_VARIABLE JIT_RET
)
string(STRIP "${JIT_OUT}" JIT_OUT_S)
if(NOT "${JIT_OUT_S}" STREQUAL "${EXPECTED}")
    message(FATAL_ERROR "JIT output mismatch.\nExpected:\n${EXPECTED}\nGot:\n${JIT_OUT_S}\nStderr:\n${JIT_ERR}")
endif()
message(STATUS "[F.5] JIT output OK")

# ── Step 2: JIT memcheck ─────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${TEST_LS}"
    OUTPUT_VARIABLE MC_JIT_OUT
    ERROR_VARIABLE  MC_JIT_ERR
    RESULT_VARIABLE MC_JIT_RET
)
string(FIND "${MC_JIT_ERR}" "OK clean" MC_OK_POS)
if(MC_OK_POS EQUAL -1)
    string(FIND "${MC_JIT_OUT}" "OK clean" MC_OK_POS2)
    if(MC_OK_POS2 EQUAL -1)
        message(FATAL_ERROR "JIT memcheck did not report OK clean.\nStdout:\n${MC_JIT_OUT}\nStderr:\n${MC_JIT_ERR}")
    endif()
endif()
message(STATUS "[F.5] JIT memcheck OK")

# ── Step 3: AOT compile ──────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS_EXE}" compile "${TEST_LS}" -o "${OUT_EXE}"
    OUTPUT_VARIABLE AOT_COMP_OUT
    ERROR_VARIABLE  AOT_COMP_ERR
    RESULT_VARIABLE AOT_COMP_RET
)
if(NOT AOT_COMP_RET EQUAL 0)
    message(FATAL_ERROR "AOT compilation failed.\nStdout:\n${AOT_COMP_OUT}\nStderr:\n${AOT_COMP_ERR}")
endif()

execute_process(
    COMMAND "${OUT_EXE}"
    OUTPUT_VARIABLE AOT_OUT
    ERROR_VARIABLE  AOT_ERR
    RESULT_VARIABLE AOT_RET
)
string(STRIP "${AOT_OUT}" AOT_OUT_S)
if(NOT "${AOT_OUT_S}" STREQUAL "${EXPECTED}")
    message(FATAL_ERROR "AOT output mismatch.\nExpected:\n${EXPECTED}\nGot:\n${AOT_OUT_S}")
endif()
message(STATUS "[F.5] AOT output OK")

# ── Step 4: AOT memcheck ─────────────────────────────────────────────────────
execute_process(
    COMMAND "${LS_EXE}" compile --memcheck "${TEST_LS}" -o "${OUT_MC}"
    OUTPUT_VARIABLE MC_COMP_OUT
    ERROR_VARIABLE  MC_COMP_ERR
    RESULT_VARIABLE MC_COMP_RET
)
if(NOT MC_COMP_RET EQUAL 0)
    message(FATAL_ERROR "AOT memcheck compilation failed.\nStdout:\n${MC_COMP_OUT}\nStderr:\n${MC_COMP_ERR}")
endif()

execute_process(
    COMMAND "${OUT_MC}"
    OUTPUT_VARIABLE MC_AOT_OUT
    ERROR_VARIABLE  MC_AOT_ERR
    RESULT_VARIABLE MC_AOT_RET
)
set(MC_COMBINED "${MC_AOT_OUT}${MC_AOT_ERR}")
string(FIND "${MC_COMBINED}" "OK clean" MC_AOT_OK_POS)
if(MC_AOT_OK_POS EQUAL -1)
    message(FATAL_ERROR "AOT memcheck did not report OK clean.\nStdout:\n${MC_AOT_OUT}\nStderr:\n${MC_AOT_ERR}")
endif()
message(STATUS "[F.5] AOT memcheck OK")

message(STATUS "[F.5] All Phase F.5 closure tests passed!")
