# test_tensor_phase3b.cmake — std.tensor 阶段 3b：broadcasting + 按轴 reduction + 激活
# （plan_ndarray_stdlib.md §-1）。NumPy 式 broadcasting（add/sub/mul）+ sum_axis +
# float 激活 exp/sigmoid/softmax_rows + 带 bias 的前向 relu(X@W+b)。
# JIT + AOT + memcheck 0/0/0。
cmake_minimum_required(VERSION 3.20)

# Resolve std/ from the source tree (imports std.tensor / std.vec / std.str / math).
get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(POS "${SAMPLE_DIR}/tensor_phase3b_test.ls")
set(_expected "TENSOR_P3B PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase3b JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "tensor_phase3b JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "tensor_phase3b JIT has FAIL lines\n${jit_out}")
endif()
message(STATUS "tensor_phase3b JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/tensor_phase3b_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase3b AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase3b AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "tensor_phase3b AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "tensor_phase3b AOT has FAIL lines\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "tensor_phase3b AOT: OK")

# ---- memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "tensor_phase3b memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "tensor_phase3b memcheck leak\n${mc_err}")
endif()
message(STATUS "tensor_phase3b memcheck: OK clean")

message(STATUS "test_tensor_phase3b: ALL PASSED")
