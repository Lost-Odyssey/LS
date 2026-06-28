# test_doc.cmake — `ls doc` API-reference generator regression.
#   Verifies: signatures are extracted from source (escaped), preceding
#   doc-comments are attached, `methods` blocks group, the module name is
#   derived, and `_`-prefixed internals are excluded.
cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(F "${CMAKE_CURRENT_LIST_DIR}/samples/doc_fixture.ls")

execute_process(COMMAND "${LS}" doc "${F}"
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc TIMEOUT 30)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "ls doc failed (rc=${rc}):\n${err}")
endif()

# signature extracted + HTML-escaped (-> becomes &gt;)
if(NOT out MATCHES "def add\\(int a, int b\\) -&gt; int")
    message(FATAL_ERROR "missing/incorrect add signature:\n${out}")
endif()
# preceding /// doc attached to the function row (not stolen by the module doc)
if(NOT out MATCHES "class=\"d\">Add two integers")
    message(FATAL_ERROR "add doc-comment not attached to its row:\n${out}")
endif()
# module doc is the //! header line, NOT the first decl's /// comment
if(NOT out MATCHES "class=\"purpose\">doc_fixture")
    message(FATAL_ERROR "module doc should be the //! header:\n${out}")
endif()
# struct rendered
if(NOT out MATCHES "struct Point")
    message(FATAL_ERROR "struct Point missing:\n${out}")
endif()
# methods block grouped + method signature/doc
if(NOT out MATCHES "methods Point")
    message(FATAL_ERROR "methods group missing:\n${out}")
endif()
if(NOT out MATCHES "def manhattan\\(&amp;self\\) -&gt; int" OR NOT out MATCHES "Manhattan distance")
    message(FATAL_ERROR "method signature/doc missing:\n${out}")
endif()
# module name derived
if(NOT out MATCHES "doc_fixture")
    message(FATAL_ERROR "module name missing:\n${out}")
endif()
# internal helper excluded
if(out MATCHES "_scratch")
    message(FATAL_ERROR "internal `_`-prefixed helper leaked into docs:\n${out}")
endif()

message(STATUS "test_doc: ALL PASSED")
