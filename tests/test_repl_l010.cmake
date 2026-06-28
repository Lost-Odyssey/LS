# test_repl_l010.cmake — regression for L-010: cross-snippet has_drop values in
# `ls repl` must not crash.
#
# The bug was a flaky (~8%/run) heap use-after-free in jit_repl's body-strip:
# deleting a multi-block function's basic blocks entry-first left dangling
# cross-block SSA uses. A single run rarely caught it, so this driver pipes a
# clone-heavy std.json repro into `ls repl` N times and counts non-zero exits.
#
# The pre-fix rate (~8%) yields ~5 crashes in 60 runs; post-fix is 0 (verified
# 0/1000 soak + 0/320 under parallel load). We FAIL on >= 2 non-zero runs (a
# reliable signal for the 8% UAF) but TOLERATE a single non-zero run: under
# heavy -j load the rapid ls.exe re-spawns occasionally hit the documented
# ambient Defender/spawn flake (a launch failure, not a segfault), which must
# not be mistaken for an L-010 regression.
#
# Required -D vars: LS_EXE, SCRIPT

if(NOT LS_EXE OR NOT SCRIPT)
    message(FATAL_ERROR "usage: -DLS_EXE= -DSCRIPT=")
endif()

set(RUNS 60)
set(fails 0)
set(out "")
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
        "${fails}/${RUNS} runs of `ls repl` crashed — L-010 regression "
        "(cross-snippet has_drop UAF in jit_repl body-strip)")
endif()

# Correctness on a clean run: both stringify results must appear.
string(FIND "${last_good}" "[]" idx_arr)
string(FIND "${last_good}" "{}" idx_obj)
if(idx_arr EQUAL -1 OR idx_obj EQUAL -1)
    message(FATAL_ERROR "expected output to contain '[]' and '{}', got:\n${last_good}")
endif()

message(STATUS "test_repl_l010: ${RUNS} runs, ${fails} non-zero (tolerance <2) + output correct — PASS")
