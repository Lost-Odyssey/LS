# test_modvar.cmake — Part 1 module global variables (P1-1 ~ P1-4)
#
# Covers:
#   P1-1: single-module global (basic read/write)
#   P1-2: cross-module same-name globals (namespace isolation)
#   P1-3: has_drop global (string) — memcheck clean
#   P1-4: accumulator pattern (multiple functions sharing one global)
#   P1-4: external read via mod.VAR syntax
cmake_minimum_required(VERSION 3.20)

# ---- helper ----
macro(run_jit sample label)
    execute_process(
        COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/${sample}/main.ls"
        OUTPUT_VARIABLE _jit_out  ERROR_VARIABLE _jit_err  RESULT_VARIABLE _jit_rc
    )
    if(NOT _jit_rc EQUAL 0)
        message(FATAL_ERROR "${label} JIT FAILED (rc=${_jit_rc})\nstderr:\n${_jit_err}")
    endif()
    if(NOT "${_jit_out}" MATCHES "${label} PASS")
        message(FATAL_ERROR "${label} JIT missing '${label} PASS'\nstdout:\n${_jit_out}")
    endif()
    message(STATUS "${label} JIT: OK")
endmacro()

macro(run_aot sample label)
    set(_aot_bin "${WORK_DIR}/${label}_aot")
    if(WIN32)
        set(_aot_bin "${_aot_bin}.exe")
    endif()
    execute_process(
        COMMAND "${LS_EXE}" compile "${SAMPLE_DIR}/${sample}/main.ls" -o "${_aot_bin}"
        RESULT_VARIABLE _aot_rc  ERROR_VARIABLE _aot_err
    )
    if(NOT _aot_rc EQUAL 0)
        message(FATAL_ERROR "${label} AOT compile FAILED:\n${_aot_err}")
    endif()
    execute_process(
        COMMAND "${_aot_bin}"
        OUTPUT_VARIABLE _aot_out  RESULT_VARIABLE _aot_run_rc
    )
    if(NOT _aot_run_rc EQUAL 0)
        message(FATAL_ERROR "${label} AOT run FAILED (rc=${_aot_run_rc})")
    endif()
    if(NOT "${_aot_out}" MATCHES "${label} PASS")
        message(FATAL_ERROR "${label} AOT missing '${label} PASS'\nstdout:\n${_aot_out}")
    endif()
    message(STATUS "${label} AOT: OK")
    file(REMOVE "${_aot_bin}")
endmacro()

macro(run_memcheck sample label)
    execute_process(
        COMMAND "${LS_EXE}" run --memcheck "${SAMPLE_DIR}/${sample}/main.ls"
        OUTPUT_VARIABLE _mc_out  ERROR_VARIABLE _mc_err  RESULT_VARIABLE _mc_rc
    )
    if(NOT _mc_rc EQUAL 0)
        message(FATAL_ERROR "${label} memcheck run FAILED (rc=${_mc_rc})\nstderr:\n${_mc_err}")
    endif()
    if(NOT "${_mc_err}" MATCHES "OK clean")
        message(FATAL_ERROR "${label} --memcheck FAILED\nstderr:\n${_mc_err}")
    endif()
    message(STATUS "${label} memcheck: OK clean")
endmacro()

# ---- P1-1: single-module global basic read/write ----
run_jit(modvar_basic MODVAR_BASIC)
run_aot(modvar_basic MODVAR_BASIC)
run_memcheck(modvar_basic MODVAR_BASIC)

# ---- P1-2: cross-module same-name globals ----
run_jit(modvar_cross MODVAR_CROSS)
run_aot(modvar_cross MODVAR_CROSS)
run_memcheck(modvar_cross MODVAR_CROSS)

# ---- P1-3: has_drop global (string) ----
run_jit(modvar_hasdrop MODVAR_HASDROP)
run_aot(modvar_hasdrop MODVAR_HASDROP)
run_memcheck(modvar_hasdrop MODVAR_HASDROP)

# ---- P1-4: accumulator pattern ----
run_jit(modvar_accum MODVAR_ACCUM)
run_aot(modvar_accum MODVAR_ACCUM)
run_memcheck(modvar_accum MODVAR_ACCUM)

# ---- P1-4: external read via mod.VAR ----
run_jit(modvar_access MODVAR_ACCESS)
run_aot(modvar_access MODVAR_ACCESS)
run_memcheck(modvar_access MODVAR_ACCESS)

# ---- P1-3 regression: OWNED global string returned by a getter ----
# The hasdrop sample above uses a static literal (cap=0, never freed) which does
# NOT exercise the double-free. This one uses an OWNED global string (.upper())
# returned by name + bound by the caller; the return path must clone (else the
# caller's free + __ls_global_cleanup double-free the shared data). memcheck is
# the critical assertion here.
run_jit(modvar_owned_return MODVAR_OWNED_RETURN)
run_aot(modvar_owned_return MODVAR_OWNED_RETURN)
run_memcheck(modvar_owned_return MODVAR_OWNED_RETURN)

message(STATUS "test_modvar: ALL PASSED")
