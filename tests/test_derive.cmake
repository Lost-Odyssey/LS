# test_derive.cmake — static reflection Stage 1: @derive(Equal, Hash, Order).
#  * JIT + AOT: synthesized ==/hash/< field-by-field (POD + Str fields);
#    Hash+Equal make the struct a usable Map key; Order gives lexicographic <
#  * memcheck: clean
#  * inspect: the derived ==, hash, < methods show up
#  * negative: an unsupported trait is a compile error
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/derive_equal.ls")
set(_expected "DERIVE TRAITS DONE")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "derive_equal JIT FAILED (rc=${jit_rc})\n${jit_out}\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "derive_equal JIT missing '${_expected}'\n${jit_out}")
endif()
if("${jit_out}" MATCHES "FAIL")
    message(FATAL_ERROR "derive_equal JIT had a FAIL line\n${jit_out}")
endif()
message(STATUS "derive_equal JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/derive_equal_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "derive_equal AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0 OR NOT "${aot_out}" MATCHES "${_expected}" OR "${aot_out}" MATCHES "FAIL")
    message(FATAL_ERROR "derive_equal AOT FAILED (rc=${aot_run_rc})\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "derive_equal AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0 OR NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "derive_equal memcheck not clean\n${mc_err}")
endif()
message(STATUS "derive_equal memcheck: OK clean")

# ---- inspect shows the synthesized methods (==, hash, <) ----
execute_process(COMMAND "${LS_EXE}" inspect Point "${POS}"
    OUTPUT_VARIABLE ins_out RESULT_VARIABLE ins_rc)
if(NOT ins_rc EQUAL 0)
    message(FATAL_ERROR "inspect Point failed\n${ins_out}")
endif()
foreach(needle "def ==" "def hash" "def <")
    if(NOT "${ins_out}" MATCHES "${needle}")
        message(FATAL_ERROR "inspect Point missing derived '${needle}'\n${ins_out}")
    endif()
endforeach()
message(STATUS "derive inspect (==, hash, <): OK")

# ---- negative: unsupported trait is a compile error ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/derive_reject.ls"
    OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
if(n_rc EQUAL 0)
    message(FATAL_ERROR "derive_reject: expected compile error, got success\n${n_out}")
endif()
string(APPEND n_all "${n_out}${n_err}")
if(NOT "${n_all}" MATCHES "not supported")
    message(FATAL_ERROR "derive_reject: missing diagnostic\n${n_all}")
endif()
if("${n_all}" MATCHES "unreachable")
    message(FATAL_ERROR "derive_reject: ran past the rejected derive\n${n_all}")
endif()
message(STATUS "derive_reject: rejected as expected")

# ---- enum derive: @derive(Equal, Hash) (JIT + AOT + memcheck) ----
set(ENUMPOS "${SAMPLE_DIR}/derive_enum.ls")
set(_eexp "DERIVE ENUM DONE")
execute_process(COMMAND "${LS_EXE}" run "${ENUMPOS}"
    OUTPUT_VARIABLE e_out ERROR_VARIABLE e_err RESULT_VARIABLE e_rc)
if(NOT e_rc EQUAL 0 OR NOT "${e_out}" MATCHES "${_eexp}" OR "${e_out}" MATCHES "FAIL")
    message(FATAL_ERROR "derive_enum JIT FAILED (rc=${e_rc})\n${e_out}\n${e_err}")
endif()
set(e_aot "${WORK_DIR}/derive_enum_aot")
if(WIN32)
    set(e_aot "${e_aot}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${ENUMPOS}" -o "${e_aot}"
    RESULT_VARIABLE e_arc ERROR_VARIABLE e_aerr)
if(NOT e_arc EQUAL 0)
    message(FATAL_ERROR "derive_enum AOT compile FAILED:\n${e_aerr}")
endif()
execute_process(COMMAND "${e_aot}" OUTPUT_VARIABLE e_aout RESULT_VARIABLE e_arrc)
if(NOT e_arrc EQUAL 0 OR NOT "${e_aout}" MATCHES "${_eexp}" OR "${e_aout}" MATCHES "FAIL")
    message(FATAL_ERROR "derive_enum AOT FAILED\n${e_aout}")
endif()
file(REMOVE "${e_aot}")
execute_process(COMMAND "${LS_EXE}" run --memcheck "${ENUMPOS}"
    ERROR_VARIABLE e_mc RESULT_VARIABLE e_mrc)
