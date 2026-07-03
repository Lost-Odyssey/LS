# test_match_own_stress.cmake — match × ownership stress guardrails
# (docs/plan_match_hardening.md Task 1): nested match, match-in-loop,
# valued/bare early returns from arms, int-switch & cond-chain has_drop
# results, block-tail owned locals (block-as-expression ownership
# transfer), borrow-match binder cloned into an owned result, and arms
# yielding Block (closure) values in all four shapes (block-tail local /
# outer local / enum payload / literal tail).
# JIT + AOT + memcheck per sample, plus one negative check: match on an
# aggregate (Str) subject must be a clear checker error, not invalid IR.
cmake_minimum_required(VERSION 3.20)

foreach(pair
    "match_own_stress_test.lls=MATCHSTRESS PASS"
    "match_borrow_mix_test.lls=BORROWMIX PASS"
    "match_block_yield_test.lls=BLOCKYIELD PASS"
    "enum_block_payload_test.lls=ENUMBLOCK PASS")
    string(REGEX REPLACE "=.*$" "" _file "${pair}")
    string(REGEX REPLACE "^[^=]*=" "" _expected "${pair}")
    set(SRC "${SAMPLE_DIR}/${_file}")
    string(REGEX REPLACE "_test\\.lls$" "" TN "${_file}")

    # ---- JIT ----
    execute_process(COMMAND "${LS_EXE}" run "${SRC}"
        OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
    if(NOT jit_rc EQUAL 0)
        message(FATAL_ERROR "${TN} JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
    endif()
    if(NOT "${jit_out}" MATCHES "${_expected}")
        message(FATAL_ERROR "${TN} JIT missing '${_expected}'\n${jit_out}")
    endif()
    if("${jit_out}" MATCHES "FAIL:")
        message(FATAL_ERROR "${TN} JIT had a failed check\n${jit_out}")
    endif()
    message(STATUS "${TN} JIT: OK")

    # ---- AOT ----
    set(aot_bin "${WORK_DIR}/${TN}_aot")
    if(WIN32)
        set(aot_bin "${aot_bin}.exe")
    endif()
    execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
        RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
    if(NOT aot_rc EQUAL 0)
        message(FATAL_ERROR "${TN} AOT compile FAILED:\n${aot_err}")
    endif()
    execute_process(COMMAND "${aot_bin}"
        OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
    if(NOT aot_run_rc EQUAL 0)
        message(FATAL_ERROR "${TN} AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
    endif()
    if(NOT "${aot_out}" MATCHES "${_expected}")
        message(FATAL_ERROR "${TN} AOT missing '${_expected}'\n${aot_out}")
    endif()
    if("${aot_out}" MATCHES "FAIL:")
        message(FATAL_ERROR "${TN} AOT had a failed check\n${aot_out}")
    endif()
    message(STATUS "${TN} AOT: OK")

    # ---- memcheck (0 leak / 0 double-free / 0 invalid free) ----
    execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
        OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
    if(NOT mc_rc EQUAL 0)
        message(FATAL_ERROR "${TN} memcheck FAILED (rc=${mc_rc})\n${mc_err}")
    endif()
    if(NOT "${mc_err}" MATCHES "OK clean")
        message(FATAL_ERROR "${TN} memcheck leak/double-free\n${mc_err}")
    endif()
    message(STATUS "${TN} memcheck: OK clean")
endforeach()

# ---- negative: aggregate (Str) match subject is a checker error ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/match_str_subject_reject.lls"
    OUTPUT_VARIABLE rej_out ERROR_VARIABLE rej_err RESULT_VARIABLE rej_rc)
if(rej_rc EQUAL 0)
    message(FATAL_ERROR "match_str_subject_reject: expected compile error, got exit 0\n${rej_out}")
endif()
if(NOT "${rej_err}" MATCHES "is not matchable")
    message(FATAL_ERROR "match_str_subject_reject: stderr missing 'is not matchable'\n${rej_err}")
endif()
message(STATUS "match_str_subject_reject: rejected as expected")
