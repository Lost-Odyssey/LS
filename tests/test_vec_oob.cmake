# test_vec_oob.cmake — Vec(T) bounds checking.
#  * Positive: in-range v[i] / get / set / get! / set! all behave (JIT+AOT+memcheck).
#  * Negative: out-of-range v[i] (read) and v[i]=x (write) abort the process with a
#    "out of bounds" diagnostic and a non-zero exit; the post-access line must NOT run.
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/vec_oob_test.ls")
set(_expected "VEC_OOB PASS")

# ---- positive: JIT ----
execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE jit_out ERROR_VARIABLE jit_err RESULT_VARIABLE jit_rc)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "vec_oob positive JIT FAILED (rc=${jit_rc})\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "vec_oob positive JIT missing '${_expected}'\n${jit_out}")
endif()
message(STATUS "vec_oob positive JIT: OK")

# ---- positive: AOT ----
set(aot_bin "${WORK_DIR}/vec_oob_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc ERROR_VARIABLE aot_err)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "vec_oob positive AOT compile FAILED:\n${aot_err}")
endif()
execute_process(COMMAND "${aot_bin}" OUTPUT_VARIABLE aot_out RESULT_VARIABLE aot_run_rc)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "vec_oob positive AOT run FAILED (rc=${aot_run_rc})")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "vec_oob positive AOT missing '${_expected}'\n${aot_out}")
endif()
file(REMOVE "${aot_bin}")
message(STATUS "vec_oob positive AOT: OK")

# ---- positive: memcheck ----
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    OUTPUT_VARIABLE mc_out ERROR_VARIABLE mc_err RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "vec_oob memcheck FAILED (rc=${mc_rc})\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "vec_oob memcheck leak\n${mc_err}")
endif()
message(STATUS "vec_oob positive memcheck: OK clean")

# ---- negative: out-of-range read/write AND mutators must abort ----
#   read/write/index/insert/remove/swap -> "... out of bounds ..."
#   truncate/resize (negative length)    -> "... negative length ..."
foreach(neg "vec_oob_panic_read" "vec_oob_panic_write"
            "vec_oob_panic_insert" "vec_oob_panic_remove" "vec_oob_panic_swap"
            "vec_oob_panic_truncate" "vec_oob_panic_resize")
    execute_process(COMMAND "${LS_EXE}" run "${SAMPLE_DIR}/${neg}.ls"
        OUTPUT_VARIABLE n_out ERROR_VARIABLE n_err RESULT_VARIABLE n_rc)
    if(n_rc EQUAL 0)
        message(FATAL_ERROR "vec_oob ${neg}: expected non-zero exit (abort)\n${n_out}")
    endif()
    if(NOT "${n_out}" MATCHES "out of bounds|negative length")
        message(FATAL_ERROR "vec_oob ${neg}: missing bounds diagnostic\n${n_out}")
    endif()
    if("${n_out}" MATCHES "AFTER")
        message(FATAL_ERROR "vec_oob ${neg}: ran past the out-of-range access\n${n_out}")
    endif()
    message(STATUS "vec_oob ${neg}: aborted as expected (rc=${n_rc})")
endforeach()

message(STATUS "test_vec_oob: ALL PASSED")
