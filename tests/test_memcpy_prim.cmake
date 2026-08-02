# test_memcpy_prim.cmake — __ls_bytecopy → llvm.memcpy primitive (form ③).
# Pins: ① default emit-ir lowers every std.sys.c.__ls_bytecopy call to
# @llvm.memcpy (zero extern calls remain); ② LS_NO_MEMCPY_PRIM=1 fully falls
# back to the extern call (zero memcpy intrinsics from the prim); ③ runtime
# output identical in both modes; ④ memcheck clean.
#
# @subsystem codegen/optimization
# @guards __ls_bytecopy lowered to llvm.memcpy (form 3)
# @sources codegen_call.c:cg_expr_call
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/memcpy_prim_test.lls")
set(EXPECT "hello world\n11\nababab\nPASS")

# ---- ① default: lowered to llvm.memcpy, no extern bytecopy calls ----
execute_process(COMMAND "${LS_EXE}" emit-ir "${SRC}"
    OUTPUT_VARIABLE ir_out ERROR_VARIABLE ir_err RESULT_VARIABLE ir_rc)
set(ir_all "${ir_out}${ir_err}")
if(NOT ir_rc EQUAL 0)
    message(FATAL_ERROR "memcpy_prim emit-ir FAILED (rc=${ir_rc})")
endif()
if(NOT "${ir_all}" MATCHES "llvm\\.memcpy")
    message(FATAL_ERROR "memcpy_prim: no llvm.memcpy in default IR")
endif()
if("${ir_all}" MATCHES "call[^\n]*@__ls_bytecopy")
    message(FATAL_ERROR "memcpy_prim: extern __ls_bytecopy call survived in default IR")
endif()
message(STATUS "memcpy_prim IR (default): OK")

# ---- ② switch: full fallback to the extern call ----
execute_process(COMMAND ${CMAKE_COMMAND} -E env LS_NO_MEMCPY_PRIM=1
    "${LS_EXE}" emit-ir "${SRC}"
    OUTPUT_VARIABLE sw_out ERROR_VARIABLE sw_err RESULT_VARIABLE sw_rc)
set(sw_all "${sw_out}${sw_err}")
if(NOT "${sw_all}" MATCHES "call[^\n]*@__ls_bytecopy")
    message(FATAL_ERROR "memcpy_prim: switch did not restore extern calls")
endif()
message(STATUS "memcpy_prim IR (switch): OK")

# ---- ③ runtime parity, both modes ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE j1 ERROR_VARIABLE e1 RESULT_VARIABLE r1)
string(REPLACE "\r" "" j1n "${j1}")
string(STRIP "${j1n}" j1n)
execute_process(COMMAND ${CMAKE_COMMAND} -E env LS_NO_MEMCPY_PRIM=1
    "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE j2 ERROR_VARIABLE e2 RESULT_VARIABLE r2)
string(REPLACE "\r" "" j2n "${j2}")
string(STRIP "${j2n}" j2n)
if(NOT r1 EQUAL 0 OR NOT j1n STREQUAL EXPECT)
    message(FATAL_ERROR "memcpy_prim JIT (default) wrong (rc=${r1}):\n${j1}")
endif()
if(NOT r2 EQUAL 0 OR NOT j2n STREQUAL j1n)
    message(FATAL_ERROR "memcpy_prim JIT parity broke (rc=${r2}):\n${j2}")
endif()
message(STATUS "memcpy_prim runtime parity: OK")

# ---- ④ memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT "${mc_out}${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "memcpy_prim memcheck not clean:\n${mc_out}${mc_err}")
endif()
message(STATUS "memcpy_prim memcheck: OK")
