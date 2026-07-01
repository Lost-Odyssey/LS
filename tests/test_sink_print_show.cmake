# test_sink_print_show.cmake — Stage C-2: print(x) honors Show. A Show struct
# prints via Show (`Name { f: v }`); a plain struct stays structural (`Name{f=v}`);
# POD/Str keep the fast path. Verifies exact output + memcheck clean (the to_str
# rewrite's owned Str rvalue is dropped). docs/plan_print_sink.md Stage C.
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

set(SRC "${SAMPLE_DIR}/sink_print_show_test.lls")

execute_process(COMMAND "${LS_EXE}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "sink_print_show FAILED (rc=${rc})\n${err}\n${out}")
endif()
# Show struct: colon + spaces (the rewritten to_str path).
if(NOT "${out}" MATCHES "P { x: 3, y: 4 }")
    message(FATAL_ERROR "print(Show struct) did not use Show format\n${out}")
endif()
# Plain struct: structural '=' form, Str field quoted.
if(NOT "${out}" MATCHES "Q{a=7, tag=\"hi\"}")
    message(FATAL_ERROR "print(plain struct) did not use structural format\n${out}")
endif()
if(NOT "${out}" MATCHES "B\\(9\\)")
    message(FATAL_ERROR "print(Show enum) wrong\n${out}")
endif()
if(NOT "${out}" MATCHES "42 hi true")
    message(FATAL_ERROR "print(POD/Str) fast path wrong\n${out}")
endif()
if(NOT "${out}" MATCHES "SHOW PRINT DONE")
    message(FATAL_ERROR "sink_print_show did not complete\n${out}")
endif()
if(NOT "${err}" MATCHES "OK clean")
    message(FATAL_ERROR "sink_print_show memcheck leak/double-free\n${err}")
endif()
message(STATUS "sink_print_show: Show/structural dispatch OK + memcheck clean")
