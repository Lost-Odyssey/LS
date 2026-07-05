# test_match_scalar_exhaust.cmake — M-7 (stage 12b warning, upgraded to an
# ERROR by L-020, 2026-07-05): a value-producing scalar match with no `_` arm
# is rejected — an unmatched subject would silently yield a zeroed result.
# Pins: 2 reject shapes (int + char subjects, both diagnosed in one pass),
# the four exempt shapes compile silently, and the exempt program runs pinned.
cmake_minimum_required(VERSION 3.20)

# ---- reject: both shapes diagnosed, non-zero exit ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/match_scalar_exhaust_reject.lls"
    OUTPUT_VARIABLE r_out ERROR_VARIABLE r_err RESULT_VARIABLE r_rc)
if(r_rc EQUAL 0)
    message(FATAL_ERROR "match_scalar_exhaust reject: expected compile error, got exit 0\n${r_out}")
endif()
if(NOT "${r_err}" MATCHES "value-producing match on 'int'")
    message(FATAL_ERROR "match_scalar_exhaust reject: missing int-subject error\n${r_err}")
endif()
if(NOT "${r_err}" MATCHES "value-producing match on 'char'")
    message(FATAL_ERROR "match_scalar_exhaust reject: missing char-subject error\n${r_err}")
endif()
message(STATUS "match_scalar_exhaust reject: OK (2 errors)")

# ---- exempt shapes: compile clean (no diagnostic at all) ----
set(SRC "${SAMPLE_DIR}/match_scalar_exhaust_ok_test.lls")
execute_process(COMMAND "${LS_EXE}" check "${SRC}"
    OUTPUT_VARIABLE chk_out ERROR_VARIABLE chk_err RESULT_VARIABLE chk_rc)
if(NOT chk_rc EQUAL 0)
    message(FATAL_ERROR "match_scalar_exhaust ok: check rc=${chk_rc}\n${chk_out}${chk_err}")
endif()
if("${chk_out}${chk_err}" MATCHES "value-producing match")
    message(FATAL_ERROR "match_scalar_exhaust ok: false positive on exempt shape\n${chk_out}${chk_err}")
endif()
message(STATUS "match_scalar_exhaust exempt shapes: OK")

# ---- JIT run of the exempt program ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "match_scalar_exhaust JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "EXH PASS one 10 1 7")
    message(FATAL_ERROR "match_scalar_exhaust JIT wrong output\n${jit_out}")
endif()
message(STATUS "match_scalar_exhaust JIT: OK")
