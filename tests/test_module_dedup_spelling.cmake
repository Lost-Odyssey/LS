# test_module_dedup_spelling.cmake — B-5: dedup imports by resolved file, not spelling
#
# Two import spellings resolve to the SAME stdlib file:
#   `import std.text.strconv`  → lib/std/text/strconv.lls (resolve Try 1)
#   `import text.strconv`      → lib/std/text/strconv.lls (resolve Try 2)
# Pre-fix the registry keyed on the spelling, so the module was parsed / checked /
# emitted twice — once under the `std_text_strconv__` prefix and once under
# `text_strconv__`. The fix dedups by the resolved file path and keys the module
# type by the canonical (first-loaded) spelling, so there is a SINGLE emitted copy
# and calls through either spelling reach it.
#
# Assertions: JIT + AOT run correctly; the emitted IR contains the canonical
# `std_text_strconv__int_to_hex` and NOT a duplicate `@text_strconv__` symbol;
# memcheck clean.
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/module_dedup_spelling.lls")
set(_expected "ff" "MODULE_DEDUP PASS")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "module_dedup JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "module_dedup JIT missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "module_dedup JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/module_dedup_spelling_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "module_dedup AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "module_dedup AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "module_dedup AOT missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "module_dedup AOT: OK")
file(REMOVE "${aot_bin}")

# ---- IR dedup: single emitted copy under the canonical prefix ----
# emit-ir streams the module IR; capture both channels and combine.
execute_process(
    COMMAND "${LS_EXE}" emit-ir "${MAIN}"
    OUTPUT_VARIABLE ir_out  ERROR_VARIABLE ir_err  RESULT_VARIABLE ir_rc
)
if(NOT ir_rc EQUAL 0)
    message(FATAL_ERROR "module_dedup emit-ir FAILED (rc=${ir_rc})\n${ir_err}")
endif()
set(ir_all "${ir_out}${ir_err}")
if(NOT "${ir_all}" MATCHES "std_text_strconv__int_to_hex")
    message(FATAL_ERROR "module_dedup: canonical symbol std_text_strconv__int_to_hex missing from IR")
endif()
# The duplicate-spelling prefix `@text_strconv__` must NOT appear (canonical is
# `@std_text_strconv__`, which has `std_` between `@` and `text`, so this anchor
# only matches the bare 2nd-spelling symbol).
if("${ir_all}" MATCHES "@text_strconv__")
    message(FATAL_ERROR "module_dedup: duplicate-spelling symbol @text_strconv__ present — module emitted twice")
endif()
message(STATUS "module_dedup IR: OK (single emitted copy)")

# ---- memcheck ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "module_dedup memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "module_dedup --memcheck FAILED\nstderr:\n${mc_err}")
endif()
message(STATUS "module_dedup memcheck: OK clean")

message(STATUS "test_module_dedup_spelling: ALL PASSED")
