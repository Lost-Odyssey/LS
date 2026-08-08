# test_bf046_map_struct_val.cmake — BF-046: map.set with a TEMPORARY has_drop
# struct/enum value (inline rvalue) leaked the temp's owned fields. Fix registers a
# statement-end drop. Named values must not double-drop. JIT + AOT + memcheck.
#
# `m.set(k, N{...})` with a temporary has_drop struct or enum value leaked 16
# bytes per element.
#
# `map.set` has deep-copy semantics: it clones the value into its node. The bug is
# what happens to the original. The set path neither moved from the temporary nor
# dropped it, so the temporary kept owning its Str field and nobody ever released
# it. The asymmetry is what made it survive: a NAMED value (`N v; m.set(k, v)`) is
# clean because scope exit drops `v`, and POD values are clean because there is
# nothing to release -- only the anonymous has_drop rvalue leaks.
#
# Fixed by spilling such an argument into an entry alloca and registering it for
# end-of-statement drop; the map holds an independent clone, so there is no double
# free. A latent second defect was fixed alongside: when the drop function already
# existed, the early return skipped setting `drop_fn` on that Type object, leaving
# a second Type instance with a NULL drop_fn.
#
# @subsystem stdlib/containers
# @guards BF-046 map.set temp has_drop struct/enum value drop
# @sources codegen_own.c:cg_store_owned
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/bf046_map_struct_val/main.lls")
set(_expected "a=V1 b=V2" "e=PAYLOAD" "BF046 PASS")

# ---- JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "bf046 JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${jit_out}" MATCHES "${_line}")
        message(FATAL_ERROR "bf046 JIT missing '${_line}'\nstdout:\n${jit_out}")
    endif()
endforeach()
message(STATUS "bf046 JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/bf046_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "bf046 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "bf046 AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
foreach(_line ${_expected})
    if(NOT "${aot_out}" MATCHES "${_line}")
        message(FATAL_ERROR "bf046 AOT missing '${_line}'\nstdout:\n${aot_out}")
    endif()
endforeach()
message(STATUS "bf046 AOT: OK")
file(REMOVE "${aot_bin}")

# ---- memcheck (the point of BF-046) ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "bf046 memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "bf046 --memcheck FAILED (leak)\nstderr:\n${mc_err}")
endif()
message(STATUS "bf046 memcheck: OK clean")

message(STATUS "test_bf046_map_struct_val: ALL PASSED")
