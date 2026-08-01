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
        "method '__clone' return type mismatch in interface 'Clone'"
        "method 'gated' return type mismatch in interface 'Gated'")
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


# Section 4 (fold-time concrete types): a generic trait impl whose signature is
#   entirely scalar needs no bound T, so it is compared at the DEFINITION -- even
#   when nothing in the program ever instantiates the type. Before this, these
#   passed silently and would only surface the day someone first used the type.
set(NEG4 "${SAMPLE_DIR}/iface_generic_fold_type_reject.lls")
execute_process(COMMAND "${LS_EXE}" check "${NEG4}"
    OUTPUT_VARIABLE s4_out ERROR_VARIABLE s4_err RESULT_VARIABLE s4_rc)
if(s4_rc EQUAL 0)
    message(FATAL_ERROR "iface_generic_fold_type_reject: expected compile errors, got success
${s4_out}${s4_err}")
endif()
set(s4_all "${s4_out}${s4_err}")
foreach(pat
        "method 'to_int' return type mismatch in interface 'Conv'"
        "method 'eat' parameter 1 type mismatch in interface 'Eat'")
    string(FIND "${s4_all}" "${pat}" _at4)
    if(_at4 EQUAL -1)
        message(FATAL_ERROR "iface_generic_fold_type_reject: missing diagnostic '${pat}'
${s4_all}")
    endif()
endforeach()
# Nothing here is instantiated, so these can only have come from the fold-time
# check -- that is the whole point of this section.
string(FIND "${s4_all}" "Wrap(" _inst4)
if(NOT _inst4 EQUAL -1)
    message(FATAL_ERROR "iface_generic_fold_type_reject: diagnostic names a concrete instance, so the corpus is no longer instantiation-free
${s4_all}")
endif()
message(STATUS "iface_generic_fold_type_reject: caught at the definition, no instantiation (rc=${s4_rc})")

# Positive twin: signatures mentioning Self or a type parameter must NOT be
# compared at fold time (T is unbound, Self has no concrete type) -- a false
# positive here would reject correct code.
set(POS4 "${SAMPLE_DIR}/iface_generic_fold_type_ok.lls")
execute_process(COMMAND "${LS_EXE}" run "${POS4}"
    OUTPUT_VARIABLE p4_out ERROR_VARIABLE p4_err RESULT_VARIABLE p4_rc)
if(NOT p4_rc EQUAL 0)
    message(FATAL_ERROR "iface_generic_fold_type_ok JIT FAILED (rc=${p4_rc})
${p4_out}
${p4_err}")
endif()
foreach(pat "to_int=7" "len=5")
    if(NOT "${p4_out}" MATCHES "${pat}")
        message(FATAL_ERROR "iface_generic_fold_type_ok JIT missing '${pat}'
${p4_out}")
    endif()
endforeach()
message(STATUS "iface_generic_fold_type_ok JIT: OK")

set(aot4 "${WORK_DIR}/iface_generic_fold_type_ok_aot")
if(WIN32)
    set(aot4 "${aot4}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS4}" -o "${aot4}"
    RESULT_VARIABLE a4_rc ERROR_VARIABLE a4_err)
if(NOT a4_rc EQUAL 0)
    message(FATAL_ERROR "iface_generic_fold_type_ok AOT compile FAILED:
${a4_err}")
endif()
execute_process(COMMAND "${aot4}" OUTPUT_VARIABLE a4_out RESULT_VARIABLE a4_run_rc)
if(NOT a4_run_rc EQUAL 0)
    message(FATAL_ERROR "iface_generic_fold_type_ok AOT run FAILED (rc=${a4_run_rc})
${a4_out}")
endif()
if(NOT "${a4_out}" MATCHES "len=5")
    message(FATAL_ERROR "iface_generic_fold_type_ok AOT missing 'len=5'
${a4_out}")
endif()
file(REMOVE "${aot4}")
message(STATUS "iface_generic_fold_type_ok AOT: OK")

execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS4}"
    OUTPUT_VARIABLE m4_out ERROR_VARIABLE m4_err RESULT_VARIABLE m4_rc)
if(NOT m4_rc EQUAL 0)
    message(FATAL_ERROR "iface_generic_fold_type_ok memcheck FAILED (rc=${m4_rc})
${m4_err}")
endif()
if(NOT "${m4_err}" MATCHES "OK clean")
    message(FATAL_ERROR "iface_generic_fold_type_ok memcheck not clean
${m4_err}")
endif()
message(STATUS "iface_generic_fold_type_ok memcheck: OK clean")


# Section 5 (L-024 residuals): two gaps that were reachable on BOTH the generic
#   and the non-generic path, so both are fixed in the shared shape leaf and all
#   four sites must report in one run.
#   (A) marker protocol arity -- fixed by the protocol, not declared by the
#       interface; getting it wrong reached codegen as "Incorrect number of
#       arguments passed to called function!".
#   (B) an impl method declaring its own type parameters, which no interface can
#       declare; calling it through the interface failed with "cannot call
#       non-function type 'void'".
set(NEG5 "${SAMPLE_DIR}/iface_residual_reject.lls")
execute_process(COMMAND "${LS_EXE}" check "${NEG5}"
    OUTPUT_VARIABLE s5_out ERROR_VARIABLE s5_err RESULT_VARIABLE s5_rc)
if(s5_rc EQUAL 0)
    message(FATAL_ERROR "iface_residual_reject: expected compile errors, got success
${s5_out}${s5_err}")
endif()
set(s5_all "${s5_out}${s5_err}")
string(REGEX MATCHALL "interface 'FromList' requires 1, got 2" s5_arity "${s5_all}")
list(LENGTH s5_arity s5_arity_n)
if(NOT s5_arity_n EQUAL 2)
    message(FATAL_ERROR "iface_residual_reject: expected the marker arity diagnostic at BOTH the non-generic and generic site, got ${s5_arity_n}
${s5_all}")
endif()
string(REGEX MATCHALL "cannot declare its own type parameters" s5_tp "${s5_all}")
list(LENGTH s5_tp s5_tp_n)
if(NOT s5_tp_n EQUAL 2)
    message(FATAL_ERROR "iface_residual_reject: expected the type-parameter diagnostic at BOTH the non-generic and generic site, got ${s5_tp_n}
${s5_all}")
endif()
# Neither may leak past the checker into the backend.
foreach(bad
        "Incorrect number of arguments"
        "cannot call non-function type"
        "module verification failed")
    string(FIND "${s5_all}" "${bad}" _bad5)
    if(NOT _bad5 EQUAL -1)
        message(FATAL_ERROR "iface_residual_reject: leaked past the checker ('${bad}')
${s5_all}")
    endif()
endforeach()
message(STATUS "iface_residual_reject: both gaps rejected on both paths (rc=${s5_rc})")

message(STATUS "test_iface_generic_sig: ALL PASSED")
