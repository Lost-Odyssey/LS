# test_chan_mpmc.cmake — std.chan MPMC blocking (N producers + M consumers).
#
# Drives __cond_wait on both sides (cap << total → real blocking). The mutex
# makes it MPMC-safe; a lost wakeup would HANG (TIMEOUT catches it), a torn move
# would crash or skew the totals. NO --memcheck (tracker not thread-safe).
# Soundness via JIT + repeated AOT runs with a timeout guard.
#
# Required: LS_EXE, SAMPLE, WORK_DIR, STDLIB (repo root → LS_HOME).
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE)
    message(FATAL_ERROR "test_chan_mpmc.cmake requires LS_EXE and SAMPLE")
endif()
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(TN "chan_mpmc")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 60)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "${TN} JIT failed/hung (rc=${jr})\n${je}\n${jo}")
endif()
if(jo MATCHES "FAIL" OR NOT jo MATCHES "CHAN OK mpmc")
    message(FATAL_ERROR "${TN} JIT correctness:\n${jo}")
endif()

# ---- AOT compile + repeated runs ----
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
    execute_process(COMMAND "${BIN}"
        OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 60)
    if(NOT ar EQUAL 0)
        message(FATAL_ERROR "${TN} AOT run ${i} failed/hung (rc=${ar})\n${ao}")
    endif()
    if(ao MATCHES "FAIL" OR NOT ao MATCHES "CHAN OK mpmc")
        message(FATAL_ERROR "${TN} AOT run ${i} correctness:\n${ao}")
    endif()
endforeach()
file(REMOVE "${BIN}")

message(STATUS "${TN}: JIT + 6x AOT MPMC blocking PASS")
