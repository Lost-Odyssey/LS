# test_reflect.cmake — static reflection Stage 3: @derive(Reflect) runtime API.
#  * TypeInfo { name, fields (name+type), funcs (name+signature+static) }
#  * fields from the struct; method signatures scanned from the program AST
#  * JIT + AOT + memcheck
cmake_minimum_required(VERSION 3.20)

set(POS "${SAMPLE_DIR}/derive_reflect.lls")

execute_process(COMMAND "${LS_EXE}" run "${POS}"
    OUTPUT_VARIABLE r_out ERROR_VARIABLE r_err RESULT_VARIABLE r_rc)
if(NOT r_rc EQUAL 0 OR NOT "${r_out}" MATCHES "DERIVE REFLECT DONE")
    message(FATAL_ERROR "derive_reflect JIT FAILED (rc=${r_rc})\n${r_out}\n${r_err}")
endif()
foreach(needle "Config" "host: Str" "port: int" "tls: bool"
               "def area" "def make")
    if(NOT "${r_out}" MATCHES "${needle}")
        message(FATAL_ERROR "derive_reflect missing '${needle}'\n${r_out}")
    endif()
endforeach()
message(STATUS "derive_reflect JIT (fields + methods): OK")

set(r_aot "${WORK_DIR}/derive_reflect_aot")
if(WIN32)
    set(r_aot "${r_aot}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${POS}" -o "${r_aot}"
    RESULT_VARIABLE r_arc ERROR_VARIABLE r_aerr)
if(NOT r_arc EQUAL 0)
    message(FATAL_ERROR "derive_reflect AOT compile FAILED:\n${r_aerr}")
endif()
execute_process(COMMAND "${r_aot}" OUTPUT_VARIABLE r_aout RESULT_VARIABLE r_arrc)
if(NOT r_arrc EQUAL 0 OR NOT "${r_aout}" MATCHES "DERIVE REFLECT DONE")
    message(FATAL_ERROR "derive_reflect AOT FAILED\n${r_aout}")
endif()
file(REMOVE "${r_aot}")
execute_process(COMMAND "${LS_EXE}" run --memcheck "${POS}"
    ERROR_VARIABLE r_mc RESULT_VARIABLE r_mrc)
if(NOT r_mrc EQUAL 0 OR NOT "${r_mc}" MATCHES "OK clean")
    message(FATAL_ERROR "derive_reflect memcheck not clean\n${r_mc}")
endif()
message(STATUS "derive_reflect: OK")

# --- Regression: reflect an IMPORTED type whose deriving module is imported
# before std.core.reflect. Used to spuriously fail with "unknown type 'TypeInfo'"
# (transitive trait propagation re-resolved interface Reflect's
# `reflect() -> TypeInfo` in the importer's scope before TypeInfo was bound). ---
set(IMP "${SAMPLE_DIR}/reflect_imported.lls")
execute_process(COMMAND "${LS_EXE}" run "${IMP}"
    OUTPUT_VARIABLE i_out ERROR_VARIABLE i_err RESULT_VARIABLE i_rc)
if(NOT i_rc EQUAL 0 OR NOT "${i_out}" MATCHES "REFLECT IMPORTED DONE")
    message(FATAL_ERROR "reflect_imported JIT FAILED (rc=${i_rc})\n${i_out}\n${i_err}")
endif()
foreach(needle "Widget" "value: int" "id: int" "Local" "def ~\\(&!self\\)")
    if(NOT "${i_out}" MATCHES "${needle}")
        message(FATAL_ERROR "reflect_imported missing '${needle}'\n${i_out}")
    endif()
endforeach()
# The destructor must surface as `~`, never the internal `__drop` name.
if("${i_out}" MATCHES "__drop")
    message(FATAL_ERROR "reflect_imported leaked internal '__drop' (expected '~')\n${i_out}")
endif()
message(STATUS "reflect_imported JIT (cross-module derive + ~ display): OK")

