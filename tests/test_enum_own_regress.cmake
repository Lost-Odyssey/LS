# test_enum_own_regress.cmake — two memcheck-found ownership regressions:
#   1. self-recursive enum ctor (Node(.., tree, tree)) bit-copied the boxes
#      and left the named source live -> shared boxes DOUBLE-FREED at scope
#      exit (fixed: boxing goes through cg_store_owned, codegen_decl.c).
#   2. bare `return` inside a match arm over an owned rvalue subject skipped
#      cg_flush_temps_scope_exit -> the subject's heap payload LEAKED
#      (fixed: flush hoisted out of the value-expr guard, codegen_stmt.c).
# JIT + AOT + memcheck (0 leak / 0 double-free).
#
# @subsystem codegen/ownership
# @guards BF: recursive enum ctor double-free + bare-return leak (29b4fa3)
# @sources codegen_decl.c:emit_enum_ctor, codegen_own.c:cg_store_owned, codegen_stmt.c:cg_stmt_return, codegen_own.c:cg_flush_temps_scope_exit
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/enum_own_regress_test.lls")
set(_expected "OWNREG PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "enum_own_regress JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "enum_own_regress JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL:")
    message(FATAL_ERROR "enum_own_regress JIT had a failed check\n${jit_out}")
endif()
message(STATUS "enum_own_regress JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/enum_own_regress_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "enum_own_regress AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "enum_own_regress AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "enum_own_regress AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL:")
    message(FATAL_ERROR "enum_own_regress AOT had a failed check\n${aot_out}")
endif()
message(STATUS "enum_own_regress AOT: OK")

# ---- memcheck (0 leak / 0 double-free) ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "enum_own_regress memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "enum_own_regress memcheck leak/double-free\n${mc_err}")
endif()
message(STATUS "enum_own_regress memcheck: OK clean")
