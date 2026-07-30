# test_iface_generic_sig.cmake -- interface signature validation.
#
# Section 1 (Task 1): FromList/FromPairs are MARKER protocol interfaces -- their
#   registered signature has param_count 0 on purpose (arity and the element types
#   come from the implementing type's own generics). Comparing a real impl's arity
#   against that placeholder rejected every non-generic FromList impl, making a
#   documented opt-in unusable.
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/iface_marker_ok.lls")
set(_expected "total=60 calls=3")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "iface_marker_ok JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "iface_marker_ok JIT missing '${_expected}'\n${jit_out}")
endif()
message(STATUS "iface_marker_ok JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/iface_marker_ok_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "iface_marker_ok AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "iface_marker_ok AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "iface_marker_ok AOT missing '${_expected}'\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "iface_marker_ok AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "iface_marker_ok memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "iface_marker_ok memcheck not clean\n${mc_err}")
endif()
message(STATUS "iface_marker_ok memcheck: OK clean")

message(STATUS "test_iface_generic_sig: ALL PASSED")
