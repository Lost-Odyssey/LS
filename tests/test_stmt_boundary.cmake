# test_stmt_boundary.cmake — parser statement-boundary regressions (L-003/L-004).
#   L-003: line-leading `*K p` generic pointer decl after a value-ending stmt is
#          not swallowed as multiplication. L-004: if/while condition starting
#          with `(` keeps its trailing infix op. Plus multiplication guards.
cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(F "${CMAKE_CURRENT_LIST_DIR}/samples/stmt_boundary_test.lls")

execute_process(COMMAND "${LS}" run "${F}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 30)
if(NOT sr EQUAL 0 OR NOT so MATCHES "STMT OK" OR so MATCHES "STMT FAIL")
    message(FATAL_ERROR "stmt_boundary JIT bad (rc=${sr}):\n${se}\n${so}")
endif()

set(EXE "${CMAKE_BINARY_DIR}/stmt_boundary_test.exe")
execute_process(COMMAND "${LS}" compile "${F}" -o "${EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "stmt_boundary AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ar EQUAL 0 OR NOT ao MATCHES "STMT OK" OR ao MATCHES "STMT FAIL")
    message(FATAL_ERROR "stmt_boundary AOT run: rc=${ar}\n${ao}")
endif()

message(STATUS "test_stmt_boundary: ALL PASSED")
