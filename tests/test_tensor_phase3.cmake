# test_tensor_phase3.cmake — std.tensor 阶段 3a：数值核心（plan_ndarray_stdlib.md §-1）
# elementwise（add/sub/mul/div）+ scalar（add_scalar/scale）+ reduction（sum/mean/
# max/argmax）+ matmul（2D）+ transpose + relu + 两层前向 demo + f64 泛型。
# JIT + AOT + memcheck 0/0/0。
cmake_minimum_required(VERSION 3.20)

# Resolve std/ from the source tree (the test imports std.tensor / std.vec / std.str).
get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(POS "${SAMPLE_DIR}/tensor_phase3_test.lls")
set(_expected "TENSOR_P3 PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase3 JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "tensor_phase3 JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "tensor_phase3 JIT has FAIL lines\n${jit_out}")
endif()
message(STATUS "tensor_phase3 JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/tensor_phase3_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase3 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase3 AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "tensor_phase3 AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "tensor_phase3 AOT has FAIL lines\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "tensor_phase3 AOT: OK")

# ---- memcheck (many intermediate owned tensors → drop-path stress) ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase3 memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "tensor_phase3 memcheck leak\n${mc_err}")
endif()
message(STATUS "tensor_phase3 memcheck: OK clean")

message(STATUS "test_tensor_phase3: ALL PASSED")
