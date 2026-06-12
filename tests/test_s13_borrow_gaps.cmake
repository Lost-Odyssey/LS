# test_s13_borrow_gaps.cmake — plan_std_map §13 两挂账回归：
#  ① borrow-match binder Vec（含导入模块实例化 Vec(std_str__Str)）用 [i] 下标
#  ② 显式 `&局部` 实参传只读 `&T` 参数（剥壳走 auto-borrow 路径）
# 正向 JIT + AOT + memcheck；负向 `&x` 传 `&!T` 仍编译拒绝。
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/s13_borrow_gaps_test.ls")
set(NEG "${SAMPLE_DIR}/s13_amp_mutref_reject.ls")
set(_expected "S13_BORROW_GAPS PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "s13_borrow_gaps JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "s13_borrow_gaps JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "s13_borrow_gaps JIT has FAIL lines\n${jit_out}")
endif()
message(STATUS "s13_borrow_gaps JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/s13_borrow_gaps_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "s13_borrow_gaps AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "s13_borrow_gaps AOT run FAILED (rc=${aot_run_rc})")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "s13_borrow_gaps AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "s13_borrow_gaps AOT has FAIL lines\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "s13_borrow_gaps AOT: OK")

# ---- memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "s13_borrow_gaps memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "s13_borrow_gaps memcheck leak\n${mc_err}")
endif()
message(STATUS "s13_borrow_gaps memcheck: OK clean")

# ---- 负向：&x 传 &!T 仍拒绝 ----
execute_process(COMMAND "${LS_EXE}" run "${NEG}"
    OUTPUT_VARIABLE neg_out ERROR_VARIABLE neg_err RESULT_VARIABLE neg_rc)
if(neg_rc EQUAL 0)
    message(FATAL_ERROR "s13_amp_mutref_reject should FAIL to compile but succeeded\n${neg_out}")
endif()
if(NOT "${neg_err}${neg_out}" MATCHES "expected '&!Vec")
    message(FATAL_ERROR "s13_amp_mutref_reject wrong error\n${neg_err}\n${neg_out}")
endif()
message(STATUS "s13_amp_mutref_reject: correctly rejected")

message(STATUS "test_s13_borrow_gaps: ALL PASSED")
