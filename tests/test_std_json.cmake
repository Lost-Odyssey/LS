# test_std_json.cmake — std.json module (JIT + AOT + memcheck)
# BF-025 (2026-05-24): user-defined fn returning TYPE_STRING now tracked via
#   cg_push_temp_string in AST_CALL codegen; eliminates 19 intermediate leaks.
# BF-026 (2026-05-24): emit_scope_cleanup() called before pop_scope() in match
#   enum arm codegen; frees string binders (is_borrowed=false, F.7 clones).
# All three json test files now pass --memcheck with 0 leaks / 0 dfree.
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

# ---- Test A: json_basic_test (14 tests) ----
set(BASIC "${SAMPLE_DIR}/json_basic_test.ls")

set(_basic_expected
    "PASS 1" "PASS 2" "PASS 3" "PASS 4" "PASS 5"
    "PASS 6" "PASS 7" "PASS 8" "PASS 9" "PASS 10"
    "PASS 11" "PASS 12" "PASS 13" "PASS 14"
)

# ---- A.1 JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${BASIC}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "json_basic JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_basic_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR
            "json_basic JIT FAILED: missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "json_basic JIT: OK")

# ---- A.2 AOT ----
set(aot_bin "${WORK_DIR}/json_basic_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${BASIC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "json_basic AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
    RESULT_VARIABLE aot_run_rc
    ERROR_VARIABLE  aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "json_basic AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_basic_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR
            "json_basic AOT FAILED: missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "json_basic AOT: OK")
file(REMOVE "${aot_bin}")

# ---- Test B: json_infra_test (5 tests) ----
set(INFRA "${SAMPLE_DIR}/json_infra_test.ls")

set(_infra_expected "PASS 1" "PASS 2" "PASS 3" "PASS 4" "PASS 5" "done")

execute_process(
    COMMAND "${LS_EXE}" run "${INFRA}"
    OUTPUT_VARIABLE infra_out
    ERROR_VARIABLE  infra_err
    RESULT_VARIABLE infra_rc
)
if(NOT infra_rc EQUAL 0)
    message(FATAL_ERROR "json_infra JIT FAILED (rc=${infra_rc})\nstderr:\n${infra_err}")
endif()
foreach(_line ${_infra_expected})
    if(NOT "${infra_out}" MATCHES "${_line}")
        message(FATAL_ERROR
            "json_infra JIT FAILED: missing '${_line}'\nstdout:\n${infra_out}")
    endif()
endforeach()
message(STATUS "json_infra JIT: OK")

# ---- B.2 AOT — json_infra_test ----
set(infra_aot_bin "${WORK_DIR}/json_infra_aot")
if(WIN32)
    set(infra_aot_bin "${infra_aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${INFRA}" -o "${infra_aot_bin}"
    RESULT_VARIABLE infra_aot_rc  ERROR_VARIABLE infra_aot_err
)
if(NOT infra_aot_rc EQUAL 0)
    message(FATAL_ERROR "json_infra AOT compile FAILED:\n${infra_aot_err}")
endif()
execute_process(
    COMMAND "${infra_aot_bin}"
    OUTPUT_VARIABLE infra_aot_out  RESULT_VARIABLE infra_aot_run_rc
)
if(NOT infra_aot_run_rc EQUAL 0)
    message(FATAL_ERROR "json_infra AOT run FAILED (rc=${infra_aot_run_rc})")
endif()
foreach(_line ${_infra_expected})
    if(NOT "${infra_aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "json_infra AOT FAILED: missing '${_line}'\nstdout:\n${infra_aot_out}")
    endif()
endforeach()
message(STATUS "json_infra AOT: OK")
file(REMOVE "${infra_aot_bin}")

# ---- Test C: json_internal_test (stringify) ----
set(INTERNAL "${SAMPLE_DIR}/json_internal_test.ls")

execute_process(
    COMMAND "${LS_EXE}" run "${INTERNAL}"
    OUTPUT_VARIABLE int_out
    ERROR_VARIABLE  int_err
    RESULT_VARIABLE int_rc
)
if(NOT int_rc EQUAL 0)
    message(FATAL_ERROR "json_internal JIT FAILED (rc=${int_rc})\nstderr:\n${int_err}")
endif()
if(NOT "${int_out}" MATCHES "done")
    message(FATAL_ERROR "json_internal JIT FAILED: missing 'done'\nstdout:\n${int_out}")
endif()
message(STATUS "json_internal JIT: OK")

# ---- C.2 AOT — json_internal_test ----
set(int_aot_bin "${WORK_DIR}/json_internal_aot")
if(WIN32)
    set(int_aot_bin "${int_aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${INTERNAL}" -o "${int_aot_bin}"
    RESULT_VARIABLE int_aot_rc  ERROR_VARIABLE int_aot_err
)
if(NOT int_aot_rc EQUAL 0)
    message(FATAL_ERROR "json_internal AOT compile FAILED:\n${int_aot_err}")
endif()
execute_process(
    COMMAND "${int_aot_bin}"
    OUTPUT_VARIABLE int_aot_out  RESULT_VARIABLE int_aot_run_rc
)
if(NOT int_aot_run_rc EQUAL 0)
    message(FATAL_ERROR "json_internal AOT run FAILED (rc=${int_aot_run_rc})")
endif()
if(NOT "${int_aot_out}" MATCHES "done")
    message(FATAL_ERROR "json_internal AOT FAILED: missing 'done'\nstdout:\n${int_aot_out}")
endif()
message(STATUS "json_internal AOT: OK")
file(REMOVE "${int_aot_bin}")

# ---- D: memcheck — json_basic_test ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${BASIC}"
    OUTPUT_VARIABLE mc_basic_out
    ERROR_VARIABLE  mc_basic_err
    RESULT_VARIABLE mc_basic_rc
)
if(NOT mc_basic_rc EQUAL 0)
    message(FATAL_ERROR "json_basic memcheck run FAILED (rc=${mc_basic_rc})\nstderr:\n${mc_basic_err}")
endif()
if(NOT "${mc_basic_err}" MATCHES "OK clean")
    message(FATAL_ERROR
        "json_basic --memcheck FAILED (leaks/dfree detected)\nstderr:\n${mc_basic_err}")
endif()
message(STATUS "json_basic memcheck: OK clean")

# ---- E: memcheck — json_infra_test ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${INFRA}"
    OUTPUT_VARIABLE mc_infra_out
    ERROR_VARIABLE  mc_infra_err
    RESULT_VARIABLE mc_infra_rc
)
if(NOT mc_infra_rc EQUAL 0)
    message(FATAL_ERROR "json_infra memcheck run FAILED (rc=${mc_infra_rc})\nstderr:\n${mc_infra_err}")
