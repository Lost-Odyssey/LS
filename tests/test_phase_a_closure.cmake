# test_phase_a_closure.cmake — Phase A closure-prerequisite glue.
# Validates four scenarios:
#   1. Positive type-alias + Block declaration (parses, type-checks, runs)
#   2. Bare `Block(...)` in fn return position is rejected with the alias hint
#   3. Bare `Block(...)` in struct field is rejected the same way
#   4. Ruby-style closure literals (prefix + trailing) parse, but the checker
#      reports the "Phase B/C inference not implemented" deferral message
#
# Variables (set by CMakeLists.txt):
#   LS_EXE     — path to ls.exe
#   SAMPLE_DIR — directory containing the .ls fixtures
#   WORK_DIR   — scratch directory for AOT outputs

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

# ---- 1) Positive: alias + Block declarations + run ----
set(pos "${SAMPLE_DIR}/type_alias_block_test.ls")
execute_process(
    COMMAND "${LS_EXE}" run "${pos}"
    OUTPUT_VARIABLE pos_out
    ERROR_VARIABLE  pos_err
    RESULT_VARIABLE pos_rc
)
if(NOT "${pos_out}" MATCHES "42" OR
   NOT "${pos_out}" MATCHES "hello" OR
   NOT "${pos_out}" MATCHES "3")
    message(FATAL_ERROR
        "Phase A positive test FAILED — expected 42/hello/3 in output\n"
        "stdout:\n${pos_out}\n"
        "stderr:\n${pos_err}")
endif()
message(STATUS "phase_a positive: OK")

# ---- 2) Negative: Block in return position rejected ----
set(neg1 "${SAMPLE_DIR}/block_return_reject.ls")
execute_process(
    COMMAND "${LS_EXE}" compile "${neg1}" -o "${WORK_DIR}/_should_not_exist.exe"
    OUTPUT_VARIABLE n1_out
    ERROR_VARIABLE  n1_err
    RESULT_VARIABLE n1_rc
)
if(n1_rc EQUAL 0)
    message(FATAL_ERROR
        "Phase A neg1 FAILED — expected non-zero exit for bare Block in "
        "return position; ls.exe accepted it.\nstderr:\n${n1_err}")
endif()
if(NOT "${n1_err}" MATCHES "Block type cannot appear directly")
    message(FATAL_ERROR
        "Phase A neg1 FAILED — missing alias-hint diagnostic.\n"
        "stderr:\n${n1_err}")
endif()
message(STATUS "phase_a return-pos reject: OK")

# ---- 3) Negative: Block in struct field rejected ----
set(neg2 "${SAMPLE_DIR}/block_field_reject.ls")
execute_process(
    COMMAND "${LS_EXE}" compile "${neg2}" -o "${WORK_DIR}/_should_not_exist2.exe"
    OUTPUT_VARIABLE n2_out
    ERROR_VARIABLE  n2_err
    RESULT_VARIABLE n2_rc
)
if(n2_rc EQUAL 0)
    message(FATAL_ERROR
        "Phase A neg2 FAILED — expected non-zero exit for bare Block in "
        "struct field; ls.exe accepted it.\nstderr:\n${n2_err}")
endif()
if(NOT "${n2_err}" MATCHES "Block type cannot appear directly")
    message(FATAL_ERROR
        "Phase A neg2 FAILED — missing alias-hint diagnostic.\n"
        "stderr:\n${n2_err}")
endif()
message(STATUS "phase_a field reject: OK")

# Step 4 — closure literal acceptance — was the Phase A "accept-then-defer"
# check (parser accepts `|x| body`, checker emits a Phase B/C deferral).
# Since Phase B has lifted that deferral by implementing real codegen for
# no-capture closures, the equivalent assertion now lives in
# test_phase_b_closure (compile + run the same shapes end-to-end).

message(STATUS "Phase A closure prerequisite: ALL OK")
