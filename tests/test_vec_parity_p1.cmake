cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
set(SRC "${CMAKE_CURRENT_LIST_DIR}/samples/vec_parity_p1_test.ls")
set(OUT_EXE "${CMAKE_BINARY_DIR}/vec_parity_p1.exe")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()

execute_process(COMMAND "${LS}" run "${SRC}"
    OUTPUT_VARIABLE jo ERROR_VARIABLE je RESULT_VARIABLE jr TIMEOUT 30)
if(NOT jr EQUAL 0)
    message(FATAL_ERROR "vec_parity_p1 JIT failed (rc=${jr}):\n${je}\n${jo}")
endif()
if(NOT jo MATCHES "RAWVEC PARITY P1 PASS" OR jo MATCHES "FAIL ")
    message(FATAL_ERROR "vec_parity_p1 JIT: bad output:\n${jo}")
endif()

execute_process(COMMAND "${LS}" run --memcheck "${SRC}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 30)
if(NOT mr EQUAL 0)
    message(FATAL_ERROR "vec_parity_p1 memcheck failed (rc=${mr}):\n${me}\n${mo}")
endif()
if(NOT me MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "vec_parity_p1 memcheck mismatch:\n${me}")
endif()

execute_process(COMMAND "${LS}" compile "${SRC}" -o "${OUT_EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce OUTPUT_VARIABLE co TIMEOUT 30)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "vec_parity_p1 AOT compile failed:\n${ce}\n${co}")
endif()
execute_process(COMMAND "${OUT_EXE}" OUTPUT_VARIABLE ao ERROR_VARIABLE ae RESULT_VARIABLE ar TIMEOUT 30)
if(NOT ar EQUAL 0 OR NOT ao MATCHES "RAWVEC PARITY P1 PASS" OR ao MATCHES "FAIL ")
    message(FATAL_ERROR "vec_parity_p1 AOT: bad output (rc=${ar}):\n${ae}\n${ao}")
endif()

message(STATUS "vec_parity_p1 all passed")
