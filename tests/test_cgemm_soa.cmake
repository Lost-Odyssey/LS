# test_cgemm_soa.cmake -- vector kernels against the scalar reference, bit-exact
#
# Judged as exact bit patterns, not a tolerance: the design's whole claim is that
# every kernel reproduces the reference's four-FMA order lane by lane, so a
# tolerance would hide the only class of failure worth catching.
#
# @subsystem stdlib/sci
# @guards uk_6x8c + uk_4x8c + uk_1x8c (M tail alone, N tail, both together) vs cgemm_ref, the M-based MR=6/MR=4 dispatch in cgemm_soa_nvec, accumulate path, FMA contraction parity, -0.0 CReLU regression
# @sources lib/std/sci/cgemm_kernels.lls lib/std/sci/cgemm.lls
#
# Injection experiments run against this corpus (each must turn it red). Be
# precise about what actually catches each one -- "caught" hides three
# different mechanisms and a future reader relying on this log to judge
# whether the corpus still has teeth needs to know which one:
#   1. uk_4x8c FMA #2: sng0 -> sam0 (drop the negation)               -> caught by same_bits (wrong value, clean FAIL line)
#   2. uk_1x8c masked store -> full-width store (OOB write)            -> caught by the allocator, NOT by same_bits: the write
#      corrupts malloc heap metadata and the process is killed before any
#      comparison runs (exit 0xc0000374 / STATUS_HEAP_CORRUPTION on this
#      platform, empty stdout -- no FAIL line ever gets printed). ctest still
#      fails this test on both its judgements (nonzero exit code, stdout
#      lacking "ALL PASS"), so the guard is effective, but a future OOB write
#      that happens to land in unused heap space could go undetected -- see
#      the comment on check(1,1,1,0) in cgemm_soa.lls for the same caveat on
#      that shape. Re-verified 2026-08-16: same exit code, same empty stdout.
#   3. uk_4x16c FMA #2 sign, under SDE (test_cgemm_sde_avx512)          -> caught by same_bits
#   4. MR dispatch, both arms (2026-08-19, added with the M-based MR=6/MR=4
#      rule): uk_4x8c's `ci2 = __simd_fma(sar2, bi, ci2)` -> `..., br, ...`
#      reddens EXACTLY M=4,8,20 and the M=8 epilogue case, and the same edit
#      in uk_6x8c reddens EXACTLY M=12,18,24,31,32,33 and the M=32 epilogue
#      cases -- i.e. the two injections partition the corpus along the
#      dispatch boundary, which is what makes the M=12/18/24 shapes evidence
#      that the `M % 6 == 0` clause is reached rather than decoration. M=1 and
#      M=3 stay green under both, correctly: below the register-block height
#      every row goes through uk_1x8c and neither core kernel runs. Caught by
#      same_bits (clean FAIL lines, no crash).
#
# A fourth experiment (2026-08-16, Task 11) is not in the drop-a-line-and-see
# category above: check_negzero's -0.0 assertion was validated by injecting
# `cr0 = cr0 + z` (adding +0.0) right after uk_1x8c's __simd_max epilogue --
# +0.0 destroys -0.0's sign under IEEE-754 addition, so this deterministically
# flips the kernel's bit pattern away from the reference's -0.0 while leaving
# every other shape in the corpus untouched. Caught by same_bits (clean
# FAIL_NEGZERO line, no crash). Confirms the assertion has teeth independent
# of whatever __simd_max itself happens to do on this build of LLVM.

if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_root}")

set(SAMPLE "${_root}/tests/samples/cgemm_soa.lls")

# ---- JIT path ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT "${jit_out}" MATCHES "ALL PASS" OR "${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_cgemm_soa JIT FAILED (exit ${jit_rc})\n"
        "stdout:\n${jit_out}\n"
        "stderr:\n${jit_err}")
endif()
message(STATUS "test_cgemm_soa JIT: OK")

# ---- JIT -O2 path ----
# `lls run` defaults to -O0, so the default JIT leg above exercises the
# UNOPTIMISED kernels only. The -0.0 CRelu divergence this corpus now pins was
# invisible at -O0 and only appeared from -O1 up, so a suite without this leg
# would have shipped it. Keep this leg.
execute_process(
    COMMAND "${LS_EXE}" run -O2 "${SAMPLE}"
    OUTPUT_VARIABLE o2_out
    ERROR_VARIABLE  o2_err
    RESULT_VARIABLE o2_rc
)
if(NOT "${o2_out}" MATCHES "ALL PASS" OR "${o2_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_cgemm_soa JIT -O2 FAILED (exit ${o2_rc})
"
        "stdout:
${o2_out}
stderr:
${o2_err}")
endif()
message(STATUS "test_cgemm_soa JIT -O2: OK")

# ---- memcheck path ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT "${mc_out}${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR
        "test_cgemm_soa memcheck FAILED (exit ${mc_rc})\n"
        "stdout:\n${mc_out}\n"
        "stderr:\n${mc_err}")
endif()
message(STATUS "test_cgemm_soa memcheck: OK")

# ---- AOT path ----
set(EXE "${WORK_DIR}/cgemm_soa_aot.exe")
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${EXE}"
    OUTPUT_VARIABLE c_out
    ERROR_VARIABLE  c_err
    RESULT_VARIABLE c_rc
)
if(NOT c_rc EQUAL 0)
    message(FATAL_ERROR
        "test_cgemm_soa AOT compile FAILED (exit ${c_rc})\n"
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
        "test_cgemm_soa AOT FAILED (exit ${a_rc})\n"
        "stdout:\n${a_out}\nstderr:\n${a_err}")
endif()
message(STATUS "test_cgemm_soa AOT: OK")

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
        "test_cgemm_soa LS_NO_FMA FAILED (exit ${nf_rc})\n"
        "stdout:\n${nf_out}\nstderr:\n${nf_err}")
endif()
message(STATUS "test_cgemm_soa LS_NO_FMA: OK")

message(STATUS "test_cgemm_soa: ALL PASSED")
