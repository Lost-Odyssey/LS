# test_ring_mpmc.cmake — std.ring MPMC lock-free (CAS reservation, Phase 3).
#
# Many producers + many consumers over new_mpmc_ring (no mutex). POD path proves
# exact conservation (count+sum); the Str path is sharper — a double-reserved
# slot would double-free a string (crash). NO --memcheck (tracker not thread-
# safe). Soundness via JIT + repeated AOT runs with a timeout guard (a lost slot
# hangs the count below total).
#
# Required: LS_EXE, SAMPLE, WORK_DIR, STDLIB (repo root → LS_HOME).
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE)
    message(FATAL_ERROR "test_ring_mpmc.cmake requires LS_EXE and SAMPLE")
endif()
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(TN "ring_mpmc")

function(_check_out out where)
    if(out MATCHES "FAIL")
        message(FATAL_ERROR "${TN} ${where} reported FAIL:\n${out}")
    endif()
    if(NOT out MATCHES "MPMC OK pod" OR NOT out MATCHES "MPMC OK str")
        message(FATAL_ERROR "${TN} ${where} missing OK markers:\n${out}")
    endif()
endfunction()

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 90)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "${TN} JIT failed/hung (rc=${jr})\n${je}\n${jo}")
endif()
_check_out("${jo}" "JIT")

# ---- AOT compile + repeated runs ----
set(BIN "${WORK_DIR}/${TN}_aot")
if(WIN32)
    set(BIN "${BIN}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${BIN}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 90)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "${TN} AOT compile failed:\n${ce}")
endif()
foreach(i RANGE 1 6)
    execute_process(COMMAND "${BIN}"
        OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 60)
    if(NOT ar EQUAL 0)
        message(FATAL_ERROR "${TN} AOT run ${i} failed/hung (rc=${ar})\n${ao}")
    endif()
    _check_out("${ao}" "AOT run ${i}")
endforeach()
file(REMOVE "${BIN}")

message(STATUS "${TN}: JIT + 6x AOT lock-free MPMC PASS")
