# test_field_enum_subject.cmake — memcheck-found ownership bug (2026-07-04):
#   `match struct.field` where the field holds a has_drop enum (Option(Str) or a
#   user enum with a Str payload). The AST_FIELD value-read path cloned only
#   has_drop TYPE_STRUCT fields, so a has_drop TYPE_ENUM field was read WITHOUT a
#   clone -> the loaded enum aliased the struct's payload heap. `match` treats a
#   non-IDENT subject as an owned rvalue temp and drops it at merge -> the shared
#   payload was double-freed against the struct's own scope-exit drop.
#   Fixed by cloning the enum field read via emit_enum_clone_val (symmetric with
#   the struct-field clone, codegen_expr.c AST_FIELD read site).
# Covers the three field-subject forms (local struct field / owned parameter field
# / &self method field) x {discard arm, value-yield binder arm} x {Option(Str)
# field, user-enum Str-payload field}. JIT + AOT + memcheck (0 leak / 0 double-free).
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/field_enum_subject_test.lls")
set(_expected "FIELDENUM PASS")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SRC}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "field_enum_subject JIT FAILED (rc=${jit_rc})\n${jit_err}\n${jit_out}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "field_enum_subject JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL:")
    message(FATAL_ERROR "field_enum_subject JIT had a failed check\n${jit_out}")
endif()
message(STATUS "field_enum_subject JIT: OK")

# ---- AOT ----
set(aot_bin "${WORK_DIR}/field_enum_subject_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SRC}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "field_enum_subject AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "field_enum_subject AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "field_enum_subject AOT missing '${_expected}'\n${aot_out}")
endif()
if("${aot_out}" MATCHES "FAIL:")
    message(FATAL_ERROR "field_enum_subject AOT had a failed check\n${aot_out}")
endif()
message(STATUS "field_enum_subject AOT: OK")

# ---- memcheck (0 leak / 0 double-free) ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "field_enum_subject memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "field_enum_subject memcheck leak/double-free\n${mc_err}")
endif()
message(STATUS "field_enum_subject memcheck: OK clean")
