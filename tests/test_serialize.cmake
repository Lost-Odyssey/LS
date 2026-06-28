# test_serialize.cmake — static reflection Stage 2: @derive(Serialize, Deserialize).
#  * Serialize:   struct -> neutral Value tree -> .to_json() (nested recurses)
#  * Deserialize: from_value(Value) rebuilds the struct; round-trips
#  * JIT + AOT + memcheck; inspect shows to_value/from_value
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/derive_serialize.ls")

execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE j_out ERROR_VARIABLE j_err RESULT_VARIABLE j_rc)
if(NOT j_rc EQUAL 0 OR NOT "${j_out}" MATCHES "DERIVE SERIALIZE DONE")
    message(FATAL_ERROR "derive_serialize JIT FAILED (rc=${j_rc})\n${j_out}\n${j_err}")
endif()
if(NOT "${j_out}" MATCHES "\"host\":\"example.com\"" OR NOT "${j_out}" MATCHES "\"origin\":{\"x\":1,\"y\":2}")
    message(FATAL_ERROR "derive_serialize wrong JSON\n${j_out}")
endif()
if(NOT "${j_out}" MATCHES "ROUNDTRIP PASS")
    message(FATAL_ERROR "derive_serialize round-trip failed\n${j_out}")
endif()
# from_json: JSON text -> Value tree -> struct (full text round-trip) + malformed -> Err
if(NOT "${j_out}" MATCHES "TEXT ROUNDTRIP PASS" OR NOT "${j_out}" MATCHES "MALFORMED OK")
    message(FATAL_ERROR "derive_serialize from_json text round-trip failed\n${j_out}")
endif()
# strict try_from_value: Ok on good input, Err on a missing field
if(NOT "${j_out}" MATCHES "STRICT OK" OR NOT "${j_out}" MATCHES "STRICT MISSING OK")
    message(FATAL_ERROR "derive_serialize strict try_from_value failed\n${j_out}")
endif()
if("${j_out}" MATCHES "FAIL")
    message(FATAL_ERROR "derive_serialize had a FAIL line\n${j_out}")
endif()
message(STATUS "derive_serialize JIT (encode + round-trip + from_json): OK")

set(s_aot "${WORK_DIR}/derive_serialize_aot")
if(WIN32)
    set(s_aot "${s_aot}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${s_aot}"
    RESULT_VARIABLE s_arc ERROR_VARIABLE s_aerr)
if(NOT s_arc EQUAL 0)
    message(FATAL_ERROR "derive_serialize AOT compile FAILED:\n${s_aerr}")
endif()
execute_process(COMMAND "${s_aot}" OUTPUT_VARIABLE s_aout RESULT_VARIABLE s_arrc)
if(NOT s_arrc EQUAL 0 OR NOT "${s_aout}" MATCHES "DERIVE SERIALIZE DONE")
    message(FATAL_ERROR "derive_serialize AOT FAILED\n${s_aout}")
endif()
file(REMOVE "${s_aot}")
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    ERROR_VARIABLE s_mc RESULT_VARIABLE s_mrc)
if(NOT s_mrc EQUAL 0 OR NOT "${s_mc}" MATCHES "OK clean")
    message(FATAL_ERROR "derive_serialize memcheck not clean\n${s_mc}")
endif()
execute_process(COMMAND "${LS_EXE}" inspect Config "${POS}"
    OUTPUT_VARIABLE i_out RESULT_VARIABLE i_rc)
if(NOT i_rc EQUAL 0 OR NOT "${i_out}" MATCHES "def to_value" OR NOT "${i_out}" MATCHES "def from_value")
    message(FATAL_ERROR "inspect Config missing to_value/from_value\n${i_out}")
endif()
message(STATUS "test_serialize: ALL PASSED")
