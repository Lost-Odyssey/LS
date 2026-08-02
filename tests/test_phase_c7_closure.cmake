# test_phase_c7_closure.cmake - Phase C.7 (revised): Vec/Map/struct by-move.
# Expected output: 10, 30, hello-world, 90, 0, LVL:7
#
# Phase C.7: closures capturing values that OWN heap (Vec, Map, has_drop struct).
#
# This is where capture stopped being a copy. An owned capture is moved into the
# closure's environment, the outer variable is marked moved, and the environment
# gains a generated destructor that releases each captured field. Three things
# have to agree -- the checker's move marking, the environment layout, and the
# per-closure drop function -- and a mismatch in any one of them is a leak or a
# double free rather than a compile error.
#
# @subsystem codegen/closure
# @guards Phase C.7 Vec/Map/struct(drop) by-move captures
# @sources codegen_closure.c:capture_type_is_by_ref_cg

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/closure_phase_c7_test.lls")

set(_expected "10" "30" "hello-world" "90" "0" "LVL:7")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
)
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "Phase C.7 JIT FAILED: missing line '${_line}' in output\n"
            "stdout:\n${jit_out}\nstderr:\n${jit_err}")
    endif()
endforeach()
message(STATUS "phase_c7 JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/closure_phase_c7_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "Phase C.7 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
)
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "Phase C.7 AOT FAILED: missing line '${_line}' in output\n"
            "stdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "phase_c7 AOT: OK")

# ---- Memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    ERROR_VARIABLE  mc_err
)
if(NOT "${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\)")
    message(FATAL_ERROR "Phase C.7 memcheck FAILED: leaks reported\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "0 double-free")
    message(FATAL_ERROR "Phase C.7 memcheck FAILED: double-free reported\n${mc_err}")
endif()
message(STATUS "phase_c7 memcheck: 0 leaks / 0 double-free OK")

message(STATUS "Phase C.7 Vec/Map/struct captures: ALL OK")
