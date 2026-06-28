# test_chan_forin.cmake — std.chan `for x in ch` Iterator(T) protocol (Phase 4).
# A producer thread streams N items then closes; main drains with for-in. JIT +
# 6x AOT, no memcheck (threaded). A broken iterator skews the count or hangs.
# Required: LS_EXE, SAMPLE, WORK_DIR, STDLIB.
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE)
    message(FATAL_ERROR "test_chan_forin.cmake requires LS_EXE and SAMPLE")
endif()
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(TN "chan_forin")

execute_process(COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 60)
if(NOT jr EQUAL 0 OR jo MATCHES "FAIL" OR NOT jo MATCHES "FORIN OK")
    message(FATAL_ERROR "${TN} JIT failed/hung: rc=${jr}\n${je}\n${jo}")
endif()

set(BIN "${WORK_DIR}/${TN}_aot")
if(WIN32)
    set(BIN "${BIN}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${BIN}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 60)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "${TN} AOT compile failed:\n${ce}")
endif()
foreach(i RANGE 1 6)
    execute_process(COMMAND "${BIN}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 60)
    if(NOT ar EQUAL 0 OR ao MATCHES "FAIL" OR NOT ao MATCHES "FORIN OK")
        message(FATAL_ERROR "${TN} AOT run ${i} failed/hung: rc=${ar}\n${ao}")
    endif()
endforeach()
file(REMOVE "${BIN}")

message(STATUS "${TN}: JIT + 6x AOT for-in PASS")
