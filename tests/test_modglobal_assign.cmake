# test_modglobal_assign.cmake — cross-module writes to a module's global must
# actually land in the global.
#
# Two bugs in the same function family, both silent (exit code 0, no
# diagnostic), both fixed together:
#
#   1. `mod.VAR = v` is an AST_FIELD assignment, so codegen asks
#      codegen_lvalue_ptr for the target address. That function bailed out on
#      any object that is not a struct -- and a MODULE is not -- while the
#      assign path guards on `if (ptr != NULL)` with no else. Every
#      cross-module store was dropped on the floor.
#   2. Calling a &!self mutating method on a module-qualified global
#      (`qmod.GV.push(30)`) resolves the receiver's address through
#      codegen_addr_of, which had the identical gap: its AST_FIELD case also
#      required a TYPE_STRUCT object, so it fell through to the fresh-rvalue
#      spill and the method ran on a private COPY of the global's current
#      value.
#
# Reads across modules always worked, and writes/mutations from INSIDE the
# defining module always worked (pmod.bump() / qmod.bump()), which is why
# neither was noticed until placematrix.py added a module-global place.
#
# Every write is verified through the MODULE's own accessor, not just by
# reading the qualified path back, so a store landing anywhere other than the
# real global still fails. Runs under memcheck (overwriting the owning Str
# global must drop the old value) and through the AOT-compiled binary.
#
# Required: LS_EXE, SAMPLE, WORK_DIR
#
# @subsystem modules
# @guards cross-module writes to a module global stored nothing, silently
# @sources codegen_expr.c:codegen_lvalue_ptr, codegen_expr.c:codegen_addr_of
cmake_minimum_required(VERSION 3.20)

foreach(_required_var LS_EXE SAMPLE WORK_DIR)
    if(NOT DEFINED ${_required_var})
        message(FATAL_ERROR "test_modglobal_assign.cmake: missing required -D${_required_var}=...")
    endif()
endforeach()

set(_want "1\n42\n42\ns1\ns2\ns2\nr1\nr2\nr2\n2\n7\n2\n3\n3\n4\nALL PASS\n")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE j_out ERROR_VARIABLE j_err RESULT_VARIABLE j_rc)
if(NOT j_rc EQUAL 0)
    message(FATAL_ERROR "modglobal-assign: JIT run failed rc=${j_rc}\n${j_err}")
endif()
string(REPLACE "\r\n" "\n" j_out "${j_out}")
if(NOT j_out STREQUAL _want)
    message(FATAL_ERROR
        "modglobal-assign: wrong values from JIT.\n"
        "Values that never change from their initializers mean cross-module "
        "stores/mutations are being dropped again.\n"
        "--- want ---\n${_want}\n--- got ---\n${j_out}")
endif()
message(STATUS "modglobal-assign: JIT values correct")

# ---- memcheck ----
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
message(STATUS "modglobal-assign: memcheck clean")

# ---- AOT ----
set(_exe "${WORK_DIR}/modglobal_assign_aot.exe")
execute_process(COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${_exe}"
    OUTPUT_VARIABLE c_out ERROR_VARIABLE c_err RESULT_VARIABLE c_rc)
if(NOT c_rc EQUAL 0)
    message(FATAL_ERROR "modglobal-assign: AOT compile failed rc=${c_rc}\n${c_err}")
endif()
execute_process(COMMAND "${_exe}" OUTPUT_VARIABLE a_out RESULT_VARIABLE a_rc)
if(NOT a_rc EQUAL 0)
    message(FATAL_ERROR "modglobal-assign: AOT binary failed rc=${a_rc}")
endif()
string(REPLACE "\r\n" "\n" a_out "${a_out}")
if(NOT a_out STREQUAL _want)
    message(FATAL_ERROR "modglobal-assign: wrong values from AOT.\n"
        "--- want ---\n${_want}\n--- got ---\n${a_out}")
endif()

message(STATUS "modglobal-assign: OK (JIT + AOT values, memcheck clean)")
