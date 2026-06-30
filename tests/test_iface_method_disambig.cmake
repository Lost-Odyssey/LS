# test_iface_method_disambig.cmake — L-002: interface same-name method disambiguation.
#   正向：固有优先派发 + 限定调用 Iface.method(recv)（场景 A/B/C + &!self 变更
#         + owned Str move-out + 单一提供者回归）。JIT + AOT + memcheck。
#   负向：① 裸歧义调用报错（含两 interface 名）② 限定调用缺 receiver
#         ③ recv 未实现该 interface ④ 泛型类型同名（v1 限制）。
cmake_minimum_required(VERSION 3.20)
set(LS "${LS_EXE}")
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
set(F "${CMAKE_CURRENT_LIST_DIR}/samples/iface_method_disambig_test.ls")

set(_expected "100" "200" "hi" "<hi>" "14" "Tom" "R2" "ALL OK")

# --- positive: JIT ---
execute_process(COMMAND "${LS}" run "${F}"
    OUTPUT_VARIABLE so ERROR_VARIABLE se RESULT_VARIABLE sr TIMEOUT 60)
if(NOT sr EQUAL 0)
    message(FATAL_ERROR "iface_disambig JIT bad (rc=${sr}):\n${se}\n${so}")
endif()
foreach(_line ${_expected})
    if(NOT "${so}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR "iface_disambig JIT: missing '${_line}'\nstdout:\n${so}")
    endif()
endforeach()
message(STATUS "test_iface_method_disambig JIT: OK")

# --- positive: AOT ---
set(EXE "${CMAKE_BINARY_DIR}/iface_method_disambig_test_aot.exe")
execute_process(COMMAND "${LS}" compile "${F}" -o "${EXE}"
    RESULT_VARIABLE cr ERROR_VARIABLE ce TIMEOUT 90)
if(NOT cr EQUAL 0)
    message(FATAL_ERROR "iface_disambig AOT compile failed:\n${ce}")
endif()
execute_process(COMMAND "${EXE}" OUTPUT_VARIABLE ao RESULT_VARIABLE ar TIMEOUT 60)
if(NOT ar EQUAL 0)
    message(FATAL_ERROR "iface_disambig AOT run rc=${ar}\n${ao}")
endif()
foreach(_line ${_expected})
    if(NOT "${ao}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR "iface_disambig AOT: missing '${_line}'\nstdout:\n${ao}")
    endif()
endforeach()
file(REMOVE "${EXE}")
message(STATUS "test_iface_method_disambig AOT: OK")

# --- positive: memcheck (owned Str move-out through a qualified call) ---
execute_process(COMMAND "${LS}" run --memcheck "${F}"
    OUTPUT_VARIABLE mo ERROR_VARIABLE me RESULT_VARIABLE mr TIMEOUT 60)
set(mc "${mo}${me}")
if(NOT mc MATCHES "0 leak" OR NOT mc MATCHES "0 double-free" OR NOT mc MATCHES "0 invalid free")
    message(FATAL_ERROR "iface_disambig memcheck not clean:\n${mc}")
endif()
message(STATUS "test_iface_method_disambig memcheck: OK")

# --- negative: bare ambiguous call ---
execute_process(COMMAND "${LS}" run "${CMAKE_CURRENT_LIST_DIR}/samples/iface_disambig_ambiguous_reject.ls"
    OUTPUT_VARIABLE no ERROR_VARIABLE ne RESULT_VARIABLE nr TIMEOUT 30)
if(nr EQUAL 0)
    message(FATAL_ERROR "ambiguous_reject: expected compile error but got rc=0\n${no}")
endif()
if(NOT "${ne}" MATCHES "ambiguous method" OR NOT "${ne}" MATCHES "Source" OR NOT "${ne}" MATCHES "Sink")
    message(FATAL_ERROR "ambiguous_reject: expected 'ambiguous method' + interface names\nstderr:\n${ne}")
endif()
message(STATUS "test_iface_method_disambig ambiguous reject: OK")

# --- negative: qualified call without a receiver ---
execute_process(COMMAND "${LS}" run "${CMAKE_CURRENT_LIST_DIR}/samples/iface_disambig_no_recv_reject.ls"
    OUTPUT_VARIABLE no2 ERROR_VARIABLE ne2 RESULT_VARIABLE nr2 TIMEOUT 30)
if(nr2 EQUAL 0)
    message(FATAL_ERROR "no_recv_reject: expected compile error but got rc=0\n${no2}")
endif()
if(NOT "${ne2}" MATCHES "requires a receiver")
    message(FATAL_ERROR "no_recv_reject: expected 'requires a receiver'\nstderr:\n${ne2}")
endif()
message(STATUS "test_iface_method_disambig no-receiver reject: OK")

# --- negative: receiver does not implement the named interface ---
execute_process(COMMAND "${LS}" run "${CMAKE_CURRENT_LIST_DIR}/samples/iface_disambig_bad_recv_reject.ls"
    OUTPUT_VARIABLE no3 ERROR_VARIABLE ne3 RESULT_VARIABLE nr3 TIMEOUT 30)
if(nr3 EQUAL 0)
    message(FATAL_ERROR "bad_recv_reject: expected compile error but got rc=0\n${no3}")
endif()
if(NOT "${ne3}" MATCHES "interface 'Sink' has no method 'close'")
    message(FATAL_ERROR "bad_recv_reject: expected 'interface ... has no method'\nstderr:\n${ne3}")
endif()
message(STATUS "test_iface_method_disambig bad-receiver reject: OK")

# --- positive (v2): generic type same-name coexistence (JIT + AOT + memcheck) ---
set(GF "${CMAKE_CURRENT_LIST_DIR}/samples/iface_disambig_generic_test.ls")
set(_gexpected "in:i" "s3:i" "mk:i" "in:j" "mk:j" "ALL OK")

execute_process(COMMAND "${LS}" run "${GF}"
    OUTPUT_VARIABLE go ERROR_VARIABLE ge RESULT_VARIABLE gr TIMEOUT 60)
if(NOT gr EQUAL 0)
    message(FATAL_ERROR "generic disambig JIT bad (rc=${gr}):\n${ge}\n${go}")
endif()
foreach(_line ${_gexpected})
    if(NOT "${go}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR "generic disambig JIT: missing '${_line}'\nstdout:\n${go}")
    endif()
endforeach()

set(GEXE "${CMAKE_BINARY_DIR}/iface_disambig_generic_test_aot.exe")
execute_process(COMMAND "${LS}" compile "${GF}" -o "${GEXE}"
    RESULT_VARIABLE gcr ERROR_VARIABLE gce TIMEOUT 90)
if(NOT gcr EQUAL 0)
    message(FATAL_ERROR "generic disambig AOT compile failed:\n${gce}")
endif()
execute_process(COMMAND "${GEXE}" OUTPUT_VARIABLE gao RESULT_VARIABLE gar TIMEOUT 60)
if(NOT gar EQUAL 0)
    message(FATAL_ERROR "generic disambig AOT run rc=${gar}\n${gao}")
endif()
foreach(_line ${_gexpected})
    if(NOT "${gao}" MATCHES "(^|\n)${_line}(\r?\n|$)")
        message(FATAL_ERROR "generic disambig AOT: missing '${_line}'\nstdout:\n${gao}")
    endif()
endforeach()
file(REMOVE "${GEXE}")

execute_process(COMMAND "${LS}" run --memcheck "${GF}"
    OUTPUT_VARIABLE gmo ERROR_VARIABLE gme RESULT_VARIABLE gmr TIMEOUT 60)
set(gmc "${gmo}${gme}")
if(NOT gmc MATCHES "0 leak" OR NOT gmc MATCHES "0 double-free" OR NOT gmc MATCHES "0 invalid free")
    message(FATAL_ERROR "generic disambig memcheck not clean:\n${gmc}")
endif()
message(STATUS "test_iface_method_disambig generic coexistence (JIT+AOT+memcheck): OK")

message(STATUS "test_iface_method_disambig: ALL PASSED")
