# test_fn_array_param.cmake — uniform by-value array(T,N) param rejection on
# the four paths left open after the interface-side fix (policy A, 2026-07-19):
# free fn (check pass bypassed check_fn_decl -> dead code), generic free fn,
# struct-level generic method, and method-level generic instantiation.
#  * Negative: all four shapes in one run -> FOUR "array must be passed by
#    pointer" diagnostics.
#  * Positive: &array(int) / &!array(int) slice params on the free-fn and
#    generic-method paths stay legal (JIT + AOT + memcheck 0/0/0).
cmake_minimum_required(VERSION 3.20)

# ---- negative: all four sites must reject ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/fn_array_param_reject.lls"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR "fn_array_param_reject: expected compile error, got success\n${n_out}")
endif()
set(n_all "${n_out}${n_err}")
string(REGEX MATCHALL "array must be passed by pointer" n_hits "${n_all}")
list(LENGTH n_hits n_hit_count)
if(NOT n_hit_count EQUAL 4)
    message(FATAL_ERROR "fn_array_param_reject: expected 4 'array must be passed by pointer' diagnostics (free fn + generic free fn + struct-generic method + method-level generic), got ${n_hit_count}\n${n_all}")
endif()
message(STATUS "fn_array_param_reject: rejected at all four sites (rc=${n_rc})")

# ---- positive: JIT ----
set(POS "${SAMPLE_DIR}/fn_array_param_ok.lls")
set(_expected "a=60 c=62 v0=11")
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "fn_array_param_ok JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "fn_array_param_ok JIT missing '${_expected}'\n${jit_out}")
endif()
message(STATUS "fn_array_param_ok JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/fn_array_param_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "fn_array_param_ok AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "fn_array_param_ok AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "fn_array_param_ok AOT missing '${_expected}'\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "fn_array_param_ok AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "fn_array_param_ok memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "fn_array_param_ok memcheck not clean\n${mc_err}")
endif()
message(STATUS "fn_array_param_ok memcheck: OK clean")

message(STATUS "test_fn_array_param: ALL PASSED")
