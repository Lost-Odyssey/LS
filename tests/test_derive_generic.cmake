# test_derive_generic.cmake — @derive on user-defined generic structs (full matrix).
#  * Equal/Hash/Order on Box(T): ==, <, Map(Box(int)) key (JIT + AOT + memcheck)
#  * Show/Serialize/Deserialize on Box(int)/Box(Str)/Box(f64): per-field T lowers to
#    .show()/.to_value()/T.from_value(), round-trips through the value tree
#  * Reflect on Box(T) via alias + index access (name, fields incl. "T" param)
#  * negative: deriving on a generic adds no `where T: Trait` bound, so instantiating
#    with a T that lacks the operation fails at monomorphization (clear error)
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/derive_generic.lls")
set(_exp "GENERIC DERIVE DONE")

execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR NOT "${out}" MATCHES "${_exp}" OR "${out}" MATCHES "FAIL")
    message(FATAL_ERROR "derive_generic JIT FAILED (rc=${rc})\n${out}\n${err}")
endif()
foreach(needle "100" "value: int" "label: Str"
               "Box { value: 5, label: k }" "Box { value: inner, label: outer }"
               "{\"value\":5,\"label\":\"k\"}" "{\"value\":\"inner\",\"label\":\"outer\"}"
               "int roundtrip PASS" "str roundtrip PASS" "f64 roundtrip PASS"
               "i16 roundtrip PASS" "u8 roundtrip PASS")
    if(NOT "${out}" MATCHES "${needle}")
        message(FATAL_ERROR "derive_generic missing '${needle}'\n${out}")
    endif()
endforeach()
message(STATUS "derive_generic JIT: OK")

set(aot "${WORK_DIR}/derive_generic_aot")
if(WIN32)
    set(aot "${aot}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot}"
    RESULT_VARIABLE arc ERROR_VARIABLE aerr)
if(NOT arc EQUAL 0)
    message(FATAL_ERROR "derive_generic AOT compile FAILED:\n${aerr}")
endif()
execute_process(COMMAND "${aot}" OUTPUT_VARIABLE aout RESULT_VARIABLE arrc)
if(NOT arrc EQUAL 0 OR NOT "${aout}" MATCHES "${_exp}" OR "${aout}" MATCHES "FAIL")
    message(FATAL_ERROR "derive_generic AOT FAILED\n${aout}")
endif()
file(REMOVE "${aot}")
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    ERROR_VARIABLE mc RESULT_VARIABLE mrc)
if(NOT mrc EQUAL 0 OR NOT "${mc}" MATCHES "OK clean")
    message(FATAL_ERROR "derive_generic memcheck not clean\n${mc}")
endif()
message(STATUS "derive_generic memcheck: OK clean")

execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/derive_generic_reject.lls"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR "derive_generic_reject: expected compile error\n${n_out}")
endif()
string(APPEND n_all "${n_out}${n_err}")
if(NOT "${n_all}" MATCHES "show")
    message(FATAL_ERROR "derive_generic_reject: missing missing-method diagnostic\n${n_all}")
endif()
message(STATUS "test_derive_generic: ALL PASSED")
