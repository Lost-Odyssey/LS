# test_match_scalar_exhaust_warn.cmake — stage 12b (plan_footgun_remediation,
# audit M-7): a VALUE-producing scalar match with no `_` arm warns (never
# errors) — an unmatched subject silently yields a zeroed result. Pins:
# exactly 2 warnings fire (int + char subjects), the four silent shapes
# (wildcard / bool fully covered / void statement match / enum subject) stay
# silent, and the program still type-checks (rc=0) and runs pinned.
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/match_scalar_exhaust_warn_test.lls")

# ---- check: exactly 2 warnings, rc = 0 (warning, not error) ----
execute_process(COMMAND "${LS_EXE}" check "${SRC}"
    OUTPUT_VARIABLE chk_out ERROR_VARIABLE chk_err RESULT_VARIABLE chk_rc)
set(all_out "${chk_out}${chk_err}")
if(NOT chk_rc EQUAL 0)
    message(FATAL_ERROR "match_scalar_exhaust_warn: check rc=${chk_rc} (warnings must not fail the check)\n${all_out}")
endif()
string(REGEX MATCHALL "value-producing match on" hits "${all_out}")
list(LENGTH hits n_hits)
if(NOT n_hits EQUAL 2)
    message(FATAL_ERROR "match_scalar_exhaust_warn: expected exactly 2 warnings, got ${n_hits}\n${all_out}")
endif()
if(NOT "${all_out}" MATCHES "value-producing match on 'int'")
    message(FATAL_ERROR "match_scalar_exhaust_warn: missing int-subject warning\n${all_out}")
endif()
if(NOT "${all_out}" MATCHES "value-producing match on 'char'")
    message(FATAL_ERROR "match_scalar_exhaust_warn: missing char-subject warning\n${all_out}")
endif()
# Silent shapes must not appear: bool subject fully covered.
if("${all_out}" MATCHES "value-producing match on 'bool'")
    message(FATAL_ERROR "match_scalar_exhaust_warn: false positive on covered bool\n${all_out}")
endif()
message(STATUS "match_scalar_exhaust_warn check: OK (2 warnings, rc=0)")

# ---- JIT run: program is unaffected by the warning ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "match_scalar_exhaust_warn JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "EXH PASS one ex 10 1 7")
    message(FATAL_ERROR "match_scalar_exhaust_warn JIT wrong output\n${jit_out}")
endif()
message(STATUS "match_scalar_exhaust_warn JIT: OK")
