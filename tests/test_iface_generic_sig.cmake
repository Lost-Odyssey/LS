# test_iface_generic_sig.cmake -- interface signature validation.
#
# Section 1 (Task 1): FromList/FromPairs are MARKER protocol interfaces -- their
#   registered signature has param_count 0 on purpose (arity and the element types
#   come from the implementing type's own generics). Comparing a real impl's arity
#   against that placeholder rejected every non-generic FromList impl, making a
#   documented opt-in unusable.
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/iface_marker_ok.lls")
set(_expected "total=60 calls=3")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "iface_marker_ok JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "iface_marker_ok JIT missing '${_expected}'\n${jit_out}")
endif()
message(STATUS "iface_marker_ok JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/iface_marker_ok_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "iface_marker_ok AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "iface_marker_ok AOT run FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "iface_marker_ok AOT missing '${_expected}'\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "iface_marker_ok AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "iface_marker_ok memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "iface_marker_ok memcheck not clean\n${mc_err}")
endif()
message(STATUS "iface_marker_ok memcheck: OK clean")

# Section 2 (Task 3): Phase A -- shape validation at FOLD time for generic
#   trait impls. The generic branch of check_impl_trait_decl used to fold the
#   methods into the inherent impl_node and return early, so none of these
#   five mistakes was reported at all ("Type check passed." rc=0).
set(NEG "${SAMPLE_DIR}/iface_generic_sig_reject.lls")
execute_process(COMMAND "${LS_EXE}" check "${NEG}"
    OUTPUT_VARIABLE s2_out ERROR_VARIABLE s2_err RESULT_VARIABLE s2_rc)
if(s2_rc EQUAL 0)
    message(FATAL_ERROR "iface_generic_sig_reject: expected compile errors, got success\n${s2_out}${s2_err}")
endif()
set(s2_all "${s2_out}${s2_err}")
foreach(pat
        "method 'to_int' parameter count mismatch"
        "does not implement interface 'Two': missing method 'b'"
        "method 'touch' self parameter mismatch"
        "method 'make' static mismatch"
        "method 'bogus' is not declared in interface 'Conv'")
    string(FIND "${s2_all}" "${pat}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "iface_generic_sig_reject: missing diagnostic '${pat}'\n${s2_all}")
    endif()
endforeach()
message(STATUS "iface_generic_sig_reject: all 5 shape diagnostics present (rc=${s2_rc})")

# each mistake must be reported ONCE, not once per instantiation
string(REGEX MATCHALL "method 'to_int' parameter count mismatch" s2_dup "${s2_all}")
list(LENGTH s2_dup s2_dup_n)
if(NOT s2_dup_n EQUAL 1)
    message(FATAL_ERROR "iface_generic_sig_reject: arity diagnostic reported ${s2_dup_n} times, expected exactly 1\n${s2_all}")
endif()
message(STATUS "iface_generic_sig_reject: no duplicate diagnostics")

# ---- positive twin: every legal shape still compiles and runs ----
set(POS2 "${SAMPLE_DIR}/iface_generic_sig_ok.lls")
set(_exp2_a "to_int=5")
set(_exp2_b "sum=425")
set(_exp2_c "len=5")
execute_process(COMMAND "${LS_EXE}" run "${POS2}"
    OUTPUT_VARIABLE p2_out ERROR_VARIABLE p2_err RESULT_VARIABLE p2_rc)
if(NOT p2_rc EQUAL 0)
    message(FATAL_ERROR "iface_generic_sig_ok JIT FAILED (rc=${p2_rc})\n${p2_out}\n${p2_err}")
endif()
foreach(pat "${_exp2_a}" "${_exp2_b}" "${_exp2_c}")
    if(NOT "${p2_out}" MATCHES "${pat}")
        message(FATAL_ERROR "iface_generic_sig_ok JIT missing '${pat}'\n${p2_out}")
    endif()
endforeach()
message(STATUS "iface_generic_sig_ok JIT: OK")

set(aot2 "${WORK_DIR}/iface_generic_sig_ok_aot")
if(WIN32)
    set(aot2 "${aot2}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS2}" -o "${aot2}"
    RESULT_VARIABLE a2_rc ERROR_VARIABLE a2_err)
if(NOT a2_rc EQUAL 0)
    message(FATAL_ERROR "iface_generic_sig_ok AOT compile FAILED:\n${a2_err}")
endif()
execute_process(COMMAND "${aot2}" OUTPUT_VARIABLE a2_out RESULT_VARIABLE a2_run_rc)
if(NOT a2_run_rc EQUAL 0)
    message(FATAL_ERROR "iface_generic_sig_ok AOT run FAILED (rc=${a2_run_rc})\n${a2_out}")
endif()
if(NOT "${a2_out}" MATCHES "${_exp2_b}")
    message(FATAL_ERROR "iface_generic_sig_ok AOT missing '${_exp2_b}'\n${a2_out}")
endif()
file(REMOVE "${aot2}")
message(STATUS "iface_generic_sig_ok AOT: OK")

execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS2}"
    OUTPUT_VARIABLE m2_out ERROR_VARIABLE m2_err RESULT_VARIABLE m2_rc)
