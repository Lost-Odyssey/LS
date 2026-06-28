# test_xmod_generic.cmake — VR-LIM-018 / F6: a consumer pattern-matches an
# imported enum whose payload is a std.vec Vec(T), and calls Vec methods
# (.len()/.get()/[i]/for-in) on the binder. The consumer does NOT import
# std.vec directly — the template is pulled transitively. JIT + AOT + memcheck.
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/xmod_generic/main.ls")
set(_expected "n=3 a=10 b=30 sum=60" "XMOD_GENERIC PASS" "XMOD_GENERIC_DROP PASS")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "xmod_generic JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "xmod_generic JIT missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "xmod_generic JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/xmod_generic_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "xmod_generic AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "xmod_generic AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "xmod_generic AOT missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "xmod_generic AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck (0 leak / 0 double-free) ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "xmod_generic --memcheck FAILED\nstderr:\n${mc_err}")
endif()
message(STATUS "xmod_generic memcheck: OK clean")
message(STATUS "test_xmod_generic: ALL PASSED")
