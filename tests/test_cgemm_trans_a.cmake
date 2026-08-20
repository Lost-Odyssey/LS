# test_cgemm_trans_a.cmake -- column-major A (COp.Trans) and the M-vec path
#
# The same numbers must come out of two different kernels (M-vec for N < VL,
# masked N-vec otherwise). That equality is the observable consequence of the
# no-k-blocking / no-horizontal-reduce contract; if it ever breaks, some kernel
# started reassociating.
#
# @subsystem stdlib/sci
# @guards uk_8x1c_mvec, cgemm dispatch rule, M tail under M-vec
# @sources lib/std/sci/cgemm_kernels.lls lib/std/sci/cgemm.lls

if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_root}")

set(SAMPLE "${_root}/tests/samples/cgemm_trans_a.lls")

# ---- JIT path ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT "${jit_out}" MATCHES "ALL PASS" OR "${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_cgemm_trans_a JIT FAILED (exit ${jit_rc})\n"
        "stdout:\n${jit_out}\n"
        "stderr:\n${jit_err}")
endif()
message(STATUS "test_cgemm_trans_a JIT: OK")

# ---- memcheck path ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT "${mc_out}${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR
        "test_cgemm_trans_a memcheck FAILED (exit ${mc_rc})\n"
        "stdout:\n${mc_out}\n"
        "stderr:\n${mc_err}")
endif()
message(STATUS "test_cgemm_trans_a memcheck: OK")

# ---- AOT path ----
set(EXE "${WORK_DIR}/cgemm_trans_a_aot.exe")
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${EXE}"
    OUTPUT_VARIABLE c_out
    ERROR_VARIABLE  c_err
    RESULT_VARIABLE c_rc
)
if(NOT c_rc EQUAL 0)
    message(FATAL_ERROR
        "test_cgemm_trans_a AOT compile FAILED (exit ${c_rc})\n"
        "stdout:\n${c_out}\nstderr:\n${c_err}")
endif()
execute_process(
    COMMAND "${EXE}"
    OUTPUT_VARIABLE a_out
    ERROR_VARIABLE  a_err
    RESULT_VARIABLE a_rc
)
if(NOT "${a_out}" MATCHES "ALL PASS" OR "${a_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_cgemm_trans_a AOT FAILED (exit ${a_rc})\n"
        "stdout:\n${a_out}\nstderr:\n${a_err}")
endif()
message(STATUS "test_cgemm_trans_a AOT: OK")

# ---- LS_NO_FMA leg: contraction must stay consistent between reference and
# kernels. If this diverges, one side is contracting and the other is not, and
# every bit-exactness claim in this family is void.
set(ENV{LS_NO_FMA} "1")
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE nf_out
    ERROR_VARIABLE  nf_err
    RESULT_VARIABLE nf_rc
)
unset(ENV{LS_NO_FMA})
if(NOT "${nf_out}" MATCHES "ALL PASS" OR "${nf_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_cgemm_trans_a LS_NO_FMA FAILED (exit ${nf_rc})\n"
        "stdout:\n${nf_out}\nstderr:\n${nf_err}")
endif()
message(STATUS "test_cgemm_trans_a LS_NO_FMA: OK")

message(STATUS "test_cgemm_trans_a: ALL PASSED")
