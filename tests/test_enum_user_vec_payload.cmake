# test_enum_user_vec_payload.cmake -- enum payload holding user Vec(T)
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/enum_user_vec_payload_test.lls")
set(_expected
    "user numbers: len=3"
    "user mixed: test len=2"
    "user empty"
    "user vec enum done")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "enum_user_vec_payload JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR
            "enum_user_vec_payload JIT FAILED: missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "test_enum_user_vec_payload JIT: OK")

# ---- JIT memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE mc_out
    ERROR_VARIABLE  mc_err
    RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "enum_user_vec_payload JIT memcheck FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "0 leak")
    message(FATAL_ERROR "enum_user_vec_payload JIT memcheck: expected 0 leaks\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "0 double-free")
    message(FATAL_ERROR "enum_user_vec_payload JIT memcheck: expected 0 double-free\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "0 invalid free")
    message(FATAL_ERROR "enum_user_vec_payload JIT memcheck: expected 0 invalid free\nstderr:\n${mc_err}")
endif()
message(STATUS "test_enum_user_vec_payload JIT memcheck: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/enum_user_vec_payload_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "enum_user_vec_payload AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
    RESULT_VARIABLE aot_run_rc
    ERROR_VARIABLE  aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "enum_user_vec_payload AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR
            "enum_user_vec_payload AOT FAILED: missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "test_enum_user_vec_payload AOT: OK")

file(REMOVE "${aot_bin}")
