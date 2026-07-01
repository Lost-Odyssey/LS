# Regression for B-MAP-OPT-001: owned rvalue Option(has_drop) match subject must
# drop exactly once even when the arm has nested control flow (for/while). Fixed
# by idempotent emit_enum_drop. JIT + memcheck 0/0/0 + AOT. Prints "OPTPAY PASS".

cmake_minimum_required(VERSION 3.20)
set(LS  "${LS_EXE}")
set(SRC "${CMAKE_CURRENT_LIST_DIR}/samples/map_option_payload_test.lls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/map_option_payload.exe")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

execute_process(COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 30)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "map_option_payload JIT failed (rc=${jr}):\n${je}\n${jo}")
endif()
if(NOT jo MATCHES "OPTPAY PASS" OR jo MATCHES "FAIL ")
    message(FATAL_ERROR "map_option_payload JIT: bad output:\n${jo}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT me MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "map_option_payload memcheck mismatch:\n${me}")
endif()

execute_process(COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "map_option_payload AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${OUT_EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ao MATCHES "OPTPAY PASS" OR ao MATCHES "FAIL ")
    message(FATAL_ERROR "map_option_payload AOT: bad output (rc=${ar}):\n${ao}")
endif()

message(STATUS "map_option_payload all passed")