set(i_aot "${WORK_DIR}/reflect_imported_aot")
if(WIN32)
    set(i_aot "${i_aot}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${IMP}" -o "${i_aot}"
    RESULT_VARIABLE i_arc ERROR_VARIABLE i_aerr)
if(NOT i_arc EQUAL 0)
    message(FATAL_ERROR "reflect_imported AOT compile FAILED:\n${i_aerr}")
endif()
execute_process(COMMAND "${i_aot}" OUTPUT_VARIABLE i_aout RESULT_VARIABLE i_arrc)
if(NOT i_arrc EQUAL 0 OR NOT "${i_aout}" MATCHES "REFLECT IMPORTED DONE")
    message(FATAL_ERROR "reflect_imported AOT FAILED\n${i_aout}")
endif()
file(REMOVE "${i_aot}")
execute_process(COMMAND "${LS_EXE}" run --memcheck "${IMP}"
    ERROR_VARIABLE i_mc RESULT_VARIABLE i_mrc)
if(NOT i_mrc EQUAL 0 OR NOT "${i_mc}" MATCHES "OK clean")
    message(FATAL_ERROR "reflect_imported memcheck not clean\n${i_mc}")
endif()
message(STATUS "reflect_imported (cross-module derive): OK")

# --- Foundational containers Vec / Str / Map reflect via the from_raw bridge
# (Vec/Str derive ReflectRaw in their leaf-safe modules; Map derives Reflect). ---
set(CON "${SAMPLE_DIR}/reflect_containers.lls")
execute_process(COMMAND "${LS_EXE}" run "${CON}"
    OUTPUT_VARIABLE c_out ERROR_VARIABLE c_err RESULT_VARIABLE c_rc)
if(NOT c_rc EQUAL 0 OR NOT "${c_out}" MATCHES "REFLECT CONTAINERS DONE")
    message(FATAL_ERROR "reflect_containers JIT FAILED (rc=${c_rc})\n${c_out}\n${c_err}")
endif()
foreach(needle "bare:Bare" "Vec" "data: \\*T" "vec-has-push" "vec-has-tilde"
               "Str" "data: \\*u8" "str-has-split"
               "Map" "keys: \\*K")
    if(NOT "${c_out}" MATCHES "${needle}")
        message(FATAL_ERROR "reflect_containers missing '${needle}'\n${c_out}")
    endif()
endforeach()
message(STATUS "reflect_containers JIT (Vec/Str/Map auto-reflect): OK")

set(c_aot "${WORK_DIR}/reflect_containers_aot")
if(WIN32)
    set(c_aot "${c_aot}.exe")
endif()
execute_process(COMMAND "${LS_EXE}" compile "${CON}" -o "${c_aot}"
    RESULT_VARIABLE c_arc ERROR_VARIABLE c_aerr)
if(NOT c_arc EQUAL 0)
    message(FATAL_ERROR "reflect_containers AOT compile FAILED:\n${c_aerr}")
endif()
execute_process(COMMAND "${c_aot}" OUTPUT_VARIABLE c_aout RESULT_VARIABLE c_arrc)
if(NOT c_arrc EQUAL 0 OR NOT "${c_aout}" MATCHES "REFLECT CONTAINERS DONE")
    message(FATAL_ERROR "reflect_containers AOT FAILED\n${c_aout}")
endif()
file(REMOVE "${c_aot}")
execute_process(COMMAND "${LS_EXE}" run --memcheck "${CON}"
    ERROR_VARIABLE c_mc RESULT_VARIABLE c_mrc)
if(NOT c_mrc EQUAL 0 OR NOT "${c_mc}" MATCHES "OK clean")
    message(FATAL_ERROR "reflect_containers memcheck not clean\n${c_mc}")
endif()
message(STATUS "reflect_containers (Vec/Str/Map): OK")

message(STATUS "test_reflect: ALL PASSED")
