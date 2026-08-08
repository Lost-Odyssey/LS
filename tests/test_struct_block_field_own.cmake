# test_struct_block_field_own.cmake — struct/array with a Block field: a
# by-value CLONE must DEEP-CLONE the Block field's env (independent), else the
# clone and the source release the same refcount=1 env at scope exit -> UAF +
# double-release. The double-release is invisible to memcheck (audit B-3), so
# the VALUE (captured int after heap churn) is the judge. Fix:
# emit_struct_clone_val / emit_array_clone_val deep-clone Block fields via
# cg_emit_block_env_clone (audit B-1 / BUG-2). JIT + AOT + memcheck 0/0/0.
#
# @subsystem codegen/ownership
# @guards BUG-2 (audit B-1/B-3), 988d3fa
# @sources codegen_own.c:emit_struct_clone_val, codegen_own.c:emit_array_clone_val, codegen_stmt.c:cg_emit_block_env_clone
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/struct_block_field_own_test.lls")
set(EXPECTED "STRUCTBLOCK PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "struct_block_field_own JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "${EXPECTED}")
    message(FATAL_ERROR "struct_block_field_own JIT missing '${EXPECTED}' (value check flipped -> UAF)\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "struct_block_field_own JIT had a failed check\n${jit_out}")
endif()
message(STATUS "struct_block_field_own JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/struct_block_field_own_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "struct_block_field_own AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "struct_block_field_own AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${EXPECTED}")
    message(FATAL_ERROR "struct_block_field_own AOT missing '${EXPECTED}'\n${aot_out}")
endif()
message(STATUS "struct_block_field_own AOT: OK")

# ---- memcheck (0 leak / 0 double-free / 0 invalid free) ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "struct_block_field_own memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "struct_block_field_own memcheck leak/double-free\n${mc_err}")
endif()
message(STATUS "struct_block_field_own memcheck: OK clean")
