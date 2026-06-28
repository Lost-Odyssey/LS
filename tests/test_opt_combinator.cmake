# test_opt_combinator.cmake — C1 Option/Result combinators (compiler-lowered).
#  * Positive: unwrap/expect/unwrap_or/is_* over Option+Result, owned payloads,
#    map.get(k).unwrap_or(...) — JIT + AOT + memcheck (0 leak / 0 double-free).
#  * Negative (panic):  `.unwrap()` on None aborts (non-zero exit, "[unwrap]"
#    diagnostic, post-access line not run).
#  * Negative (reject): `.unwrap()` consumes a has_drop receiver — reusing it is
#    a compile-time move error (non-zero exit, "moved" diagnostic).
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/opt_combinator_test.ls")
set(_expected "OPTCOMB PASS")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "opt_combinator positive JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "opt_combinator positive JIT missing '${_expected}'\n${jit_out}")
endif()
message(STATUS "opt_combinator positive JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/opt_combinator_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "opt_combinator positive AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "opt_combinator positive AOT run FAILED (rc=${aot_run_rc})")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "opt_combinator positive AOT missing '${_expected}'\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "opt_combinator positive AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "opt_combinator memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "opt_combinator memcheck leak\n${mc_err}")
endif()
message(STATUS "opt_combinator positive memcheck: OK clean")

# ---- negative: unwrap() on None must abort ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/opt_combinator_panic.ls"
    OUTPUT_VARIABLE p_out ERROR_VARIABLE p_err RESULT_VARIABLE p_rc)
if(p_rc EQUAL 0)
    message(FATAL_ERROR "opt_combinator_panic: expected non-zero exit (abort)\n${p_out}")
endif()
if(NOT "${p_out}" MATCHES "\\[unwrap\\]")
    message(FATAL_ERROR "opt_combinator_panic: missing '[unwrap]' diagnostic\n${p_out}")
endif()
if("${p_out}" MATCHES "AFTER")
    message(FATAL_ERROR "opt_combinator_panic: ran past the failed unwrap\n${p_out}")
endif()
message(STATUS "opt_combinator_panic: aborted as expected (rc=${p_rc})")

# ---- negative: use-after-move must be rejected at compile time ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/opt_combinator_reject.ls"
    OUTPUT_VARIABLE r_out ERROR_VARIABLE r_err RESULT_VARIABLE r_rc)
if(r_rc EQUAL 0)
    message(FATAL_ERROR "opt_combinator_reject: expected non-zero exit (move error)\n${r_out}")
endif()
if(NOT "${r_out}${r_err}" MATCHES "moved")
    message(FATAL_ERROR "opt_combinator_reject: missing 'moved' diagnostic\n${r_out}\n${r_err}")
endif()
message(STATUS "opt_combinator_reject: rejected as expected (rc=${r_rc})")

# ---- C2b negative: map/and_then/map_err need an explicit result type param ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/c2b_no_typearg.ls"
    OUTPUT_VARIABLE t_out ERROR_VARIABLE t_err RESULT_VARIABLE t_rc)
if(t_rc EQUAL 0)
    message(FATAL_ERROR "c2b_no_typearg: expected non-zero exit (missing type arg)\n${t_out}")
endif()
if(NOT "${t_out}${t_err}" MATCHES "type argument")
    message(FATAL_ERROR "c2b_no_typearg: missing 'type argument' diagnostic\n${t_out}\n${t_err}")
endif()
message(STATUS "c2b_no_typearg: rejected as expected (rc=${t_rc})")

# ---- C2b negative: map_err is Result-only; on an Option it is rejected ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/c2b_map_err_option.ls"
    OUTPUT_VARIABLE me_out ERROR_VARIABLE me_err RESULT_VARIABLE me_rc)
if(me_rc EQUAL 0)
    message(FATAL_ERROR "c2b_map_err_option: expected non-zero exit (Result-only)\n${me_out}")
endif()
if(NOT "${me_out}${me_err}" MATCHES "Result combinator")
    message(FATAL_ERROR "c2b_map_err_option: missing 'Result combinator' diagnostic\n${me_out}\n${me_err}")
endif()
message(STATUS "c2b_map_err_option: rejected as expected (rc=${me_rc})")

message(STATUS "test_opt_combinator: ALL PASSED")
