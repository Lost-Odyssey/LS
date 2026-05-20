# test_impl_trait_parse.cmake — impl Trait for Struct parsing tests (JIT + AOT)
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SAMPLE "${SAMPLE_DIR}/impl_trait_parse_test.ls")

set(_expected "10" "20" "99")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE jit_out
    ERROR_VARIABLE  jit_err
    RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "impl_trait_parse JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "impl_trait_parse JIT FAILED: missing '${_line}'\nstdout:\n${jit_out}\nstderr:\n${jit_err}")
    endif()
endforeach()
message(STATUS "test_impl_trait_parse JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/impl_trait_parse_test_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc
    ERROR_VARIABLE  aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "impl_trait_parse AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out
    RESULT_VARIABLE aot_run_rc
    ERROR_VARIABLE  aot_run_err
)
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR
            "impl_trait_parse AOT FAILED: missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "test_impl_trait_parse AOT: OK")

file(REMOVE "${aot_bin}")

# ---- Negative: missing method should error ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/trait_missing_method.ls"
    OUTPUT_VARIABLE miss_out
    ERROR_VARIABLE  miss_err
    RESULT_VARIABLE miss_rc
)
if(miss_rc EQUAL 0)
    message(FATAL_ERROR "trait_missing_method should have failed but succeeded")
endif()
if(NOT "${miss_err}" MATCHES "missing method 'display'")
    message(FATAL_ERROR
        "trait_missing_method: expected 'missing method' error\nstderr:\n${miss_err}")
endif()
message(STATUS "test_impl_trait_parse missing method reject: OK")

# ---- Negative: signature mismatch should error ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/trait_sig_mismatch.ls"
    OUTPUT_VARIABLE sig_out
    ERROR_VARIABLE  sig_err
    RESULT_VARIABLE sig_rc
)
if(sig_rc EQUAL 0)
    message(FATAL_ERROR "trait_sig_mismatch should have failed but succeeded")
endif()
if(NOT "${sig_err}" MATCHES "parameter count mismatch")
    message(FATAL_ERROR
        "trait_sig_mismatch: expected 'parameter count mismatch' error\nstderr:\n${sig_err}")
endif()
message(STATUS "test_impl_trait_parse signature mismatch reject: OK")

# ---- Negative: extra method should error ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/trait_extra_method.ls"
    OUTPUT_VARIABLE ext_out
    ERROR_VARIABLE  ext_err
    RESULT_VARIABLE ext_rc
)
if(ext_rc EQUAL 0)
    message(FATAL_ERROR "trait_extra_method should have failed but succeeded")
endif()
if(NOT "${ext_err}" MATCHES "not declared in trait")
    message(FATAL_ERROR
        "trait_extra_method: expected 'not declared in trait' error\nstderr:\n${ext_err}")
endif()
message(STATUS "test_impl_trait_parse extra method reject: OK")
