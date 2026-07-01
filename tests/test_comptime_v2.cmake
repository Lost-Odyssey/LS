# test_comptime_v2.cmake — comptime field iteration, v2:
#   ① generic construction T{} / T{...} + writable fields (v.(f) = x, POD + owned)
#   ② enum variants(T) metadata iteration (vr.name / vr.index / vr.type_name)
#   ③ f.type as a type value — static-call receiver f.type.from_value(x)
#      (write-once generic deserialize, no @derive)
#  JIT + AOT + memcheck 0/0/0; negative: variants(non-enum) is a clean error.
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/comptime_v2_test.lls")
set(_expected "COMPTIME V2 DONE")
set(_needles "${_expected}" "doubled=6,10" "copy=alice,42,true" "src=alice"
             "blank=..,0,false" "Circle#0.int. Square#1.int. Empty#2.."
             "deser=8080,true,h")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "comptime v2 JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
foreach(needle ${_needles})
    if(NOT "${jit_out}" MATCHES "${needle}")
        message(FATAL_ERROR "comptime v2 JIT missing '${needle}'\n${jit_out}")
    endif()
endforeach()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "comptime v2 JIT had a FAIL line\n${jit_out}")
endif()
message(STATUS "comptime v2 JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/comptime_v2_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "comptime v2 AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0 OR NOT "${aot_out}" MATCHES "${_expected}"
   OR NOT "${aot_out}" MATCHES "deser=8080,true,h" OR "${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "comptime v2 AOT FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "comptime v2 AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0 OR NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "comptime v2 memcheck not clean\n${mc_err}")
endif()
message(STATUS "comptime v2 memcheck: OK clean")

# ---- negative: variants(non-enum) is a clean compile error ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/comptime_v2_reject.lls"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR "comptime_v2_reject: expected compile error, got success\n${n_out}")
endif()
set(n_all "${n_out}${n_err}")
if(NOT "${n_all}" MATCHES "requires an enum type")
    message(FATAL_ERROR "comptime_v2_reject: missing diagnostic\n${n_all}")
endif()
message(STATUS "comptime_v2_reject: rejected as expected")

message(STATUS "test_comptime_v2: ALL PASSED")
