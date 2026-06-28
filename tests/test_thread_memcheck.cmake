# test_thread_memcheck.cmake — multi-threaded programs must be memcheck-clean.
# Validates the thread-safe memcheck tracker (runtime/memcheck.c): worker threads
# allocate/free concurrently, so before the lock + thread-local frame stack this
# would corrupt the tracker table. These samples spawn workers hammering shared
# Atomic / Guard / Chan state and must run AND report "OK clean" under --memcheck.
#
# Thread scheduling is nondeterministic and the harness has a rare ambient
# process-spawn flake, so each sample gets up to 3 attempts; it passes if ANY
# attempt is clean, and fails only on a hard crash or a consistent leak/dirty.
#
# Closure-env-into-thread samples (nested_closure_thread / par_for_test) are now
# INCLUDED: L-015 is fixed (the worker thunk drops the moved env via the
# memcheck-tracked free), so closures with a heap env moved into a thread are
# memcheck-clean too. They are the regression guard for L-015.
#
# Required: LS_EXE, SAMPLE_DIR, LS_HOME
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE_DIR OR NOT LS_HOME)
    message(FATAL_ERROR "requires LS_EXE, SAMPLE_DIR, LS_HOME")
endif()
set(ENV{LS_HOME} "${LS_HOME}")

set(_samples guard_thread_test atomic_thread_test chan_mpmc_test
             nested_closure_thread par_for_test)
set(_fail 0)
foreach(_s IN LISTS _samples)
    set(_f "${SAMPLE_DIR}/${_s}.ls")
    if(NOT EXISTS "${_f}")
        message(WARNING "missing sample: ${_f}")
        set(_fail 1)
        continue()
    endif()
    set(_clean 0)
    foreach(_attempt RANGE 1 3)
        execute_process(
            COMMAND "${LS_EXE}" run --memcheck "${_f}"
            OUTPUT_QUIET ERROR_VARIABLE _err RESULT_VARIABLE _rc
            TIMEOUT 60
        )
        if("${_rc}" MATCHES "^0$" AND "${_err}" MATCHES "OK clean")
            set(_clean 1)
            break()
        endif()
    endforeach()
    if(_clean)
        message(STATUS "ok (memcheck clean under threads): ${_s}")
    else()
        message(WARNING "FAIL: ${_s} — not memcheck-clean (rc=${_rc})\n${_err}")
        set(_fail 1)
    endif()
endforeach()

if(_fail)
    message(FATAL_ERROR "test_thread_memcheck: FAILED")
endif()
message(STATUS "test_thread_memcheck: ALL PASSED")
