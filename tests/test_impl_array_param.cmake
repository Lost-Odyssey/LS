# test_impl_array_param.cmake — interface-side array(T,N) by-value param rejection.
# The inherent-methods twin (check_impl_decl) has always rejected by-value
# array params ("array must be passed by pointer"); the interface declaration
# and `methods T: Iface` param loops lacked the check, so a by-value array of
# has_drop elements bit-copied into the callee frame and double-freed.
#  * Negative: interface sig + trait-impl method with array(int,3) param ->
#    TWO "array must be passed by pointer" diagnostics in one run.
#  * Positive: &array(int) / &!array(int) slice params in interface + impl
#    stay legal (JIT + AOT + memcheck 0/0/0).
#
# @subsystem language/syntax
# @guards interface-side by-value array param rejection
# @sources checker_decl.c:reject_array_by_value_param
cmake_minimum_required(VERSION 3.20)

# ---- negative: both decl sites must reject ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/impl_array_param_reject.lls"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR "impl_array_param_reject: expected compile error, got success\n${n_out}")
endif()
set(n_all "${n_out}${n_err}")
string(REGEX MATCHALL "array must be passed by pointer" n_hits "${n_all}")
list(LENGTH n_hits n_hit_count)
if(NOT n_hit_count EQUAL 2)
    message(FATAL_ERROR "impl_array_param_reject: expected 2 'array must be passed by pointer' diagnostics (interface sig + impl method), got ${n_hit_count}\n${n_all}")
endif()
message(STATUS "impl_array_param_reject: rejected at both sites (rc=${n_rc})")

# ---- positive: JIT ----
set(POS "${SAMPLE_DIR}/impl_array_param_ok.lls")
set(_expected "sum=61 v0=11")
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "impl_array_param_ok JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "impl_array_param_ok JIT missing '${_expected}'\n${jit_out}")
endif()
message(STATUS "impl_array_param_ok JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/impl_array_param_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "impl_array_param_ok AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "impl_array_param_ok AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "impl_array_param_ok AOT missing '${_expected}'\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "impl_array_param_ok AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "impl_array_param_ok memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "impl_array_param_ok memcheck not clean\n${mc_err}")
endif()
message(STATUS "impl_array_param_ok memcheck: OK clean")

message(STATUS "test_impl_array_param: ALL PASSED")
