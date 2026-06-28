# test_enum_print.cmake — print(enum) readable rendering (Variant / Variant(…),
# Option/Result, enum fields in structs) + owned enum rvalue not leaked.
# JIT + AOT (exact-line asserts) + memcheck (0 leak / 0 double-free).
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SRC "${SAMPLE_DIR}/enum_print_test.ls")

# Substrings that must appear (exact rendered forms).
set(_expect
    "Circle(5)"
    "Rect(3, 4)"
    "Named(\"hi\")"
    "Some(\"s7\")"
    "None"
    "Err(\"bad\")"
    "Wrap{sh=Rect(1, 2), c=Blue}"
    "Some(\"s8\")"
    "ENUMPRINT PASS")

function(_assert_output out where)
    # Literal substring matching (parens in `Circle(5)` are not regex here).
    foreach(line IN LISTS _expect)
        string(FIND "${out}" "${line}" _pos)
        if(_pos EQUAL -1)
            message(FATAL_ERROR "enum_print ${where}: missing '${line}'\n${out}")
        endif()
    endforeach()
    # The old raw-bytes rendering must be gone.
    string(FIND "${out}" "0000000000000001" _raw)
    if(NOT _raw EQUAL -1)
        message(FATAL_ERROR "enum_print ${where}: raw discriminant bytes leaked into output\n${out}")
    endif()
endfunction()

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "enum_print JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
_assert_output("${jit_out}" "JIT")
message(STATUS "enum_print JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/enum_print_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "enum_print AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "enum_print AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
_assert_output("${aot_out}" "AOT")
message(STATUS "enum_print AOT: OK")

# ---- memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "enum_print memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "enum_print memcheck leak/double-free\n${mc_err}")
endif()
message(STATUS "enum_print memcheck: OK clean")
