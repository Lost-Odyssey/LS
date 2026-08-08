# test_enum_300_variants.cmake — B4: a 300-variant enum locks in the
# drop-sentinel / !range fallback path for variant counts past the i8 tag's
# 256 possible values (existed since 2026-07-05, never exercised by any test
# before this one). Not a bug fix -- a zero-coverage boundary being locked
# down. JIT + AOT + memcheck.
#
# Required: LS_EXE, SAMPLE, WORK_DIR
#
# @subsystem codegen/enum
# @guards B4 >255 variants locks the drop-sentinel / !range fallback
# @sources codegen_own.c:emit_enum_drop
cmake_minimum_required(VERSION 3.20)

foreach(_required_var LS_EXE SAMPLE WORK_DIR)
    if(NOT DEFINED ${_required_var})
        message(FATAL_ERROR "test_enum_300_variants.cmake: missing required -D${_required_var}=...")
    endif()
endforeach()

set(_want "hello\n150\n254\n255\n299\nALL PASS\n")

execute_process(COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE j_out ERROR_VARIABLE j_err RESULT_VARIABLE j_rc)
if(NOT j_rc EQUAL 0)
    message(FATAL_ERROR "enum-300-variants: JIT run FAILED rc=${j_rc}\n${j_err}")
endif()
string(REPLACE "\r\n" "\n" j_out "${j_out}")
if(NOT j_out STREQUAL _want)
    message(FATAL_ERROR "enum-300-variants: wrong JIT values.\n--- want ---\n${_want}\n--- got ---\n${j_out}")
endif()

execute_process(COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE m_out ERROR_VARIABLE m_err RESULT_VARIABLE m_rc)
if(NOT m_rc EQUAL 0)
    message(FATAL_ERROR "enum-300-variants: memcheck run FAILED rc=${m_rc}\n${m_err}")
endif()
if(NOT "${m_out}${m_err}" MATCHES "OK clean")
    message(FATAL_ERROR "enum-300-variants: memcheck not clean\n${m_out}${m_err}")
endif()

set(_exe "${WORK_DIR}/enum_300_variants_aot.exe")
execute_process(COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${_exe}"
    OUTPUT_VARIABLE c_out ERROR_VARIABLE c_err RESULT_VARIABLE c_rc)
if(NOT c_rc EQUAL 0)
    message(FATAL_ERROR "enum-300-variants: AOT compile FAILED rc=${c_rc}\n${c_err}")
endif()
execute_process(COMMAND "${_exe}" OUTPUT_VARIABLE a_out RESULT_VARIABLE a_rc)
if(NOT a_rc EQUAL 0)
    message(FATAL_ERROR "enum-300-variants: AOT binary FAILED rc=${a_rc}")
endif()
string(REPLACE "\r\n" "\n" a_out "${a_out}")
if(NOT a_out STREQUAL _want)
    message(FATAL_ERROR "enum-300-variants: wrong AOT values.\n--- want ---\n${_want}\n--- got ---\n${a_out}")
endif()

message(STATUS "test_enum_300_variants: OK (JIT + AOT values, memcheck clean)")
