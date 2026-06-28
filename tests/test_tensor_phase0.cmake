# test_tensor_phase0.cmake — std.tensor 堆 Tensor 阶段 0 地基探针（plan_ndarray_stdlib.md §-1）
# 硬编码 IMat（堆 *int buffer 的 2D int 矩阵）验证 NumPy 式堆存储地基：
# 偏移 at/set、as_ptr -> *int 基址、row_ptr 子数组指针、move 语义 +
# 显式 .copy() 独立、as_ptr 传真 C 函数（CRT memcpy）。JIT + AOT + memcheck 0/0/0。
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/tensor_phase0_test.ls")
set(_expected "TENSOR_P0 PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase0 JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "tensor_phase0 JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "tensor_phase0 JIT has FAIL lines\n${jit_out}")
endif()
message(STATUS "tensor_phase0 JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/tensor_phase0_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase0 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase0 AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "tensor_phase0 AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "tensor_phase0 AOT has FAIL lines\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "tensor_phase0 AOT: OK")

# ---- memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase0 memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "tensor_phase0 memcheck leak\n${mc_err}")
endif()
message(STATUS "tensor_phase0 memcheck: OK clean")

message(STATUS "test_tensor_phase0: ALL PASSED")
