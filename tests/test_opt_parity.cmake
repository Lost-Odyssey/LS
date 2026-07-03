# test_opt_parity.cmake — kill-switch A/B parity for optimization passes
# that previously had escape hatches but no ctest coverage
# (docs/plan_match_hardening.md Task 3): LS_NO_ELIDE (A1 clone-elision),
# LS_NO_INTERNALIZE (A5 internalize+GlobalDCE), LS_NO_ENUM_RANGE (A3 tag
# !range). Disabling any of them must not change program output — a
# silently-broken pass (or off-path) fails loudly here.
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/match_own_stress_test.lls")
set(_expected "MATCHSTRESS PASS")

# ---- baseline: all passes on ----
set(base_bin "${WORK_DIR}/opt_parity_base")
if(WIN32)
    set(base_bin "${base_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${base_bin}"
    RESULT_VARIABLE c_rc ERROR_VARIABLE c_err)
if(NOT c_rc EQUAL 0)
    message(FATAL_ERROR "opt_parity baseline compile FAILED:\n${c_err}")
endif()
execute_process(COMMAND "${base_bin}"
    OUTPUT_VARIABLE base_out RESULT_VARIABLE r_rc)
if(NOT r_rc EQUAL 0 OR NOT "${base_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "opt_parity baseline run bad (rc=${r_rc})\n${base_out}")
endif()

# ---- each kill switch: output must equal the baseline ----
foreach(sw LS_NO_ELIDE LS_NO_INTERNALIZE LS_NO_ENUM_RANGE)
    set(ENV{${sw}} "1")
    set(off_bin "${WORK_DIR}/opt_parity_${sw}")
    if(WIN32)
        set(off_bin "${off_bin}.exe")
    endif()
    execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${off_bin}"
        RESULT_VARIABLE oc_rc ERROR_VARIABLE oc_err)
    if(NOT oc_rc EQUAL 0)
        message(FATAL_ERROR "opt_parity ${sw}=1 compile FAILED:\n${oc_err}")
    endif()
    execute_process(COMMAND "${off_bin}"
        OUTPUT_VARIABLE off_out RESULT_VARIABLE or_rc)
    unset(ENV{${sw}})
    if(NOT or_rc EQUAL 0)
        message(FATAL_ERROR "opt_parity ${sw}=1 run FAILED (rc=${or_rc})\n${off_out}")
    endif()
    if(NOT "${off_out}" STREQUAL "${base_out}")
        message(FATAL_ERROR
            "opt_parity ${sw}=1 output differs from baseline\n--- baseline\n${base_out}\n--- ${sw}=1\n${off_out}")
    endif()
    file(REMOVE "${off_bin}")
    if(WIN32)
        file(REMOVE "${off_bin}.obj")
    endif()
    message(STATUS "opt_parity ${sw}: AOT output parity OK")
endforeach()

# ---- LS_NO_ELIDE also affects the JIT (checker-level pass) ----
set(ENV{LS_NO_ELIDE} "1")
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_off RESULT_VARIABLE j_rc)
unset(ENV{LS_NO_ELIDE})
if(NOT j_rc EQUAL 0 OR NOT "${jit_off}" MATCHES "${_expected}")
    message(FATAL_ERROR "opt_parity LS_NO_ELIDE=1 JIT bad (rc=${j_rc})\n${jit_off}")
endif()
message(STATUS "opt_parity LS_NO_ELIDE JIT: OK")

file(REMOVE "${base_bin}")
if(WIN32)
    file(REMOVE "${base_bin}.obj")
endif()
message(STATUS "opt_parity: all kill-switch parities PASS")
