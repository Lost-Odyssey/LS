# test_repl_soak.cmake — generic crash-soak driver for `ls repl`.
#
# Pipes SCRIPT into `ls repl` RUNS times and counts non-zero exits. Used for
# flaky-crash regressions (the L-010 family): a single run rarely catches an
# ~10% heap UAF, so we run many and FAIL on >= 2 crashes while TOLERATING a
# single non-zero run (the documented ambient Defender/spawn flake under heavy
# -j load — a launch failure, not a segfault).
#
# Required -D vars: LS_EXE, SCRIPT, EXPECT (a substring a clean run must print).
# Optional: RUNS (default 60).

if(NOT LS_EXE OR NOT SCRIPT OR NOT DEFINED EXPECT)
    message(FATAL_ERROR "usage: -DLS_EXE= -DSCRIPT= -DEXPECT= [-DRUNS=]")
endif()
if(NOT DEFINED RUNS)
    set(RUNS 60)
endif()

set(fails 0)
set(last_good "")
foreach(i RANGE 1 ${RUNS})
    execute_process(
        COMMAND "${LS_EXE}" repl
        INPUT_FILE "${SCRIPT}"
        OUTPUT_VARIABLE out
        ERROR_VARIABLE  err
        RESULT_VARIABLE rc
    )
    if(NOT rc EQUAL 0)
        math(EXPR fails "${fails} + 1")
        message(STATUS "run ${i}/${RUNS} exited ${rc} (non-zero #${fails})\nstderr:\n${err}")
    else()
        set(last_good "${out}")
    endif()
endforeach()

if(fails GREATER_EQUAL 2)
    message(FATAL_ERROR
        "${fails}/${RUNS} runs of `ls repl` crashed — REPL crash regression")
endif()

string(FIND "${last_good}" "${EXPECT}" idx)
if(idx EQUAL -1)
    message(FATAL_ERROR "expected a clean run to contain '${EXPECT}', got:\n${last_good}")
endif()

message(STATUS "test_repl_soak: ${RUNS} runs, ${fails} non-zero (tolerance <2) + output correct — PASS")
