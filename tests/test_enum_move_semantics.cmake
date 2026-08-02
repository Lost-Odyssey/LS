# test_enum_move_semantics.cmake — L-019 (audit OWN-6): has_drop enum
# bindings move like every other has_drop type; @dup keeps an explicit deep
# copy; use-after-move is a checker error. JIT + AOT + reject triple.
#
# @subsystem codegen/ownership
# @guards L-019
# @sources checker_borrow.c:type_is_movable, checker_borrow.c:checker_try_mark_moved, codegen_own.c:cg_invalidate_moved_source
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/enum_move_semantics_test.lls")
set(EXPECT "payload\n7\npayload\ninner\nMOVE PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "enum_move JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
string(REPLACE "\r" "" jit_norm "${jit_out}")
string(STRIP "${jit_norm}" jit_norm)
if(NOT jit_norm STREQUAL EXPECT)
    message(FATAL_ERROR "enum_move JIT wrong output:\n${jit_out}")
endif()
message(STATUS "enum_move JIT: OK")

# ---- memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT "${mc_out}${mc_err}" MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "enum_move memcheck not clean:\n${mc_out}${mc_err}")
endif()
message(STATUS "enum_move memcheck: OK")

# ---- AOT ----
set(EXE "${WORK_DIR}/enum_move_aot.exe")
execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${EXE}"
    OUTPUT_VARIABLE c_out ERROR_VARIABLE c_err RESULT_VARIABLE c_rc)
if(NOT c_rc EQUAL 0)
    message(FATAL_ERROR "enum_move AOT compile FAILED\n${c_err}\n${c_out}")
endif()
execute_process(COMMAND "${EXE}"
    OUTPUT_VARIABLE a_out ERROR_VARIABLE a_err RESULT_VARIABLE a_rc)
string(REPLACE "\r" "" a_norm "${a_out}")
string(STRIP "${a_norm}" a_norm)
if(NOT a_rc EQUAL 0 OR NOT a_norm STREQUAL EXPECT)
    message(FATAL_ERROR "enum_move AOT FAILED (rc=${a_rc}):\n${a_out}")
endif()
message(STATUS "enum_move AOT: OK")

# ---- reject: use after bind-move ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/enum_move_reject.lls"
    OUTPUT_VARIABLE r_out ERROR_VARIABLE r_err RESULT_VARIABLE r_rc)
if(r_rc EQUAL 0)
    message(FATAL_ERROR "enum_move reject: expected move error, got exit 0\n${r_out}")
endif()
if(NOT "${r_err}" MATCHES "use of moved variable 'a'")
    message(FATAL_ERROR "enum_move reject: stderr missing move diagnostic\n${r_err}")
endif()
message(STATUS "enum_move reject: OK")
