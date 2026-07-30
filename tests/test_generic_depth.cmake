# test_generic_depth.cmake -- generic instantiation depth guard.
# A self-referential template (struct Rec(T) { Rec(Rec(T)) inner }) drove
# checker_instantiate_struct <-> resolve_type_node_with_substitution into
# unbounded mutual recursion: STATUS_STACK_OVERFLOW (0xC00000FD), zero
# diagnostics, zero output. Must now be a clean bounded error.
#
# There is deliberately no positive JIT/AOT/memcheck corpus here: the guard has
# no runtime behaviour to validate -- it only changes the outcome past the limit
# and leaves the normal path untouched. The evidence is instead (a) the crash
# became a diagnostic, (b) legal deep nesting still checks clean, and (c) the IR
# snapshots stay byte-identical.
cmake_minimum_required(VERSION 3.20)

set(NEG "${SAMPLE_DIR}/generic_depth_reject.lls")
execute_process(COMMAND "${LS_EXE}" check "${NEG}"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
set(n_all "${n_out}${n_err}")

if(n_rc EQUAL 0)
    message(FATAL_ERROR "generic_depth_reject: expected a compile error, got success\n${n_all}")
endif()

# A stack overflow shows up as a huge/negative rc with empty output. Insist on a
# real diagnostic, which is the whole point of the guard.
string(FIND "${n_all}" "generic instantiation too deep" _at)
if(_at EQUAL -1)
    message(FATAL_ERROR "generic_depth_reject: no 'generic instantiation too deep' diagnostic -- likely still crashing (rc=${n_rc})\n${n_all}")
endif()
message(STATUS "generic_depth_reject: clean bounded diagnostic (rc=${n_rc})")

# The guard must not fire on ordinary nesting. Vec(Map(Str,Vec(...))) style depth
# is pinned by test_mangle_deep_nest; here just confirm a healthy generic sample
# still checks clean, i.e. the limit is not accidentally tiny.
execute_process(COMMAND "${LS_EXE}" check "${SAMPLE_DIR}/mangle_deep_nest/main.lls"
    OUTPUT_VARIABLE p_out ERROR_VARIABLE p_err RESULT_VARIABLE p_rc)
if(NOT p_rc EQUAL 0)
    message(FATAL_ERROR "mangle_deep_nest regressed: the depth guard fires on legal nesting\n${p_out}${p_err}")
endif()
message(STATUS "mangle_deep_nest: still clean under the depth guard")

message(STATUS "test_generic_depth: ALL PASSED")
