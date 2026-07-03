# test_diag_json.cmake — C2-3 (docs/plan_diagnostics_v2.md §3.4/§6):
# `lls check --json` prints one schema-v1 JSON object on stdout, keeps
# stderr empty, and preserves the exit code (non-zero on any error).
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

function(_check_json _name _expect_rc)
    execute_process(
        COMMAND "${LS_EXE}" check --json "${SAMPLE_DIR}/${_name}.lls"
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE  _err
        RESULT_VARIABLE _rc
    )
    if(_expect_rc STREQUAL "zero" AND NOT _rc EQUAL 0)
        message(FATAL_ERROR "${_name}: expected exit 0, got ${_rc}\nstderr:\n${_err}")
    endif()
    if(_expect_rc STREQUAL "nonzero" AND _rc EQUAL 0)
        message(FATAL_ERROR "${_name}: expected non-zero exit\nstdout:\n${_out}")
    endif()
    if(NOT "${_err}" STREQUAL "")
        message(FATAL_ERROR "${_name}: expected empty stderr under --json\nstderr:\n${_err}")
    endif()
    if(NOT "${_out}" MATCHES "\"version\":1")
        message(FATAL_ERROR "${_name}: missing schema version\nstdout:\n${_out}")
    endif()
    foreach(_pat ${ARGN})
        if(NOT "${_out}" MATCHES "${_pat}")
            message(FATAL_ERROR "${_name}: stdout must match '${_pat}'\nstdout:\n${_out}")
        endif()
    endforeach()
    message(STATUS "test_diag_json ${_name}: OK")
endfunction()

# Type error + did-you-mean: kind/stage/help fields present.
_check_json(diag_render_suggest nonzero
    "\"kind\":\"type\"" "\"stage\":\"check\""
    "\"help\":\"did you mean 'length'\\?\"")

# Parse error: stage=parse.
_check_json(diag_render_parse nonzero
    "\"kind\":\"parse\"" "\"stage\":\"parse\"")

# Move error: kind=move.
_check_json(diag_render_move nonzero
    "\"kind\":\"move\"" "\"stage\":\"check\"")

# Clean file: empty diagnostics array, exit 0.
_check_json(diag_json_ok zero
    "\"truncated\":false" "\"diagnostics\":\\[\\]")
