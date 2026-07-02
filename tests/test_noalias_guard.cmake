# test_noalias_guard.cmake — A4 noalias-recovery safety gold standard.
#
# noalias_guard.lls packs the three cross-thread &!self sharing patterns
# (SpinGuard CAS spin, Guard mutex, Chan blocking send/recv) that DEADLOCK if
# `noalias` is ever emitted on their borrow params (LLVM hoists the spin/recv
# load past the other thread's write). The A4 whitelist must keep every
# function on these paths disqualified forever.
#
# Positive control (manual, NOT run here — a 60s expected-hang is too costly
# for CI): LS_FORCE_NOALIAS=1 on this sample hangs AOT within seconds
# (verified 2026-07-02). This script asserts the DEFAULT (whitelist) build
# stays correct: JIT + 6x AOT. NO --memcheck (tracker not thread-safe).
#
# Required: LS_EXE, SAMPLE, WORK_DIR, STDLIB (repo root → LS_HOME).
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE)
    message(FATAL_ERROR "test_noalias_guard.cmake requires LS_EXE and SAMPLE")
endif()
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(TN "noalias_guard")

# Never inherit a stray diagnostic switch from the environment.
unset(ENV{LS_FORCE_NOALIAS})
unset(ENV{LS_NO_NOALIAS})
unset(ENV{LS_NO_BORROW_ATTRS})

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 60)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "${TN} JIT failed/hung (rc=${jr})\n${je}\n${jo}")
endif()
if(jo MATCHES "FAIL" OR NOT jo MATCHES "NOALIAS-GUARD OK")
    message(FATAL_ERROR "${TN} JIT correctness:\n${jo}")
endif()

# ---- AOT compile + repeated runs (hangs here = whitelist regression) ----
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
    if(ao MATCHES "FAIL" OR NOT ao MATCHES "NOALIAS-GUARD OK")
        message(FATAL_ERROR "${TN} AOT run ${i} correctness:\n${ao}")
    endif()
endforeach()
file(REMOVE "${BIN}")

message(STATUS "${TN}: JIT + 6x AOT cross-thread patterns PASS")
