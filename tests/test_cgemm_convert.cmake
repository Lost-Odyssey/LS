# test_cgemm_convert.cmake -- generic layout converters, round-trip identities
#
# Judged by round-trip identities (aos->soa->aos, transpose twice, conj twice)
# plus a would-catch-a-copy assertion per converter (deinterleave lands in the
# right plane, transpose actually moves (i,j) to (j,i), conj actually flips a
# known nonzero imaginary sign) -- a round-trip alone cannot tell a correct
# converter from a pair of mutually-inverse bugs that cancel.
#
# @subsystem stdlib/sci
# @guards cconv_aos_to_soa/cconv_soa_to_aos/ctranspose_soa/cconj_soa round-trip + non-round-trip position/sign checks, square + non-square + non-multiple-of-8 shapes, padded strides
# @sources lib/std/sci/cgemm_kernels.lls

if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_root}")

set(SAMPLE "${_root}/tests/samples/cgemm_convert.lls")

# ---- JIT path ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT "${jit_out}" MATCHES "ALL PASS" OR "${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_cgemm_convert JIT FAILED (exit ${jit_rc})\n"
        "stdout:\n${jit_out}\n"
        "stderr:\n${jit_err}")
endif()
message(STATUS "test_cgemm_convert JIT: OK")

# ---- memcheck path ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT "${mc_out}${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR
        "test_cgemm_convert memcheck FAILED (exit ${mc_rc})\n"
        "stdout:\n${mc_out}\n"
        "stderr:\n${mc_err}")
endif()
message(STATUS "test_cgemm_convert memcheck: OK")

# ---- AOT path ----
set(EXE "${WORK_DIR}/cgemm_convert_aot.exe")
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${EXE}"
    OUTPUT_VARIABLE c_out
    ERROR_VARIABLE  c_err
    RESULT_VARIABLE c_rc
)
if(NOT c_rc EQUAL 0)
    message(FATAL_ERROR
        "test_cgemm_convert AOT compile FAILED (exit ${c_rc})\n"
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
        "test_cgemm_convert AOT FAILED (exit ${a_rc})\n"
        "stdout:\n${a_out}\nstderr:\n${a_err}")
endif()
message(STATUS "test_cgemm_convert AOT: OK")

message(STATUS "test_cgemm_convert: ALL PASSED")
