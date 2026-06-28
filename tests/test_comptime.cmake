# test_comptime.cmake — static reflection Stage 3b, step 2: comptime field
# iteration (read-only unroll).
#  * JIT + AOT: `comptime for f in fields(T)` unrolls per field; f.name /
#    f.index / f.type_name become literals, v.(f) becomes a concrete field access.
#    Covers non-generic + generic instantiation (per-T fields) + a Str field.
#  * memcheck: clean (by-value struct clone/drop balanced).
#  * negative: fields(non-struct) is a clean compile error, not a crash.
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/comptime_test.ls")
set(_expected "COMPTIME STEP2 DONE")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "comptime JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
foreach(needle "${_expected}" "x=3 y=7" "3,7" "ann,30,true" "x:int;y:int;"
               "flags=1" "name=other age=num active=flag" "a=.1,2. b=.3,4.")
    if(NOT "${jit_out}" MATCHES "${needle}")
        message(FATAL_ERROR "comptime JIT missing '${needle}'\n${jit_out}")
    endif()
endforeach()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "comptime JIT had a FAIL line\n${jit_out}")
endif()
message(STATUS "comptime JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/comptime_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "comptime AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0 OR NOT "${aot_out}" MATCHES "${_expected}"
   OR NOT "${aot_out}" MATCHES "ann,30,true" OR NOT "${aot_out}" MATCHES "flags=1"
   OR "${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "comptime AOT FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "comptime AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0 OR NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "comptime memcheck not clean\n${mc_err}")
endif()
message(STATUS "comptime memcheck: OK clean")

# ---- negative: fields(non-struct) is a clean compile error ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/comptime_reject.ls"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR "comptime_reject: expected compile error, got success\n${n_out}")
endif()
string(APPEND n_all "${n_out}${n_err}")
if(NOT "${n_all}" MATCHES "requires a struct type")
    message(FATAL_ERROR "comptime_reject: missing diagnostic\n${n_all}")
endif()
if("${n_all}" MATCHES "unreachable")
    message(FATAL_ERROR "comptime_reject: ran past the rejected comptime for\n${n_all}")
endif()
message(STATUS "comptime_reject: rejected as expected")

# ---- negative: a non-constant comptime if condition is a clean compile error ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/comptime_if_reject.ls"
    OUTPUT_VARIABLE n2_out ERROR_VARIABLE n2_err RESULT_VARIABLE n2_rc)
if(n2_rc EQUAL 0)
    message(FATAL_ERROR "comptime_if_reject: expected compile error, got success\n${n2_out}")
endif()
string(APPEND n2_all "${n2_out}${n2_err}")
if(NOT "${n2_all}" MATCHES "compile-time constant")
    message(FATAL_ERROR "comptime_if_reject: missing diagnostic\n${n2_all}")
endif()
if("${n2_all}" MATCHES "unreachable")
    message(FATAL_ERROR "comptime_if_reject: ran past the rejected comptime if\n${n2_all}")
endif()
message(STATUS "comptime_if_reject: rejected as expected")

message(STATUS "test_comptime: ALL PASSED")
