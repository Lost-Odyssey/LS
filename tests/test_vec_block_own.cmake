# test_vec_block_own.cmake — Vec(Block) element-copy ownership guardrail.
# copy()/extend() must deep-clone each element's closure env (not share it);
# a shared env double-frees at scope exit. Fix: @dup(Block) env-clones +
# Vec.extend routes through @dup. (match_codegen_guide.html §7.A.)
# JIT + AOT + memcheck 0/0/0.
#
# @subsystem codegen/ownership
# @guards Vec(Block) element copy shares env (8bcabf4)
# @sources codegen_own.c:emit_clone_value, lib/std/core/vec.lls
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/vec_block_own_test.lls")
set(EXPECTED "VECBLOCK PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "vec_block_own JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "${EXPECTED}")
    message(FATAL_ERROR "vec_block_own JIT missing '${EXPECTED}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "vec_block_own JIT had a failed check\n${jit_out}")
endif()
message(STATUS "vec_block_own JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/vec_block_own_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "vec_block_own AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "vec_block_own AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${EXPECTED}")
    message(FATAL_ERROR "vec_block_own AOT missing '${EXPECTED}'\n${aot_out}")
endif()
message(STATUS "vec_block_own AOT: OK")

# ---- memcheck (0 leak / 0 double-free / 0 invalid free) ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "vec_block_own memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "vec_block_own memcheck leak/double-free\n${mc_err}")
endif()
message(STATUS "vec_block_own memcheck: OK clean")
