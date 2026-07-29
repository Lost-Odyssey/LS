# test_closure_tail_expr.cmake — a closure's braced body yields its tail
# expression, exactly like a function's does.
#
# codegen gives functions that behaviour by intercepting the last statement of
# the body block (codegen_fn_decl, "handle implicit return of last expression").
# The closure path had no such interception: it emitted the whole block as
# statements and then fell through to `ret zeroinitializer`. So `|| { 7 }`
# returned 0 and `|| { "hello" }` returned an empty Str -- exit code 0, no
# diagnostic, wrong value. Seven of the eleven values this sample prints were
# wrong, including the one inside a generic method body.
#
# The fix rewrites the tail into a real `return` in the checker, so this also
# pins that the ownership paths still work: the sample moves an owned, heap-built
# Str out through a closure tail, which is where a fix that duplicated codegen's
# interception next to the closure's different scope layout would double-free.
#
# Required: LS_EXE, SAMPLE, WORK_DIR
cmake_minimum_required(VERSION 3.20)

foreach(_required_var LS_EXE SAMPLE WORK_DIR)
    if(NOT DEFINED ${_required_var})
        message(FATAL_ERROR "test_closure_tail_expr.cmake: missing required -D${_required_var}=...")
    endif()
endforeach()

set(_want "7\n11\n21\n7\n7\n11\n5\nhello\nabcd\n1\n15\nALL PASS\n")

# ---- JIT ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE j_out ERROR_VARIABLE j_err RESULT_VARIABLE j_rc)
if(NOT j_rc EQUAL 0)
    message(FATAL_ERROR "closure-tail-expr: JIT run failed rc=${j_rc}\n${j_err}")
endif()
string(REPLACE "\r\n" "\n" j_out "${j_out}")
if(NOT j_out STREQUAL _want)
    message(FATAL_ERROR
        "closure-tail-expr: wrong values from JIT.\n"
        "A run of zeroes and blanks means the closure body's tail expression is "
        "being discarded and the closure is returning zeroinitializer again.\n"
        "--- want ---\n${_want}\n--- got ---\n${j_out}")
endif()
message(STATUS "closure-tail-expr: JIT values correct")

# ---- memcheck: an owned Str leaves the closure through the block tail ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE m_out ERROR_VARIABLE m_err RESULT_VARIABLE m_rc)
if(NOT m_rc EQUAL 0)
    message(FATAL_ERROR "closure-tail-expr: memcheck run failed rc=${m_rc}\n${m_err}")
endif()
if(NOT "${m_out}${m_err}" MATCHES "OK clean")
    message(FATAL_ERROR "closure-tail-expr: memcheck not clean\n${m_out}${m_err}")
endif()
message(STATUS "closure-tail-expr: memcheck clean")

# ---- AOT: the same values through the compiled binary ----
set(_exe "${WORK_DIR}/closure_tail_expr_aot.exe")
execute_process(COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${_exe}"
    OUTPUT_VARIABLE c_out ERROR_VARIABLE c_err RESULT_VARIABLE c_rc)
if(NOT c_rc EQUAL 0)
    message(FATAL_ERROR "closure-tail-expr: AOT compile failed rc=${c_rc}\n${c_err}")
endif()
execute_process(COMMAND "${_exe}" OUTPUT_VARIABLE a_out RESULT_VARIABLE a_rc)
if(NOT a_rc EQUAL 0)
    message(FATAL_ERROR "closure-tail-expr: AOT binary failed rc=${a_rc}")
endif()
string(REPLACE "\r\n" "\n" a_out "${a_out}")
if(NOT a_out STREQUAL _want)
    message(FATAL_ERROR "closure-tail-expr: wrong values from AOT.\n"
        "--- want ---\n${_want}\n--- got ---\n${a_out}")
endif()

message(STATUS "closure-tail-expr: OK (JIT + AOT values, memcheck clean)")
