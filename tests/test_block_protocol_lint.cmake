# test_block_protocol_lint.cmake — stage 5 (plan_footgun_remediation):
# Block container-ownership protocol method-name lint (audit B-2).
#   block_protocol.h is the single name authority; the checker warns (never
#   errors) when USER code defines a reserved name (get/push/...) with a
#   Block in the signature. This test pins: 3 warnings fire (non-generic
#   reader + non-generic sink + generic template via type alias), reserved
#   names WITHOUT Block and Block under free names stay silent, and the
#   program still type-checks (rc=0) and runs with pinned output.
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/block_protocol_lint_test.lls")

# ---- check: exactly 3 protocol warnings, rc = 0 (warning, not error) ----
execute_process(COMMAND "${LS_EXE}" check "${SRC}"
    OUTPUT_VARIABLE chk_out ERROR_VARIABLE chk_err RESULT_VARIABLE chk_rc)
set(all_out "${chk_out}${chk_err}")
if(NOT chk_rc EQUAL 0)
    message(FATAL_ERROR "block_protocol_lint: check rc=${chk_rc} (warnings must not fail the check)\n${all_out}")
endif()
string(REGEX MATCHALL "container-ownership protocol reserved name" hits "${all_out}")
list(LENGTH hits n_hits)
if(NOT n_hits EQUAL 3)
    message(FATAL_ERROR "block_protocol_lint: expected exactly 3 warnings, got ${n_hits}\n${all_out}")
endif()
if(NOT "${all_out}" MATCHES "method name 'get'")
    message(FATAL_ERROR "block_protocol_lint: missing reader ('get') warning\n${all_out}")
endif()
if(NOT "${all_out}" MATCHES "method name 'push'")
    message(FATAL_ERROR "block_protocol_lint: missing sink ('push') warning\n${all_out}")
endif()
# Reserved name without Block ('set') and Block under a free name
# ('register_cb') must stay silent.
if("${all_out}" MATCHES "method name 'set'" OR "${all_out}" MATCHES "register_cb")
    message(FATAL_ERROR "block_protocol_lint: false positive\n${all_out}")
endif()
message(STATUS "block_protocol_lint check: OK (3 warnings, rc=0)")

# ---- JIT run: program is unaffected by the lint ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "block_protocol_lint JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "LINT PASS 10 5 20 1")
    message(FATAL_ERROR "block_protocol_lint JIT wrong output\n${jit_out}")
endif()
message(STATUS "block_protocol_lint JIT: OK")

# ---- std exemption: a std module compiles with zero protocol warnings ----
# (std implements the protocol; path under <LS_HOME>/lib/ is exempt. Checking
# a user file that imports std.core.vec exercises the exempt path.)
set(imp_src "${WORK_DIR}/bpl_std_import.lls")
file(WRITE "${imp_src}" "import std.core.vec\n\ndef main() -> int {\n    Vec(int) v = [1, 2]\n    v.push(3)\n    @print(f\"{v.len()}\")\n    return 0\n}\n")
execute_process(COMMAND "${LS_EXE}" check "${imp_src}"
    OUTPUT_VARIABLE std_out ERROR_VARIABLE std_err RESULT_VARIABLE std_rc)
if(NOT std_rc EQUAL 0)
    message(FATAL_ERROR "block_protocol_lint std-import check FAILED\n${std_out}${std_err}")
endif()
if("${std_out}${std_err}" MATCHES "container-ownership protocol")
    message(FATAL_ERROR "block_protocol_lint: std module false positive\n${std_out}${std_err}")
endif()
message(STATUS "block_protocol_lint std exemption: OK")
