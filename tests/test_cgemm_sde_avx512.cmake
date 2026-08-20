# test_cgemm_sde_avx512.cmake -- the AVX-512 kernels, bit-exact, under Intel SDE
#
# This machine is AVX2-only, so the 16-wide path cannot execute natively. SDE is
# a FUNCTIONAL simulator: it proves the numbers, it says nothing about cycles.
# Any performance claim about AVX-512 must come from real hardware.
#
# @subsystem stdlib/sci
# @guards uk_4x16c / uk_1x16c / uk_16x1c_mvec vs cgemm_ref, under Intel SDE (-gnr)
# @sources lib/std/sci/cgemm_kernels.lls lib/std/sci/cgemm.lls

if(DEFINED ENV{CMAKE_SOURCE_DIR_OVERRIDE})
    set(_root "$ENV{CMAKE_SOURCE_DIR_OVERRIDE}")
else()
    get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
endif()
set(ENV{LS_HOME} "${_root}")

set(SDE_EXE "${_root}/tools/sde-external-10.8.0-2026-03-15-win/sde.exe")
if(NOT EXISTS "${SDE_EXE}")
    message(STATUS "test_cgemm_sde_avx512: SDE not present, SKIPPED")
    return()
endif()

set(SAMPLE "${_root}/tests/samples/cgemm_w16.lls")
set(EXE "${WORK_DIR}/cgemm_w16_gnr.exe")

# ---- AOT compile, cross-targeted to Granite Rapids (emits zmm / AVX-512) ----
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${EXE}" --target=graniterapids
    OUTPUT_VARIABLE c_out
    ERROR_VARIABLE  c_err
    RESULT_VARIABLE c_rc
)
if(NOT c_rc EQUAL 0)
    message(FATAL_ERROR
        "test_cgemm_sde_avx512 AOT compile FAILED (exit ${c_rc})\n"
        "stdout:\n${c_out}\nstderr:\n${c_err}")
endif()

# NOTE: this exe is intentionally NOT run natively here. It cross-compiled
# real AVX-512 instructions for a host CPU without AVX-512, so a native run
# reliably dies with Illegal instruction (verified: exit 132). Running it is
# what SDE is for.

# ---- run under SDE (-gnr = Granite Rapids, matches --target above) ----
execute_process(
    COMMAND "${SDE_EXE}" -gnr -- "${EXE}"
    OUTPUT_VARIABLE sde_out
    ERROR_VARIABLE  sde_err
    RESULT_VARIABLE sde_rc
)
if(NOT "${sde_out}" MATCHES "ALL PASS" OR "${sde_out}" MATCHES "FAIL")
    message(FATAL_ERROR
        "test_cgemm_sde_avx512 SDE run FAILED (exit ${sde_rc})\n"
        "stdout:\n${sde_out}\nstderr:\n${sde_err}")
endif()

message(STATUS "test_cgemm_sde_avx512: ALL PASSED")
