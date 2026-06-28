# test_deref_method.cmake — codegen_addr_of deref-receiver fix.
# (*ptr).method() on a &!self/&self method must operate on the pointee, not a
# spilled copy. JIT + AOT ("DEREF OK") + memcheck 0/0/0.
# Required: LS_EXE, SAMPLE, WORK_DIR, STDLIB.
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE)
    message(FATAL_ERROR "test_deref_method.cmake requires LS_EXE and SAMPLE")
endif()
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(TN "deref_method")

execute_process(COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 30)
if(NOT jr EQUAL 0 OR jo MATCHES "FAIL" OR NOT jo MATCHES "DEREF OK")
    message(FATAL_ERROR "${TN} JIT: rc=${jr}\n${je}\n${jo}")
endif()

set(BIN "${WORK_DIR}/${TN}_aot")
if(WIN32)
    set(BIN "${BIN}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${BIN}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "${TN} AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${BIN}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 30)
file(REMOVE "${BIN}")
if(NOT ar EQUAL 0 OR ao MATCHES "FAIL" OR NOT ao MATCHES "DEREF OK")
    message(FATAL_ERROR "${TN} AOT run: rc=${ar}\n${ao}")
endif()

execute_process(COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT mr EQUAL 0 OR NOT me MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "${TN} memcheck:\n${me}")
endif()

message(STATUS "${TN}: JIT + AOT + memcheck PASS")