if(NOT e_mrc EQUAL 0 OR NOT "${e_mc}" MATCHES "OK clean")
    message(FATAL_ERROR "derive_enum memcheck not clean\n${e_mc}")
endif()
# inspect the enum shows the derived methods
execute_process(COMMAND "${LS_EXE}" inspect Tok "${ENUMPOS}"
    OUTPUT_VARIABLE e_ins RESULT_VARIABLE e_irc)
if(NOT e_irc EQUAL 0 OR NOT "${e_ins}" MATCHES "def ==" OR NOT "${e_ins}" MATCHES "def hash"
   OR NOT "${e_ins}" MATCHES "def show" OR NOT "${e_ins}" MATCHES "def <")
    message(FATAL_ERROR "inspect Tok missing derived methods\n${e_ins}")
endif()
message(STATUS "derive_enum (Equal+Hash+Order+Show, Map key): OK")

# ---- negative: @derive(Serialize) on an enum is not supported -> compile error ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/derive_enum_reject.ls"
    OUTPUT_VARIABLE er_out ERROR_VARIABLE er_err RESULT_VARIABLE er_rc)
if(er_rc EQUAL 0)
    message(FATAL_ERROR "derive_enum_reject: expected compile error\n${er_out}")
endif()
string(APPEND er_all "${er_out}${er_err}")
if(NOT "${er_all}" MATCHES "not supported yet")
    message(FATAL_ERROR "derive_enum_reject: missing diagnostic\n${er_all}")
endif()
message(STATUS "derive_enum_reject (Order): rejected as expected")

# ---- @derive(Show): def show()->Str (POD/Str/nested fields) JIT+AOT+memcheck ----
set(SHOWPOS "${SAMPLE_DIR}/derive_show.ls")
set(_sexp "DERIVE SHOW DONE")
execute_process(COMMAND "${LS_EXE}" run "${SHOWPOS}"
    OUTPUT_VARIABLE s_out ERROR_VARIABLE s_err RESULT_VARIABLE s_rc)
if(NOT s_rc EQUAL 0 OR NOT "${s_out}" MATCHES "${_sexp}")
    message(FATAL_ERROR "derive_show JIT FAILED (rc=${s_rc})\n${s_out}\n${s_err}")
endif()
# format matches print(): nested struct recurses
foreach(needle "Point { x: 3, y: 4 }" "Named { tag: hi, n: 5 }" "Line { a: Point {")
    if(NOT "${s_out}" MATCHES "${needle}")
        message(FATAL_ERROR "derive_show missing '${needle}'\n${s_out}")
    endif()
endforeach()
set(s_aot "${WORK_DIR}/derive_show_aot")
if(WIN32)
    set(s_aot "${s_aot}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${SHOWPOS}" -o "${s_aot}"
    RESULT_VARIABLE s_arc ERROR_VARIABLE s_aerr)
if(NOT s_arc EQUAL 0)
    message(FATAL_ERROR "derive_show AOT compile FAILED:\n${s_aerr}")
endif()
execute_process(COMMAND "${s_aot}" OUTPUT_VARIABLE s_aout RESULT_VARIABLE s_arrc)
if(NOT s_arrc EQUAL 0 OR NOT "${s_aout}" MATCHES "${_sexp}")
    message(FATAL_ERROR "derive_show AOT FAILED\n${s_aout}")
endif()
file(REMOVE "${s_aot}")
execute_process(COMMAND "${LS_EXE}" run --memcheck "${SHOWPOS}"
    ERROR_VARIABLE s_mc RESULT_VARIABLE s_mrc)
if(NOT s_mrc EQUAL 0 OR NOT "${s_mc}" MATCHES "OK clean")
    message(FATAL_ERROR "derive_show memcheck not clean\n${s_mc}")
endif()
message(STATUS "derive_show (Show, nested, composable): OK")

# ---- negative: @derive(Clone) is redundant -> rejected with explanation ----
execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/derive_clone_reject.ls"
    OUTPUT_VARIABLE cl_out ERROR_VARIABLE cl_err RESULT_VARIABLE cl_rc)
if(cl_rc EQUAL 0)
    message(FATAL_ERROR "derive_clone_reject: expected compile error\n${cl_out}")
endif()
string(APPEND cl_all "${cl_out}${cl_err}")
if(NOT "${cl_all}" MATCHES "unnecessary")
    message(FATAL_ERROR "derive_clone_reject: missing explanation\n${cl_all}")
endif()
message(STATUS "derive_clone_reject: rejected as expected")

message(STATUS "test_derive: ALL PASSED")