if(NOT m2_rc EQUAL 0)
    message(FATAL_ERROR "iface_generic_sig_ok memcheck FAILED (rc=${m2_rc})\n${m2_err}")
endif()
if(NOT "${m2_err}" MATCHES "OK clean")
    message(FATAL_ERROR "iface_generic_sig_ok memcheck not clean\n${m2_err}")
endif()
message(STATUS "iface_generic_sig_ok memcheck: OK clean")

# Section 3 (Task 4): Phase B -- param/return TYPE validation at
#   monomorphization. These shapes are all legal; only the types are wrong.
#   The Clone case used to reach codegen and emit invalid IR, caught only by
#   the LLVM module verifier with no source location.
set(NEG3 "${SAMPLE_DIR}/iface_generic_type_reject.lls")
execute_process(COMMAND "${LS_EXE}" run "${NEG3}"
    OUTPUT_VARIABLE s3_out ERROR_VARIABLE s3_err RESULT_VARIABLE s3_rc)
if(s3_rc EQUAL 0)
    message(FATAL_ERROR "iface_generic_type_reject: expected compile errors, got success\n${s3_out}${s3_err}")
endif()
set(s3_all "${s3_out}${s3_err}")
foreach(pat
        "method 'to_int' return type mismatch in interface 'Conv'"
        "method 'eat' parameter 1 type mismatch in interface 'Eat'"
        "method '__clone' return type mismatch in interface 'Clone'")
    string(FIND "${s3_all}" "${pat}" _at3)
    if(_at3 EQUAL -1)
        message(FATAL_ERROR "iface_generic_type_reject: missing diagnostic '${pat}'\n${s3_all}")
    endif()
endforeach()
message(STATUS "iface_generic_type_reject: all 3 type diagnostics present (rc=${s3_rc})")

# the whole point: a clean type error, NOT an LLVM verifier complaint
foreach(bad
        "module verification failed"
        "Call parameter type does not match function signature")
    string(FIND "${s3_all}" "${bad}" _bad3)
    if(NOT _bad3 EQUAL -1)
        message(FATAL_ERROR "iface_generic_type_reject: reached codegen and produced invalid IR ('${bad}') instead of a checker diagnostic\n${s3_all}")
    endif()
endforeach()
message(STATUS "iface_generic_type_reject: rejected in the checker, no invalid IR")

# reported once per template method, not once per instantiation
string(REGEX MATCHALL "method 'to_int' return type mismatch" s3_dup "${s3_all}")
list(LENGTH s3_dup s3_dup_n)
if(NOT s3_dup_n EQUAL 1)
    message(FATAL_ERROR "iface_generic_type_reject: type diagnostic reported ${s3_dup_n} times, expected exactly 1\n${s3_all}")
endif()
message(STATUS "iface_generic_type_reject: no duplicate diagnostics")

message(STATUS "test_iface_generic_sig: ALL PASSED")