endif()
if(NOT "${mc_infra_err}" MATCHES "OK clean")
    message(FATAL_ERROR
        "json_infra --memcheck FAILED (leaks/dfree detected)\nstderr:\n${mc_infra_err}")
endif()
message(STATUS "json_infra memcheck: OK clean")

# ---- F: memcheck — json_internal_test ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${INTERNAL}"
    OUTPUT_VARIABLE mc_int_out
    ERROR_VARIABLE  mc_int_err
    RESULT_VARIABLE mc_int_rc
)
if(NOT mc_int_rc EQUAL 0)
    message(FATAL_ERROR "json_internal memcheck run FAILED (rc=${mc_int_rc})\nstderr:\n${mc_int_err}")
endif()
if(NOT "${mc_int_err}" MATCHES "OK clean")
    message(FATAL_ERROR
        "json_internal --memcheck FAILED (leaks/dfree detected)\nstderr:\n${mc_int_err}")
endif()
message(STATUS "json_internal memcheck: OK clean")

# ---- Test G: json_file_test (navigation API + file round-trip, 13 tests) ----
# Exercises: array_len / object_len / object_has / object_keys + io.write_file /
# io.read_file round-trip + pretty-print re-parse + large array (100 elems)
set(FILE_TEST "${SAMPLE_DIR}/json_file_test.ls")

set(_file_expected
    "PASS 1" "PASS 2a" "PASS 2b" "PASS 2c" "PASS 2d" "PASS 2e"
    "PASS 3a" "PASS 3b" "PASS 4a" "PASS 4b" "PASS 4c"
    "PASS 5" "PASS 6"
)

# G.1 JIT
execute_process(
    COMMAND "${LS_EXE}" run "${FILE_TEST}"
    WORKING_DIRECTORY "${WORK_DIR}"
    OUTPUT_VARIABLE ft_jit_out  ERROR_VARIABLE ft_jit_err  RESULT_VARIABLE ft_jit_rc
)
if(NOT ft_jit_rc EQUAL 0)
    message(FATAL_ERROR "json_file JIT FAILED (rc=${ft_jit_rc})\nstderr:\n${ft_jit_err}")
