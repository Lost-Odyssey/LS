# tests/ls_opt_env.cmake — verifies that LS_OPT reaches the JIT (`lls run`),
# run via `cmake -P`.
#
# Background: `LS_OPT` is documented (docs/env_reference.md) as the optimization
# pipeline's default level, and ls_opt_default_jit() does parse it -- but the
# plain `lls run` entry point used to hard-code LS_OPT_O0 and then overwrite
# .level unconditionally, so the environment variable was silently dropped for
# JIT while it worked for AOT. Setting it looked like it worked and did nothing.
#
# Judged relationally, never against hardcoded numbers: the sample's output
# depends on the host CPU and LLVM version, so the driver first establishes the
# two baselines itself and then checks which side each override lands on.
#
#   A = `lls run sample`             -- O0 baseline (no flag, no env)
#   B = `lls run -O2 sample`         -- O2 baseline (CLI flag)
#   A != B                           -- the sample still discriminates
#   LS_OPT=2, no flag        == B    -- the fix: env reaches the JIT
#   LS_OPT=0, no flag        == A    -- env can also pin O0 explicitly
#   LS_OPT=0 with -O2        == B    -- CLI beats env
#   LS_OPT=bogus, no flag    == A    -- unparseable value falls back to O0,
#                                       NOT to optpipe's O2 AOT default
#
# The last case is the one that guards the actual footgun in this area: the
# level parser's fallback for AOT is O2, so a naive "just read the env" fix
# would flip the JIT default to O2 for anyone with a malformed LS_OPT (and, if
# the unset case were routed the same way, for everyone). The default-O0
# decision is load-bearing -- see the B1 JIT tiering spike, which measured
# +80~150% end-to-end cost for enabling any optimization level on `lls run`.
#
# Every run goes through `cmake -E env` with the same invocation shape, and the
# baselines explicitly `--unset=LS_OPT`, so an LS_OPT already exported in the
# developer's shell cannot poison them.
cmake_minimum_required(VERSION 3.20)

foreach(_required_var LS_EXE SAMPLE)
    if(NOT DEFINED ${_required_var})
        message(FATAL_ERROR "ls_opt_env.cmake: missing required -D${_required_var}=...")
    endif()
endforeach()

if(NOT EXISTS "${SAMPLE}")
    message(FATAL_ERROR "ls_opt_env: sample not found: ${SAMPLE}")
endif()

# Run the sample with an explicit LS_OPT setting (or with it unset when
# `_env` is the literal string UNSET) plus any extra CLI arguments, and
# return the trimmed stdout.
function(run_sample _out_var _env)
    if(_env STREQUAL "UNSET")
        set(_env_arg "--unset=LS_OPT")
    else()
        set(_env_arg "LS_OPT=${_env}")
    endif()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env ${_env_arg} --
                "${LS_EXE}" run ${ARGN} "${SAMPLE}"
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "ls_opt_env: `lls run ${ARGN}` (LS_OPT=${_env}) failed with rc=${_rc}\n"
            "stdout: ${_stdout}\nstderr: ${_stderr}")
    endif()
    string(STRIP "${_stdout}" _stdout)
    if(_stdout STREQUAL "")
        message(FATAL_ERROR
            "ls_opt_env: `lls run ${ARGN}` (LS_OPT=${_env}) produced no output\n"
            "stderr: ${_stderr}")
    endif()
    set(${_out_var} "${_stdout}" PARENT_SCOPE)
endfunction()

run_sample(BASE_O0 UNSET)
run_sample(BASE_O2 UNSET -O2)

if(BASE_O0 STREQUAL BASE_O2)
    message(FATAL_ERROR
        "ls_opt_env: the sample no longer distinguishes O0 from O2 "
        "(both printed '${BASE_O0}'), so this test cannot verify anything.\n"
        "Do NOT delete or weaken the test: find a new observable that the "
        "optimization pipeline changes and update tests/samples/ls_opt_env_test.lls.")
endif()

message(STATUS "ls_opt_env: O0 baseline='${BASE_O0}' O2 baseline='${BASE_O2}'")

# Each entry is "<LS_OPT value>|<extra CLI arg or NONE>|<expected baseline>|<what it pins>".
# NONE (not an empty field) stands for "no CLI flag": `|` is rewritten to the
# list separator below, and consecutive separators would not survive that.
set(_cases
    "2|NONE|O2|LS_OPT=2 reaches the JIT"
    "0|NONE|O0|LS_OPT=0 pins O0 explicitly"
    "0|-O2|O2|CLI -O2 overrides LS_OPT=0"
    "2|-O0|O0|CLI -O0 overrides LS_OPT=2"
    "bogus|NONE|O0|unparseable LS_OPT falls back to O0, not optpipe's AOT O2"
    "UNSET|NONE|O0|unset LS_OPT keeps the O0 default"
    # --memcheck / --profile used to return before the level was consulted, so
    # both LS_OPT and an explicit -O were dropped on those paths. Their stdout
    # is just the program's own output (the memcheck report goes to stderr), so
    # they are judged against the same two baselines.
    "2|--memcheck|O2|LS_OPT reaches the JIT under --memcheck"
    "UNSET|--memcheck|O0|--memcheck keeps the O0 default"
    "2|--profile|O2|LS_OPT reaches the JIT under --profile"
)

set(_failures "")
foreach(_case IN LISTS _cases)
    string(REPLACE "|" ";" _parts "${_case}")
    list(GET _parts 0 _env)
    list(GET _parts 1 _cli)
    list(GET _parts 2 _want_name)
    list(GET _parts 3 _desc)
    if(_cli STREQUAL "NONE")
        set(_cli "")
    endif()
    if(_want_name STREQUAL "O2")
        set(_want "${BASE_O2}")
    else()
        set(_want "${BASE_O0}")
    endif()

    run_sample(_got "${_env}" ${_cli})
    if(NOT _got STREQUAL _want)
        string(APPEND _failures
            "  FAIL: ${_desc}\n"
            "        LS_OPT=${_env} lls run ${_cli}\n"
            "        expected ${_want_name} baseline '${_want}', got '${_got}'\n")
    else()
        message(STATUS "ls_opt_env: PASS (${_desc})")
    endif()
endforeach()

if(NOT _failures STREQUAL "")
    message(FATAL_ERROR "ls_opt_env: assertions failed:\n${_failures}")
endif()

message(STATUS "ls_opt_env: all assertions passed")
