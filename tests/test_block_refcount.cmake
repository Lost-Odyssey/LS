# test_block_refcount.cmake — owned Block rvalue lifetime guardrail (P3 / O2).
# A factory call / force-unwrap of a container-get yields an OWNED Block whose
# env carries refcount 1: discarded => released at the statement flush, bound =>
# the binding owns it. Map(Block) rehash relocates entries by move. Guards the
# O1 (make()() leak), O3 (Vec/Map get!() consistency) and O2 (Map rehash
# double-free) fixes. JIT + AOT + memcheck 0/0/0.
#
# @subsystem codegen/ownership
# @guards Block env refcount O1/O2/O3 (fc741bf)
# @sources codegen_own.c:cg_spill_owned_rvalue, codegen_own.c:cg_track_block_rvalue, codegen_stmt.c:cg_emit_block_env_clone
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/block_refcount_test.lls")
set(EXPECTED "BLOCKRC PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "block_refcount JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "${EXPECTED}")
    message(FATAL_ERROR "block_refcount JIT missing '${EXPECTED}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "block_refcount JIT had a failed check\n${jit_out}")
endif()
message(STATUS "block_refcount JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/block_refcount_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "block_refcount AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "block_refcount AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${EXPECTED}")
    message(FATAL_ERROR "block_refcount AOT missing '${EXPECTED}'\n${aot_out}")
endif()
message(STATUS "block_refcount AOT: OK")

# ---- memcheck (0 leak / 0 double-free / 0 invalid free) ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "block_refcount memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "block_refcount memcheck leak/double-free\n${mc_err}")
endif()
message(STATUS "block_refcount memcheck: OK clean")
