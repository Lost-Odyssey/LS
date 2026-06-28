# test_chan.cmake — std.chan single-threaded correctness + memcheck.
# JIT + AOT ("CHAN PASS", no "FAIL") + JIT --memcheck 0/0/0 (incl. __drop residual).
# Required: LS_EXE, SAMPLE, WORK_DIR, STDLIB (repo root → LS_HOME).
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE)
    message(FATAL_ERROR "test_chan.cmake requires LS_EXE and SAMPLE")
endif()
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(TN "chan")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 30)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "${TN} JIT failed (rc=${jr})\n${je}\n${jo}")
endif()
if(jo MATCHES "FAIL" OR NOT jo MATCHES "CHAN PASS")
    message(FATAL_ERROR "${TN} JIT correctness:\n${jo}")
endif()

# ---- AOT ----
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
if(NOT ar EQUAL 0 OR ao MATCHES "FAIL" OR NOT ao MATCHES "CHAN PASS")
    message(FATAL_ERROR "${TN} AOT run: rc=${ar}\n${ao}")
endif()

# ---- memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT mr EQUAL 0)
    message(FATAL_ERROR "${TN} memcheck run failed (rc=${mr})\n${me}")
endif()
if(NOT me MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "${TN} memcheck SUMMARY mismatch:\n${me}")
endif()

message(STATUS "${TN}: JIT + AOT + memcheck PASS")
