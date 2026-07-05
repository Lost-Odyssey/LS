# test_memcheck_aot.cmake — driven by `cmake -P` from CTest.
#
# Verifies the AOT --memcheck path end-to-end:
#   1. ls.exe compile --memcheck memcheck_phase_a.lls -> tmp.exe (links ls_memcheck.lib)
#   2. tmp.exe runs and ls_mc_report prints "OK clean" via atexit
#
# Required cache variables (passed by add_test):
#   LS_EXE    — path to the ls.exe / ls binary just built
#   SAMPLE    — absolute path to the .lls sample to compile
#   WORK_DIR  — directory to drop tmp.exe in (cleaned up at end)

if(NOT LS_EXE OR NOT SAMPLE OR NOT WORK_DIR)
    message(FATAL_ERROR "test_memcheck_aot.cmake requires LS_EXE, SAMPLE, WORK_DIR")
endif()

# Derive the output name from the sample so tests sharing this script
# (test_memcheck_aot / test_mem_m4_5_aot / test_mem_overhaul_aot) never race
# on the same .o/exe under parallel ctest. A shared hardcoded name caused
# concurrent `ls compile` runs to truncate each other's object file mid-link
# (Linux ld: "file truncated"; Windows: LNK1104).
get_filename_component(SAMPLE_STEM "${SAMPLE}" NAME_WE)
if(WIN32 OR CMAKE_HOST_WIN32)
    set(OUT_EXE "${WORK_DIR}/aot_mc_${SAMPLE_STEM}.exe")
else()
    set(OUT_EXE "${WORK_DIR}/aot_mc_${SAMPLE_STEM}")
endif()
file(REMOVE "${OUT_EXE}")

# Step 1: Compile with --memcheck. Linker must resolve ls_mc_alloc/free/report
# from the ls_memcheck static archive that lives next to ls.exe.
execute_process(
    COMMAND "${LS_EXE}" compile --memcheck "${SAMPLE}" -o "${OUT_EXE}"
    RESULT_VARIABLE compile_rc
    OUTPUT_VARIABLE compile_out
    ERROR_VARIABLE  compile_err
)
if(NOT compile_rc EQUAL 0)
    message(FATAL_ERROR
        "ls compile --memcheck failed (rc=${compile_rc})\n"
        "stdout: ${compile_out}\n"
        "stderr: ${compile_err}\n")
endif()
if(NOT EXISTS "${OUT_EXE}")
    message(FATAL_ERROR "AOT compile reported success but ${OUT_EXE} was not produced")
endif()

# Step 2: Run the AOT-compiled binary and inspect the report. The memcheck
# report goes to stderr; program output goes to stdout. We need both.
execute_process(
    COMMAND "${OUT_EXE}"
    RESULT_VARIABLE run_rc
    OUTPUT_VARIABLE run_out
    ERROR_VARIABLE  run_err
)
if(NOT run_rc EQUAL 0)
    message(FATAL_ERROR
        "${OUT_EXE} exited with rc=${run_rc}\n"
        "stdout: ${run_out}\n"
        "stderr: ${run_err}\n")
endif()

# Step 3: Grep for the clean marker. Anything else (LEAK / DOUBLE FREE /
# INVALID FREE) means a regression.
if(NOT run_err MATCHES "\\[memcheck\\] OK clean")
    message(FATAL_ERROR
        "AOT memcheck did not report 'OK clean' for ${SAMPLE}\n"
        "stdout: ${run_out}\n"
        "stderr: ${run_err}\n")
endif()

# Sanity: there should be zero leaks in the SUMMARY line.
if(NOT run_err MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR
        "AOT memcheck SUMMARY line mismatched for ${SAMPLE}\n"
        "stderr: ${run_err}\n")
endif()

# Cleanup
file(REMOVE "${OUT_EXE}")
message(STATUS "AOT memcheck PASS: ${SAMPLE}")
