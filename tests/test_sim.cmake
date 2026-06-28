# test_sim.cmake — sim (instruction-level microarch sim + advisor, lib/sim/).
# Runs each sim_* sample through JIT + AOT + memcheck (0/0/0).
# LS_HOME points at the source tree so ls.exe resolves lib/sim/*.ls.
cmake_minimum_required(VERSION 3.20)

set(ENV{LS_HOME} "${REPO_DIR}")

# sim_bfp8_html writes tmp/sim_bfp8_viz.html relative to cwd (= REPO_DIR via the
# add_test WORKING_DIRECTORY) — make sure the dir exists on a fresh checkout.
file(MAKE_DIRECTORY "${REPO_DIR}/tmp")

set(SAMPLES
    "sim_viz_test.ls|SIM VIZ PASS"
    "sim_engine_test.ls|SIM ENGINE PASS"
    "sim_advisor_test.ls|SIM ADVISOR PASS"
    "sim_uarch_test.ls|SIM UARCH PASS"
    "sim_pipeline_test.ls|SIM PIPELINE PASS"
    "sim_engine2_test.ls|SIM ENGINE2 PASS"
    "sim_catalog_test.ls|SIM CATALOG PASS"
    "sim_analysis_test.ls|SIM ANALYSIS PASS"
    "sim_frontend_test.ls|SIM FRONTEND PASS"
    "sim_cmul_advice_test.ls|SIM CMUL ADVICE PASS"
    "sim_report_test.ls|SIM REPORT PASS"
    "sim_licm_test.ls|SIM LICM PASS"
    "sim_earlyexit_test.ls|SIM EARLYEXIT PASS"
    "sim_mca_oracle_test.ls|SIM MCA ORACLE PASS"
    "sim_asmfile_test.ls|SIM ASMFILE PASS"
    "sim_branch_test.ls|SIM BRANCH PASS"
    "sim_batch_compare_test.ls|SIM BATCH COMPARE PASS"
    "sim_movement_test.ls|SIM MOVEMENT PASS"
    "sim_isa_table_test.ls|SIM ISA TABLE PASS"
    "sim_intrinsics_test.ls|SIM INTRINSICS PASS"
    "sim_movement_gallery_test.ls|SIM MOVEMENT GALLERY DONE"
    "sim_mask_track_test.ls|SIM MASK TRACK PASS"
    "sim_bfp8_walk.ls|BFP8 WALK DONE"
    "sim_bfp8_html.ls|SIM BFP8 HTML DONE"
    "sim_full_html.ls|SIM FULL HTML DONE"
)

foreach(entry ${SAMPLES})
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 fname)
    list(GET parts 1 expected)
    set(src "${SAMPLE_DIR}/${fname}")

    # ---- JIT ----
    execute_process(COMMAND "${LS_EXE}" run "${src}"
        OUTPUT_VARIABLE o RESULT_VARIABLE rc ERROR_VARIABLE e)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "sim JIT ${fname} FAILED (rc=${rc})\n${o}\n${e}")
    endif()
    if(NOT "${o}" MATCHES "${expected}")
        message(FATAL_ERROR "sim JIT ${fname} missing '${expected}'\n${o}")
    endif()
    if("${o}" MATCHES "FAIL")
        message(FATAL_ERROR "sim JIT ${fname} had a FAIL line\n${o}")
    endif()

    # ---- AOT ----
    set(bin "${WORK_DIR}/${fname}_aot.exe")
    execute_process(COMMAND "${LS_EXE}" compile "${src}" -o "${bin}"
        RESULT_VARIABLE crc ERROR_VARIABLE ce)
    if(NOT crc EQUAL 0)
        message(FATAL_ERROR "sim AOT compile ${fname} FAILED\n${ce}")
    endif()
    execute_process(COMMAND "${bin}" OUTPUT_VARIABLE ao RESULT_VARIABLE arc)
    if(NOT arc EQUAL 0)
        message(FATAL_ERROR "sim AOT run ${fname} FAILED (rc=${arc})\n${ao}")
    endif()
    if(NOT "${ao}" MATCHES "${expected}")
        message(FATAL_ERROR "sim AOT ${fname} missing '${expected}'\n${ao}")
    endif()
    if("${ao}" MATCHES "FAIL")
        message(FATAL_ERROR "sim AOT ${fname} had a FAIL line\n${ao}")
    endif()
    file(REMOVE "${bin}")

    # ---- memcheck (0 leak / 0 double-free / 0 invalid free) ----
    execute_process(COMMAND "${LS_EXE}" run --memcheck "${src}"
        OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mrc)
    if(NOT mrc EQUAL 0)
        message(FATAL_ERROR "sim memcheck ${fname} FAILED (rc=${mrc})\n${me}")
    endif()
    if(NOT "${me}" MATCHES "OK clean")
        message(FATAL_ERROR "sim memcheck ${fname} not clean\n${me}")
    endif()

    message(STATUS "sim ${fname}: JIT+AOT+memcheck OK")
endforeach()

message(STATUS "test_sim: ALL PASSED")
