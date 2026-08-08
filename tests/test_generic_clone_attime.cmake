# test_generic_clone_attime.cmake — @time/@bench inside a generic method body:
# ast_clone_deep must deep-copy their child expr (silent shallow-copy default
# used to alias the subtree between template and per-instantiation clones →
# compiler heap corruption). JIT + AOT + memcheck.
#
# `ast_clone_deep` must handle every node kind that can appear in a function body.
#
# A generic method body is cloned once per instantiation. Four node kinds fell
# through to a `default` that shallow-copied them, so the clone and the template
# shared owned pointers -- and `ast_free` then released the same subtree twice. A
# generic method containing `@time` or `@bench` therefore crashed the COMPILER
# with heap corruption (0xC0000374, all output lost).
#
# The fix removed the `default` entirely, so a newly added node kind now fails to
# compile under -Wswitch instead of silently corrupting the heap -- which is why
# this corpus instantiates the same generic body twice.
#
# @subsystem frontend/ast
# @guards ast_clone_deep shallow-copied @time/@bench -> heap corruption (7ff720b)
# @sources ast.c:ast_clone_deep
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/generic_clone_attime.lls")
set(_expected "GEN CLONE 7 2.5")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "generic_clone_attime JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "generic_clone_attime JIT missing '${_expected}'\nstdout:\n${jit_out}")
endif()
message(STATUS "generic_clone_attime JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/generic_clone_attime_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "generic_clone_attime AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "generic_clone_attime AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "generic_clone_attime AOT missing '${_expected}'\nstdout:\n${aot_out}")
endif()
message(STATUS "generic_clone_attime AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "generic_clone_attime memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "generic_clone_attime --memcheck FAILED\nstderr:\n${mc_err}")
endif()
message(STATUS "generic_clone_attime memcheck: OK clean")

message(STATUS "test_generic_clone_attime: ALL PASSED")
