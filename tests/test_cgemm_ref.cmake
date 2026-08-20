# test_cgemm_ref.cmake -- the scalar complex-GEMM reference
#
# The reference DEFINES the four-FMA order that every vector kernel must copy
# lane by lane, so this corpus is the anchor of every bit-exactness claim in the
# cgemm family. Values are hand-computed, not cross-checked against another
# implementation, on purpose: an oracle verified against a sibling implementation
# only proves they agree.
#
# @subsystem stdlib/sci
# @guards cgemm_ref operation order, accumulate-starts-at-C semantics
# @sources lib/std/sci/cgemm.lls

if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_root}")

set(SAMPLE "${_root}/tests/samples/cgemm_ref.lls")

# ---- JIT path ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT "${jit_out}" MATCHES "ALL PASS" OR "${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_cgemm_ref JIT FAILED (exit ${jit_rc})\n"
        "stdout:\n${jit_out}\n"
        "stderr:\n${jit_err}")
endif()
message(STATUS "test_cgemm_ref JIT: OK")

# ---- memcheck path ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT "${mc_out}${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR
        "test_cgemm_ref memcheck FAILED (exit ${mc_rc})\n"
        "stdout:\n${mc_out}\n"
        "stderr:\n${mc_err}")
endif()
message(STATUS "test_cgemm_ref memcheck: OK")

# ---- AOT path ----
set(EXE "${WORK_DIR}/cgemm_ref_aot.exe")
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${EXE}"
    OUTPUT_VARIABLE c_out
    ERROR_VARIABLE  c_err
    RESULT_VARIABLE c_rc
)
if(NOT c_rc EQUAL 0)
    message(FATAL_ERROR
        "test_cgemm_ref AOT compile FAILED (exit ${c_rc})\n"
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
        "test_cgemm_ref AOT FAILED (exit ${a_rc})\n"
        "stdout:\n${a_out}\nstderr:\n${a_err}")
endif()
message(STATUS "test_cgemm_ref AOT: OK")

message(STATUS "test_cgemm_ref: ALL PASSED")
