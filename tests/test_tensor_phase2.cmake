# test_tensor_phase2.cmake — std.tensor 阶段 2：多下标 t[i,j,k]（arity 派发协议）
# （plan_ndarray_stdlib.md §-1/§4）。t[i,j]/t[i,j,k]/t[i,j,k,l] 经编译器按下标个数
# 派发到 __index{N}/__index_set{N} 协议方法。正向 2D/3D/4D 读写 + 表达式 + f64 +
# 单下标 Vec 不受影响；负向 Vec 多下标编译期拒绝。JIT + AOT + memcheck 0/0/0。
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(POS "${SAMPLE_DIR}/tensor_phase2_test.lls")
set(NEG "${SAMPLE_DIR}/tensor_multi_index_reject.lls")
set(_expected "TENSOR_P2 PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase2 JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "tensor_phase2 JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "tensor_phase2 JIT has FAIL lines\n${jit_out}")
endif()
message(STATUS "tensor_phase2 JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/tensor_phase2_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase2 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase2 AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "tensor_phase2 AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "tensor_phase2 AOT has FAIL lines\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "tensor_phase2 AOT: OK")

# ---- memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase2 memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "tensor_phase2 memcheck leak\n${mc_err}")
endif()
message(STATUS "tensor_phase2 memcheck: OK clean")

# ---- 负向：Vec 多下标编译期拒绝 ----
execute_process(COMMAND "${LS_EXE}" run "${NEG}"
    OUTPUT_VARIABLE neg_out ERROR_VARIABLE neg_err RESULT_VARIABLE neg_rc)
if(neg_rc EQUAL 0)
    message(FATAL_ERROR "tensor_multi_index_reject should FAIL but succeeded\n${neg_out}")
endif()
if(NOT "${neg_err}${neg_out}" MATCHES "does not support 2-D indexing")
    message(FATAL_ERROR "tensor_multi_index_reject wrong error\n${neg_err}\n${neg_out}")
endif()
message(STATUS "tensor_multi_index_reject: correctly rejected")

message(STATUS "test_tensor_phase2: ALL PASSED")
