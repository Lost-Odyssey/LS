# test_mangle_deep_collision.cmake ¡ª Task 7.2 method-symbol exactness
# (docs/plan_arch_round2_backlog.md Batch 7.2).
#
# Two 70-deep generic chains (Box(...Box(int)...) / Box(...Box(Str)...))
# share a >255-char name prefix. Before mangle_method_symbol the def-side
# method/__drop symbols were built in char[256] buffers: both chains
# truncated to the SAME 255-char symbol (34 such artifacts, drop symbols
# collapsed 147->101, zero symbols longer than 300 bytes) - the second
# chain silently bound the first chain's drop fn. Now every symbol is
# exact. Asserts:
#   1. emit-ir contains BOTH full-length .__drop symbols (red on pre-fix).
#   2. JIT run prints the correct core tags and exits 0.
cmake_minimum_required(VERSION 3.20)
if(NOT LS_EXE OR NOT SAMPLE)
    message(FATAL_ERROR "test_mangle_deep_collision.cmake requires LS_EXE and SAMPLE")
endif()
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(TN "mangle_deep_collision")

execute_process(COMMAND "${LS_EXE}" emit-ir "${SAMPLE}"
    OUTPUT_VARIABLE o1 ERROR_VARIABLE ir RESULT_VARIABLE r1 TIMEOUT 120)
if(NOT r1 EQUAL 0)
    message(FATAL_ERROR "${TN} emit-ir failed (rc=${r1})")
endif()

set(SYM_A [=[Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(int)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))).__drop]=])
set(SYM_B [=[Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(Box(std_core_str_core__Str)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))).__drop]=])
string(FIND "${ir}" "${SYM_A}" pa)
string(FIND "${ir}" "${SYM_B}" pb)
if(pa EQUAL -1)
    message(FATAL_ERROR "${TN}: full-length int-chain .__drop symbol missing (def-site truncation regressed)")
endif()
if(pb EQUAL -1)
    message(FATAL_ERROR "${TN}: full-length Str-chain .__drop symbol missing (def-site truncation regressed)")
endif()

execute_process(COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rr TIMEOUT 240)
if(NOT rr EQUAL 0)
    message(FATAL_ERROR "${TN} JIT run failed (rc=${rr})
${out}
${err}")
endif()
if(NOT out MATCHES "a: a70 b: b70")
    message(FATAL_ERROR "${TN} JIT output wrong:
${out}")
endif()
message(STATUS "${TN} OK")
