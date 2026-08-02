# test_vec_global_drop.cmake — VR-LIM-002 global user Vec(T) cleanup.
#
# Globals are released before main returns, by the generated cleanup function.
# Pre-fix that function only knew two shapes: TYPE_STRING (free when cap>0) and
# the then-builtin TYPE_VECTOR (drop each element, free the buffer). A pure-LS
# `Vec(T)` is neither -- it is a has_drop STRUCT -- so a global Vec never got
# its `__drop` and its raw buffer leaked for the whole process.
#
# The asymmetry is what let this survive: LOCAL Vecs were always fine, because
# scope cleanup has had a `TYPE_STRUCT && has_drop` branch all along. Only
# globals lacked it, and only memcheck can see the difference -- the program
# prints the right numbers either way.
#
# Two places had to change, and doing only one is a silent no-op: the predicate
# deciding whether a cleanup function is emitted AT ALL was also limited to
# string/vector, so adding the struct branch to the body without widening the
# predicate leaves that body unreachable.
#
# The corpus covers three element kinds because they leak for different reasons:
# `Vec(int)` (buffer only), `Vec(Str)` (buffer + per-element heap), `Vec(Item)`
# with Item a has_drop struct (buffer + per-element `__drop` -> nested Str).
# Values are checked too (VEC_GLOBAL_DROP PASS, never FAIL), so a "fix" that
# drops elements too eagerly and corrupts the reads cannot pass by being
# merely leak-free.
#
# @subsystem codegen/ownership
# @guards VR-LIM-002 global Vec cleanup
# @sources codegen.c:cg_emit_global_cleanup, codegen_own.c:emit_auto_drop_fn

cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE OR NOT WORK_DIR)
    message(FATAL_ERROR "test_vec_global_drop.cmake requires LS_EXE, SAMPLE, WORK_DIR")
endif()

if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE _jit_out ERROR_VARIABLE _jit_err RESULT_VARIABLE _jit_rc
)
if(NOT _jit_rc EQUAL 0)
    message(FATAL_ERROR "vec_global_drop JIT failed (rc=${_jit_rc})\nstderr:\n${_jit_err}")
endif()
if(_jit_out MATCHES "FAIL" OR NOT _jit_out MATCHES "VEC_GLOBAL_DROP PASS")
    message(FATAL_ERROR "vec_global_drop JIT bad output:\n${_jit_out}")
endif()

execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE _mc_out ERROR_VARIABLE _mc_err RESULT_VARIABLE _mc_rc
)
if(NOT _mc_rc EQUAL 0)
    message(FATAL_ERROR "vec_global_drop memcheck run failed (rc=${_mc_rc})\nstderr:\n${_mc_err}")
endif()
if(NOT _mc_err MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "vec_global_drop memcheck SUMMARY mismatch\nstderr:\n${_mc_err}")
endif()

set(_aot_bin "${WORK_DIR}/vec_global_drop_aot")
if(WIN32)
    set(_aot_bin "${_aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${_aot_bin}"
    RESULT_VARIABLE _aot_rc ERROR_VARIABLE _aot_err
)
if(NOT _aot_rc EQUAL 0)
    message(FATAL_ERROR "vec_global_drop AOT compile failed:\n${_aot_err}")
endif()
execute_process(
    COMMAND "${_aot_bin}"
    OUTPUT_VARIABLE _aot_out RESULT_VARIABLE _aot_run_rc
)
file(REMOVE "${_aot_bin}")
if(NOT _aot_run_rc EQUAL 0)
    message(FATAL_ERROR "vec_global_drop AOT run failed (rc=${_aot_run_rc})\nstdout:\n${_aot_out}")
endif()
if(_aot_out MATCHES "FAIL" OR NOT _aot_out MATCHES "VEC_GLOBAL_DROP PASS")
    message(FATAL_ERROR "vec_global_drop AOT bad output:\n${_aot_out}")
endif()

message(STATUS "vec_global_drop: JIT + AOT + memcheck PASS")
