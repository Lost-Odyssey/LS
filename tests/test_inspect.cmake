# test_inspect.cmake — `ls inspect` static reflection (Stage 1.5).
#  * struct: fields + instance/static methods printed with signatures + tags
#  * enum:   variants with payload types
#  * negative: unknown type -> nonzero exit
# Needles avoid regex metachars ( ) [ ] so they match literally.
cmake_minimum_required(VERSION 3.20)

set(SRC "${SAMPLE_DIR}/inspect_demo.lls")

# ---- struct Point ----
execute_process(COMMAND "${LS_EXE}" inspect Point "${SRC}"
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "inspect Point FAILED (rc=${rc})\n${out}\n${err}")
endif()
foreach(needle "struct Point" "x : int" "y : int"
               "def area" "&self" "def translate" "&!self"
               "def origin" "static" "Point")
    if(NOT "${out}" MATCHES "${needle}")
        message(FATAL_ERROR "inspect Point missing '${needle}'\n${out}")
    endif()
endforeach()
message(STATUS "inspect Point: OK")

# ---- enum Shape ----
execute_process(COMMAND "${LS_EXE}" inspect Shape "${SRC}"
    OUTPUT_VARIABLE out2 ERROR_VARIABLE err2 RESULT_VARIABLE rc2)
if(NOT rc2 EQUAL 0)
    message(FATAL_ERROR "inspect Shape FAILED (rc=${rc2})\n${out2}\n${err2}")
endif()
foreach(needle "enum Shape" "Circle" "Rect" "f64")
    if(NOT "${out2}" MATCHES "${needle}")
        message(FATAL_ERROR "inspect Shape missing '${needle}'\n${out2}")
    endif()
endforeach()
message(STATUS "inspect Shape: OK")

# ---- negative: unknown type ----
execute_process(COMMAND "${LS_EXE}" inspect Nope "${SRC}"
    OUTPUT_VARIABLE out3 ERROR_VARIABLE err3 RESULT_VARIABLE rc3)
if(rc3 EQUAL 0)
    message(FATAL_ERROR "inspect Nope: expected nonzero exit, got success\n${out3}")
endif()
message(STATUS "inspect unknown-type: rejected as expected (rc=${rc3})")

message(STATUS "test_inspect: ALL PASSED")
