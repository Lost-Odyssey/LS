# test_irasm.cmake — `ls ir` / `ls asm` per-function codegen inspection.
#  * ir  <fn>: prints that function's LLVM IR (define ... @fn)
#  * asm <fn>: prints that function's assembly (label + body, bounded)
#  * fuzzy name match: 'area' resolves to 'Point.area'
#  * negative: unknown function -> nonzero exit
# Output is platform/LLVM-version dependent; assert only stable anchors.
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/irasm_demo.lls")

# ---- ir square ----
execute_process(COMMAND "${LS_EXE}" ir square "${SRC}"
    OUTPUT_VARIABLE ir_out ERROR_VARIABLE ir_err RESULT_VARIABLE ir_rc)
if(NOT ir_rc EQUAL 0)
    message(FATAL_ERROR "ir square FAILED (rc=${ir_rc})\n${ir_out}\n${ir_err}")
endif()
foreach(needle "define" "square" "mul")
    if(NOT "${ir_out}" MATCHES "${needle}")
        message(FATAL_ERROR "ir square missing '${needle}'\n${ir_out}")
    endif()
endforeach()
message(STATUS "ir square: OK")

# ---- asm square ----
execute_process(COMMAND "${LS_EXE}" asm square "${SRC}"
    OUTPUT_VARIABLE asm_out ERROR_VARIABLE asm_err RESULT_VARIABLE asm_rc)
if(NOT asm_rc EQUAL 0)
    message(FATAL_ERROR "asm square FAILED (rc=${asm_rc})\n${asm_out}\n${asm_err}")
endif()
if(NOT "${asm_out}" MATCHES "square:" OR NOT "${asm_out}" MATCHES "ret")
    message(FATAL_ERROR "asm square missing label/ret\n${asm_out}")
endif()
# The slice must be bounded — main's body must NOT leak into square's asm.
if("${asm_out}" MATCHES "__ls_set_args")
    message(FATAL_ERROR "asm square leaked into another function\n${asm_out}")
endif()
message(STATUS "asm square: OK (bounded)")

# ---- fuzzy match: 'area' -> 'Point.area' ----
execute_process(COMMAND "${LS_EXE}" asm area "${SRC}"
    OUTPUT_VARIABLE fz_out ERROR_VARIABLE fz_err RESULT_VARIABLE fz_rc)
if(NOT fz_rc EQUAL 0)
    message(FATAL_ERROR "asm area FAILED (rc=${fz_rc})\n${fz_out}\n${fz_err}")
endif()
if(NOT "${fz_out}" MATCHES "Point.area:")
    message(FATAL_ERROR "asm area did not resolve to Point.area\n${fz_out}\n${fz_err}")
endif()
message(STATUS "asm area (fuzzy): OK")

# ---- negative: unknown function ----
execute_process(COMMAND "${LS_EXE}" ir nope "${SRC}"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR "ir nope: expected nonzero exit\n${n_out}")
endif()
message(STATUS "ir unknown-fn: rejected as expected")

message(STATUS "test_irasm: ALL PASSED")
