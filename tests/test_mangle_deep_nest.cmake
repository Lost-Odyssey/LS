# test_mangle_deep_nest.cmake — Task 2.2 (docs/plan_arch_round2_backlog.md
# Batch 2): deep generic type-arg nesting (Vec(Map(Str,Vec(Pair(int,int)))))
# plus a cross-module type argument (Option(mod_w.Widget)) feeding the
# instance-name builders that moved from fixed 256/512-byte snprintf buffers
# to the growable MangleBuf (src/mangle.c). JIT + AOT + memcheck.
#
# @subsystem checker/generics
# @guards Batch 2 deep generic instance names truncated in fixed buffers
# @sources mangle.c:mangle_type_arg_name
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/mangle_deep_nest/main.lls")
set(_expected "total=10 wid=7" "MANGLE_DEEP_NEST PASS")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "mangle_deep_nest JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "mangle_deep_nest JIT missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "mangle_deep_nest JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/mangle_deep_nest_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "mangle_deep_nest AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "mangle_deep_nest AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "mangle_deep_nest AOT missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "mangle_deep_nest AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "mangle_deep_nest memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "mangle_deep_nest --memcheck FAILED\nstderr:\n${mc_err}")
endif()
message(STATUS "mangle_deep_nest memcheck: OK clean")

message(STATUS "test_mangle_deep_nest: ALL PASSED")
