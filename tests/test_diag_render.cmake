# test_diag_render.cmake — C2-1 (docs/plan_diagnostics_v2.md): the text
# renderer appends a source-snippet line and a caret line after each
# diagnostic. One sample per diagnostic kind (type / move / parse); each must
# fail `lls check` and render both the legacy one-line message (byte-stable
# contract) and the new snippet + caret lines.
cmake_minimum_required(VERSION 3.20)

get_filename_component(_ls_stdlib_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(ENV{LS_HOME} "${_ls_stdlib_root}")

# sample basename # expected message substring # expected caret-line regex
set(_cases
    "diag_render_type#undefined variable 'lenght'#\\| +\\^"
    "diag_render_move#use of moved variable 's'#\\| +\\^"
    "diag_render_parse#expected expression#\\| +\\^~~~"
    "diag_render_suggest#undefined variable 'lenght'#help: did you mean 'length'\\?"
)

foreach(_case ${_cases})
    string(REPLACE "#" ";" _parts "${_case}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _msg)
    list(GET _parts 2 _caret)
    execute_process(
        COMMAND "${LS_EXE}" check "${SAMPLE_DIR}/${_name}.lls"
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE  _err
        RESULT_VARIABLE _rc
    )
    if(_rc EQUAL 0)
        message(FATAL_ERROR
            "${_name}: expected check failure but it passed\nstdout:\n${_out}")
    endif()
    if(NOT "${_err}" MATCHES "${_msg}")
        message(FATAL_ERROR
            "${_name}: expected stderr to contain '${_msg}'\nstderr:\n${_err}")
    endif()
    if(NOT "${_err}" MATCHES "${_caret}")
        message(FATAL_ERROR
            "${_name}: expected caret line matching '${_caret}'\nstderr:\n${_err}")
    endif()
    message(STATUS "test_diag_render ${_name}: snippet+caret OK")
endforeach()
