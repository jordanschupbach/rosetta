option(ROSETTA_BUILD_OCAML_EXAMPLES "Build and test OCaml libraries (requires OCaml 5)" OFF)
if(ROSETTA_BUILD_OCAML_EXAMPLES)
    find_program(ROSETTA_OCAMLC NAMES ocamlc.opt ocamlc REQUIRED)
    find_program(ROSETTA_OCAMLOPT NAMES ocamlopt.opt ocamlopt REQUIRED)
    find_program(ROSETTA_OCAML NAMES ocaml REQUIRED)
    find_package(Threads REQUIRED)
    execute_process(COMMAND "${ROSETTA_OCAMLC}" -where OUTPUT_VARIABLE ROSETTA_OCAML_STDLIB OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${ROSETTA_OCAMLC}" -config-var ext_dll OUTPUT_VARIABLE ROSETTA_OCAML_EXT_DLL OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    # Native programs link the stubs' C++ runtime through the archive.
    if(APPLE)
        set(ROSETTA_OCAML_CXX_RUNTIME -cclib -lc++)
    else()
        set(ROSETTA_OCAML_CXX_RUNTIME -cclib -lstdc++)
    endif()
    # Each library NAME is NAME.ml/.cmi with NAME.cma (bytecode, loading
    # dllNAME_stubs) and NAME.cmxa (native, linking libNAME_stubs.a), all here.
    set(ocaml_dir "${CMAKE_CURRENT_BINARY_DIR}/ocaml")
    set(ocaml_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/ocaml")
    set(ocaml_modules demo verify)
    if(BUILD_TESTING)
        list(APPEND ocaml_modules names)
    endif()
    set(ocaml_libraries)
    foreach(example ${ocaml_modules})
        set(module ocaml_${example})
        if(example STREQUAL "demo")
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.hpp")
            set(policy POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.policy.json")
        elseif(example STREQUAL "verify")
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.hpp")
            set(policy POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.policy.json")
        else()
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/tests/ocaml_names.hpp")
            set(policy POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/ocaml_names.policy.json")
        endif()
        rosetta_generate_ocaml(HEADER "${header}" MODULE ${module} OUTPUT "${ocaml_generated}/${example}.cpp"
            OCAML_OUTPUT "${ocaml_dir}/${module}.ml" CLANG "${ROSETTA_CLANG_EXECUTABLE}" ${policy})
        add_library(rosetta_ocaml_${example}_objects OBJECT "${ocaml_generated}/${example}.cpp")
        set_target_properties(rosetta_ocaml_${example}_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_include_directories(rosetta_ocaml_${example}_objects SYSTEM PRIVATE "${ROSETTA_OCAML_STDLIB}")
        target_compile_features(rosetta_ocaml_${example}_objects PRIVATE cxx_std_17)
        rosetta_warnings(rosetta_ocaml_${example}_objects)
        # The bytecode runtime provides the OCaml symbols when it loads the DLL.
        add_library(rosetta_ocaml_${example}_shared MODULE $<TARGET_OBJECTS:rosetta_ocaml_${example}_objects>)
        set_target_properties(rosetta_ocaml_${example}_shared PROPERTIES PREFIX "dll" SUFFIX "${ROSETTA_OCAML_EXT_DLL}"
            OUTPUT_NAME ${module}_stubs LIBRARY_OUTPUT_DIRECTORY "${ocaml_dir}")
        target_link_libraries(rosetta_ocaml_${example}_shared PRIVATE Threads::Threads)
        if(APPLE)
            target_link_options(rosetta_ocaml_${example}_shared PRIVATE -undefined dynamic_lookup)
        endif()
        add_library(rosetta_ocaml_${example}_static STATIC $<TARGET_OBJECTS:rosetta_ocaml_${example}_objects>)
        set_target_properties(rosetta_ocaml_${example}_static PROPERTIES OUTPUT_NAME ${module}_stubs ARCHIVE_OUTPUT_DIRECTORY "${ocaml_dir}")
        set(outputs "${ocaml_dir}/${module}.cmi" "${ocaml_dir}/${module}.cma" "${ocaml_dir}/${module}.cmxa")
        add_custom_command(OUTPUT ${outputs}
            COMMAND "${ROSETTA_OCAMLC}" -c -o "${ocaml_dir}/${module}.cmo" "${ocaml_dir}/${module}.ml"
            COMMAND "${ROSETTA_OCAMLC}" -a -o "${ocaml_dir}/${module}.cma" "${ocaml_dir}/${module}.cmo" -dllib -l${module}_stubs
            COMMAND "${ROSETTA_OCAMLOPT}" -c -o "${ocaml_dir}/${module}.cmx" "${ocaml_dir}/${module}.ml"
            COMMAND "${ROSETTA_OCAMLOPT}" -a -o "${ocaml_dir}/${module}.cmxa" "${ocaml_dir}/${module}.cmx"
                -cclib -l${module}_stubs ${ROSETTA_OCAML_CXX_RUNTIME}
            DEPENDS "${ocaml_dir}/${module}.ml" COMMENT "Compiling OCaml module ${module}" VERBATIM)
        add_custom_target(rosetta_ocaml_${example} ALL DEPENDS ${outputs} rosetta_ocaml_${example}_shared rosetta_ocaml_${example}_static)
        list(APPEND ocaml_libraries ${module})
    endforeach()
    target_sources(rosetta_ocaml_verify_objects PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.cpp")
    target_compile_features(rosetta_ocaml_verify_objects PRIVATE cxx_std_20)
    # The toplevel loads each .cma, which finds its DLL on the -I path.
    set(ocaml_command "${ROSETTA_OCAML}" -I "${ocaml_dir}")
    add_custom_target(ocaml-repl
        COMMAND ${ocaml_command} -init "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.ml" ocaml_demo.cma ocaml_verify.cma
        DEPENDS rosetta_ocaml_demo rosetta_ocaml_verify USES_TERMINAL VERBATIM)
    if(BUILD_TESTING)
        set(test_source "${ocaml_dir}/test/test_ocaml.ml")
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ocaml.ml" "${test_source}" COPYONLY)
        foreach(family scalars objects features)
            add_test(NAME ocaml_${family} COMMAND ${ocaml_command} ocaml_demo.cma ocaml_verify.cma ocaml_names.cma "${test_source}" ${family})
            set_tests_properties(ocaml_${family} PROPERTIES TIMEOUT 45)
        endforeach()
        # The same tests compiled to native code.
        add_custom_command(OUTPUT "${ocaml_dir}/test/test_ocaml"
            COMMAND "${ROSETTA_OCAMLOPT}" -I "${ocaml_dir}" -o "${ocaml_dir}/test/test_ocaml"
                ocaml_demo.cmxa ocaml_verify.cmxa ocaml_names.cmxa "${test_source}"
            DEPENDS "${test_source}" rosetta_ocaml_demo rosetta_ocaml_verify rosetta_ocaml_names
                "${ocaml_dir}/ocaml_demo.cmxa" "${ocaml_dir}/ocaml_verify.cmxa" "${ocaml_dir}/ocaml_names.cmxa"
                rosetta_ocaml_demo_static rosetta_ocaml_verify_static rosetta_ocaml_names_static
            COMMENT "Compiling native OCaml tests" VERBATIM)
        add_custom_target(rosetta_ocaml_native_tests ALL DEPENDS "${ocaml_dir}/test/test_ocaml")
        foreach(family scalars objects features)
            add_test(NAME ocaml_native_${family} COMMAND "${ocaml_dir}/test/test_ocaml" ${family})
            set_tests_properties(ocaml_native_${family} PROPERTIES TIMEOUT 45)
        endforeach()
        add_test(NAME ocaml_repl COMMAND ${ocaml_command} ocaml_demo.cma ocaml_verify.cma "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.ml")
        set_tests_properties(ocaml_repl PROPERTIES TIMEOUT 45 PASS_REGULAR_EXPRESSION "R_demo.add 20 22 = 42")
    endif()
endif()
