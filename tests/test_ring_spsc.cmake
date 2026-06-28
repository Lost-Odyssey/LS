# test_ring_spsc.cmake — std.ring SPSC cross-thread (lock-free, Phase 1).
#
# A producer thread enqueues while the main thread consumes; the Atomic(i64)
# cursors make it lock-free. POD path = exact count+sum; Str path = owned
# has_drop elements moved across the thread boundary (checksum). NO --memcheck
# (tracker not thread-safe — same as task/atomic/sync); soundness via JIT +
# repeated AOT runs (a torn handle / double-free would crash or skew the count).
#
# Required: LS_EXE, SAMPLE, WORK_DIR, STDLIB (repo root → LS_HOME).
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE)
    message(FATAL_ERROR "test_ring_spsc.cmake requires LS_EXE and SAMPLE")
endif()
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(TN "ring_spsc")

function(_check_out out where)
    if(out MATCHES "FAIL")
        message(FATAL_ERROR "${TN} ${where} reported FAIL:\n${out}")
    endif()
    if(NOT out MATCHES "SPSC OK pod" OR NOT out MATCHES "SPSC OK str")
        message(FATAL_ERROR "${TN} ${where} missing OK markers:\n${out}")
    endif()
endfunction()

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 60)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "${TN} JIT run failed (rc=${jr})\nstderr:\n${je}\n${jo}")
endif()
_check_out("${jo}" "JIT")

# ---- AOT compile + repeated runs (concurrency soundness) ----
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
        message(FATAL_ERROR "${TN} AOT run ${i} failed (rc=${ar})\nstdout:\n${ao}")
    endif()
    _check_out("${ao}" "AOT run ${i}")
endforeach()
file(REMOVE "${BIN}")

message(STATUS "${TN}: JIT + 6x AOT cross-thread SPSC PASS")
