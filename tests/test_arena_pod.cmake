# test_arena_pod.cmake — std.arena Phase 1: typed POD bump arena.
#  * Positive: alloc/get/set!/auto-grow/handle-linked list/reset+reuse
#    (JIT + AOT + memcheck 0/0/0).
#  * Negative: Arena(Str).alloc is a compile error (element type must be Pod).
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/arena_pod_test.lls")
set(_expected "ARENA POD PASS")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "arena_pod positive JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "arena_pod positive JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL ")
    message(FATAL_ERROR "arena_pod positive JIT had a FAIL line\n${jit_out}")
endif()
message(STATUS "arena_pod positive JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/arena_pod_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "arena_pod positive AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "arena_pod positive AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "arena_pod positive AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL ")
    message(FATAL_ERROR "arena_pod positive AOT had a FAIL line\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "arena_pod positive AOT: OK")

# ---- positive: memcheck (0 leak / 0 double-free / 0 invalid free) ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "arena_pod memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "arena_pod memcheck not clean\n${mc_err}")
endif()
message(STATUS "arena_pod positive memcheck: OK clean")

# ---- negative: Arena(Str).alloc must be a compile error ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/arena_pod_reject.lls"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR "arena_pod_reject: expected compile error, got success\n${n_out}")
endif()
string(APPEND n_all "${n_out}${n_err}")
if(NOT "${n_all}" MATCHES "does not implement Pod")
    message(FATAL_ERROR "arena_pod_reject: missing Pod diagnostic\n${n_all}")
endif()
if("${n_all}" MATCHES "unreachable")
    message(FATAL_ERROR "arena_pod_reject: ran past the rejected alloc\n${n_all}")
endif()
message(STATUS "arena_pod_reject: rejected as expected (rc=${n_rc})")

message(STATUS "test_arena_pod: ALL PASSED")
