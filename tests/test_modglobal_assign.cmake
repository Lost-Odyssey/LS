# test_modglobal_assign.cmake — cross-module assignment to a module's global
# must actually store.
#
# `mod.VAR = v` is an AST_FIELD assignment, so codegen asks codegen_lvalue_ptr
# for the target address. That function bailed out on any object that is not a
# struct -- and a MODULE is not -- while the assign path guards on
# `if (ptr != NULL)` with no else. Every cross-module store was therefore
# dropped on the floor: exit code 0, no diagnostic, reads still returning the
# old value. Reads across modules always worked, and writes from inside the
# defining module always worked, which is why this went unnoticed.
#
# Same shape as the array-place family: a place reader that only understood
# identifiers, and a caller that treated "no address" as "nothing to do".
#
# Every write is verified through the MODULE's own accessor, so a store that
# landed anywhere other than the real global still fails. The Str case also runs
# under memcheck: overwriting an owning global must drop the old value.
#
# Required: LS_EXE, SAMPLE
cmake_minimum_required(VERSION 3.20)

foreach(_required_var LS_EXE SAMPLE)
    if(NOT DEFINED ${_required_var})
        message(FATAL_ERROR "test_modglobal_assign.cmake: missing required -D${_required_var}=...")
    endif()
endforeach()

set(_want "1\n42\n42\ns1\ns2\ns2\nr1\nr2\nr2\n2\n7\nALL PASS\n")

execute_process(COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE j_out ERROR_VARIABLE j_err RESULT_VARIABLE j_rc)
if(NOT j_rc EQUAL 0)
    message(FATAL_ERROR "modglobal-assign: run failed rc=${j_rc}\n${j_err}")
endif()
string(REPLACE "\r\n" "\n" j_out "${j_out}")
if(NOT j_out STREQUAL _want)
    message(FATAL_ERROR
        "modglobal-assign: wrong values.\n"
        "Values that never change from their initializers mean cross-module "
        "stores are being dropped again.\n"
        "--- want ---\n${_want}\n--- got ---\n${j_out}")
endif()
message(STATUS "modglobal-assign: values correct")

execute_process(COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE m_out ERROR_VARIABLE m_err RESULT_VARIABLE m_rc)
if(NOT m_rc EQUAL 0)
    message(FATAL_ERROR "modglobal-assign: memcheck run failed rc=${m_rc}\n${m_err}")
endif()
if(NOT "${m_out}${m_err}" MATCHES "OK clean")
    message(FATAL_ERROR
        "modglobal-assign: memcheck not clean -- overwriting an owning global "
        "must drop the old value\n${m_out}${m_err}")
endif()

message(STATUS "modglobal-assign: OK (values + memcheck clean)")
