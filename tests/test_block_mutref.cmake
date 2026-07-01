# test_block_mutref.cmake — Block(&!T) 可写借用块参数（plan_std_map §13 收官）
# 正向 JIT + AOT + memcheck（Vec/Str/Map 载体、转发、混合参数、只读对照）；
# 负向①只读 &T 体内调 &!self 方法拒绝；负向②裸 v 传 &!T 拒绝。
cmake_minimum_required(VERSION 3.20)

set(POS  "${SAMPLE_DIR}/block_mutref_test.lls")
set(NEG1 "${SAMPLE_DIR}/block_mutref_readonly_reject.lls")
set(NEG2 "${SAMPLE_DIR}/block_mutref_bare_reject.lls")
set(_expected "BLOCK_MUTREF PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "block_mutref JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "block_mutref JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "block_mutref JIT has FAIL lines\n${jit_out}")
endif()
message(STATUS "block_mutref JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/block_mutref_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "block_mutref AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "block_mutref AOT run FAILED (rc=${aot_run_rc})")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "block_mutref AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "block_mutref AOT has FAIL lines\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "block_mutref AOT: OK")

# ---- memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "block_mutref memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "block_mutref memcheck leak\n${mc_err}")
endif()
message(STATUS "block_mutref memcheck: OK clean")

# ---- 负向①：只读 &T 闭包体内调 &!self 方法 ----
execute_process(COMMAND "${LS_EXE}" run "${NEG1}"
    OUTPUT_VARIABLE n1_out ERROR_VARIABLE n1_err RESULT_VARIABLE n1_rc)
if(n1_rc EQUAL 0)
    message(FATAL_ERROR "block_mutref_readonly_reject should FAIL but succeeded\n${n1_out}")
endif()
if(NOT "${n1_err}${n1_out}" MATCHES "read-only borrow")
    message(FATAL_ERROR "block_mutref_readonly_reject wrong error\n${n1_err}\n${n1_out}")
endif()
message(STATUS "block_mutref_readonly_reject: correctly rejected")

# ---- 负向②：裸 v 传 Block(&!T) ----
execute_process(COMMAND "${LS_EXE}" run "${NEG2}"
    OUTPUT_VARIABLE n2_out ERROR_VARIABLE n2_err RESULT_VARIABLE n2_rc)
if(n2_rc EQUAL 0)
    message(FATAL_ERROR "block_mutref_bare_reject should FAIL but succeeded\n${n2_out}")
endif()
if(NOT "${n2_err}${n2_out}" MATCHES "expected '&!Vec")
    message(FATAL_ERROR "block_mutref_bare_reject wrong error\n${n2_err}\n${n2_out}")
endif()
message(STATUS "block_mutref_bare_reject: correctly rejected")

message(STATUS "test_block_mutref: ALL PASSED")