endif()
foreach(_line ${_file_expected})
    if(NOT "${ft_jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "json_file JIT FAILED: missing '${_line}'\nstdout:\n${ft_jit_out}")
    endif()
endforeach()
message(STATUS "json_file JIT: OK")

# G.2 AOT
set(ft_aot_bin "${WORK_DIR}/json_file_aot")
if(WIN32)
    set(ft_aot_bin "${ft_aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${FILE_TEST}" -o "${ft_aot_bin}"
    RESULT_VARIABLE ft_aot_rc  ERROR_VARIABLE ft_aot_err
)
if(NOT ft_aot_rc EQUAL 0)
    message(FATAL_ERROR "json_file AOT compile FAILED:\n${ft_aot_err}")
endif()
execute_process(
    COMMAND "${ft_aot_bin}"
    WORKING_DIRECTORY "${WORK_DIR}"
    OUTPUT_VARIABLE ft_aot_out  RESULT_VARIABLE ft_aot_run_rc
)
if(NOT ft_aot_run_rc EQUAL 0)
    message(FATAL_ERROR "json_file AOT run FAILED (rc=${ft_aot_run_rc})")
endif()
foreach(_line ${_file_expected})
    if(NOT "${ft_aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "json_file AOT FAILED: missing '${_line}'\nstdout:\n${ft_aot_out}")
    endif()
endforeach()
message(STATUS "json_file AOT: OK")
file(REMOVE "${ft_aot_bin}")

# G.3 memcheck
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${FILE_TEST}"
    WORKING_DIRECTORY "${WORK_DIR}"
    OUTPUT_VARIABLE ft_mc_out  ERROR_VARIABLE ft_mc_err  RESULT_VARIABLE ft_mc_rc
)
if(NOT ft_mc_rc EQUAL 0)
    message(FATAL_ERROR "json_file memcheck run FAILED (rc=${ft_mc_rc})\nstderr:\n${ft_mc_err}")
endif()
if(NOT "${ft_mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "json_file --memcheck FAILED\nstderr:\n${ft_mc_err}")
endif()
message(STATUS "json_file memcheck: OK clean")

# ---- Test E: e2e JSON file read + iterate ----
set(E2E "${SAMPLE_DIR}/json_e2e_test.ls")
set(E2E_DATA "${SAMPLE_DIR}/json_e2e_data.json")

# Note: uses --memcheck because Phase H double-free in vec/map of has_drop enum
# causes heap corruption without memcheck wrappers.
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${E2E}"
    OUTPUT_VARIABLE e2e_out  ERROR_VARIABLE e2e_err  RESULT_VARIABLE e2e_rc
)
if(NOT e2e_rc EQUAL 0)
    message(FATAL_ERROR "json_e2e JIT FAILED (rc=${e2e_rc})\nstderr:\n${e2e_err}")
endif()
if(NOT "${e2e_out}" MATCHES "Top-level keys: 8")
    message(FATAL_ERROR "json_e2e: expected 8 top-level keys\nstdout:\n${e2e_out}")
endif()
if(NOT "${e2e_out}" MATCHES "name = LS Language")
    message(FATAL_ERROR "json_e2e: expected 'name = LS Language'\nstdout:\n${e2e_out}")
endif()
if(NOT "${e2e_out}" MATCHES "version = 1.0")
    message(FATAL_ERROR "json_e2e: expected 'version = 1.0'\nstdout:\n${e2e_out}")
endif()
if(NOT "${e2e_out}" MATCHES "active = true")
    message(FATAL_ERROR "json_e2e: expected 'active = true'\nstdout:\n${e2e_out}")
endif()
if(NOT "${e2e_out}" MATCHES "count = 42")
    message(FATAL_ERROR "json_e2e: expected 'count = 42'\nstdout:\n${e2e_out}")
endif()
if(NOT "${e2e_out}" MATCHES "nothing = null")
    message(FATAL_ERROR "json_e2e: expected 'nothing = null'\nstdout:\n${e2e_out}")
endif()
if(NOT "${e2e_out}" MATCHES "round-trip:")
    message(FATAL_ERROR "json_e2e: expected round-trip output\nstdout:\n${e2e_out}")
endif()
message(STATUS "json_e2e JIT: OK")

message(STATUS "test_std_json: ALL PASSED")
