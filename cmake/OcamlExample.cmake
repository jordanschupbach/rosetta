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

        # The full API (--ocaml-api full): python_features.hpp (split in two sources) and the names
        # test, each a findlib package directory ocaml-full/NAME (NAME.ml/.mli/.cmi/.cma/.cmxa, the
        # stubs' dllNAME_stubs.so and libNAME_stubs.a, META). The .mli is compiled with every
        # warning as an error, and odoc (when found) must accept its documentation.
        find_program(ROSETTA_OCAMLFIND NAMES ocamlfind)
        find_program(ROSETTA_ODOC NAMES odoc)
        set(ocaml_full "${CMAKE_CURRENT_BINARY_DIR}/ocaml-full")
        foreach(example features names)
            set(dir "${ocaml_full}/${example}")
            if(example STREQUAL "features")
                rosetta_generate_ocaml(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp"
                    MODULE features OUTPUT "${ocaml_generated}/full/features.cpp" OCAML_OUTPUT "${dir}/features.ml" OCAML_API full PARTS 2
                    CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json")
                set(sources "${ocaml_generated}/full/features.cpp" "${ocaml_generated}/full/features_1.cpp")
                set(include "${CMAKE_CURRENT_SOURCE_DIR}/examples")
            else()
                rosetta_generate_ocaml(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/ocaml_full_names.hpp"
                    MODULE names OUTPUT "${ocaml_generated}/full/names.cpp" OCAML_OUTPUT "${dir}/names.ml" OCAML_API full
                    CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/ocaml_full_names.policy.json")
                set(sources "${ocaml_generated}/full/names.cpp")
                set(include "${CMAKE_CURRENT_SOURCE_DIR}/tests")
            endif()
            add_library(rosetta_ocaml_full_${example}_objects OBJECT ${sources})
            set_target_properties(rosetta_ocaml_full_${example}_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
            target_include_directories(rosetta_ocaml_full_${example}_objects PRIVATE "${include}")
            target_include_directories(rosetta_ocaml_full_${example}_objects SYSTEM PRIVATE "${ROSETTA_OCAML_STDLIB}")
            target_compile_features(rosetta_ocaml_full_${example}_objects PRIVATE cxx_std_17)
            rosetta_warnings(rosetta_ocaml_full_${example}_objects)
            add_library(rosetta_ocaml_full_${example}_shared MODULE $<TARGET_OBJECTS:rosetta_ocaml_full_${example}_objects>)
            set_target_properties(rosetta_ocaml_full_${example}_shared PROPERTIES PREFIX "dll" SUFFIX "${ROSETTA_OCAML_EXT_DLL}"
                OUTPUT_NAME ${example}_stubs LIBRARY_OUTPUT_DIRECTORY "${dir}")
            target_link_libraries(rosetta_ocaml_full_${example}_shared PRIVATE Threads::Threads)
            if(APPLE)
                target_link_options(rosetta_ocaml_full_${example}_shared PRIVATE -undefined dynamic_lookup)
            endif()
            add_library(rosetta_ocaml_full_${example}_static STATIC $<TARGET_OBJECTS:rosetta_ocaml_full_${example}_objects>)
            set_target_properties(rosetta_ocaml_full_${example}_static PROPERTIES OUTPUT_NAME ${example}_stubs ARCHIVE_OUTPUT_DIRECTORY "${dir}")
            set(outputs "${dir}/${example}.cmi" "${dir}/${example}.cmti" "${dir}/${example}.cma" "${dir}/${example}.cmxa")
            add_custom_command(OUTPUT ${outputs}
                COMMAND "${ROSETTA_OCAMLC}" -w +A -warn-error +A -bin-annot -c -o "${dir}/${example}.cmi" "${dir}/${example}.mli"
                COMMAND "${ROSETTA_OCAMLC}" -I "${dir}" -c -o "${dir}/${example}.cmo" "${dir}/${example}.ml"
                COMMAND "${ROSETTA_OCAMLC}" -a -o "${dir}/${example}.cma" "${dir}/${example}.cmo" -dllib -l${example}_stubs
                    -cclib -l${example}_stubs ${ROSETTA_OCAML_CXX_RUNTIME}
                COMMAND "${ROSETTA_OCAMLOPT}" -I "${dir}" -c -o "${dir}/${example}.cmx" "${dir}/${example}.ml"
                COMMAND "${ROSETTA_OCAMLOPT}" -a -o "${dir}/${example}.cmxa" "${dir}/${example}.cmx"
                    -cclib -l${example}_stubs ${ROSETTA_OCAML_CXX_RUNTIME}
                DEPENDS "${dir}/${example}.ml" "${dir}/${example}.mli" COMMENT "Compiling OCaml module ${example} (full API)" VERBATIM)
            add_custom_target(rosetta_ocaml_full_${example} ALL DEPENDS ${outputs}
                rosetta_ocaml_full_${example}_shared rosetta_ocaml_full_${example}_static)
            # The test, compiled to native code against the package directory.
            set(test "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ocaml_full.ml")
            if(example STREQUAL "names")
                set(test "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ocaml_full_names.ml")
            endif()
            set(program "${ocaml_full}/test/${example}")
            add_custom_command(OUTPUT "${program}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${ocaml_full}/test/${example}.build"
                COMMAND "${CMAKE_COMMAND}" -E copy "${test}" "${ocaml_full}/test/${example}.build/test_${example}.ml"
                COMMAND "${ROSETTA_OCAMLOPT}" -w +A-4-44-45-70 -warn-error +A -I "${dir}" -o "${program}"
                    ${example}.cmxa "${ocaml_full}/test/${example}.build/test_${example}.ml"
                DEPENDS "${test}" rosetta_ocaml_full_${example} "${dir}/${example}.cmxa"
                COMMENT "Compiling native OCaml ${example} test (full API)" VERBATIM)
            add_custom_target(rosetta_ocaml_full_${example}_test ALL DEPENDS "${program}")
        endforeach()
        # Native, under a small minor heap and frequent major slices (GC stress), and bytecode in
        # the toplevel (the .cma loads dllfeatures_stubs from the -I directory).
        add_test(NAME ocaml_full_features COMMAND "${ocaml_full}/test/features" "${ocaml_full}/features/features.mli")
        set_tests_properties(ocaml_full_features PROPERTIES ENVIRONMENT "OCAMLRUNPARAM=s=4k,o=20")
        add_test(NAME ocaml_full_features_bytecode COMMAND "${ROSETTA_OCAML}" -I "${ocaml_full}/features" features.cma
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ocaml_full.ml" "${ocaml_full}/features/features.mli")
        set_tests_properties(ocaml_full_features_bytecode PROPERTIES ENVIRONMENT "OCAMLRUNPARAM=v=0x400")
        add_test(NAME ocaml_full_names COMMAND "${ocaml_full}/test/names")
        set_tests_properties(ocaml_full_features ocaml_full_features_bytecode ocaml_full_names PROPERTIES TIMEOUT 120)
        set_tests_properties(ocaml_full_features ocaml_full_features_bytecode PROPERTIES PASS_REGULAR_EXPRESSION "OCaml full API passed")
        set_tests_properties(ocaml_full_names PROPERTIES PASS_REGULAR_EXPRESSION "\\[cout\\]\nOCaml full names passed")
        if(ROSETTA_ODOC)
            # odoc parses every comment of the .mli (warnings are errors) and renders HTML.
            add_test(NAME ocaml_full_docs COMMAND "${CMAKE_COMMAND}" -E chdir "${ocaml_full}/features" sh -c
                "\"$0\" compile --warn-error -o features.odoc features.cmti && \"$0\" link -o features.odocl features.odoc && \"$0\" html-generate -o html features.odocl && test -f html/features/Features/index.html -o -f html/Features/index.html"
                "${ROSETTA_ODOC}")
            set_tests_properties(ocaml_full_docs PROPERTIES TIMEOUT 120)
        else()
            message(STATUS "odoc not found: skipping the ocaml_full_docs test")
        endif()
        if(ROSETTA_OCAMLFIND)
            # A consumer build through findlib: OCAMLPATH holds the package directory (with META).
            add_test(NAME ocaml_full_findlib COMMAND "${CMAKE_COMMAND}" -E env "OCAMLPATH=${ocaml_full}" sh -c
                "cd \"$2\" && \"$0\" ocamlopt -package features -linkpkg -o consumer \"$1\" && ./consumer \"$3\""
                "${ROSETTA_OCAMLFIND}" test_features.ml "${ocaml_full}/test/features.build"
                "${ocaml_full}/features/features.mli")
            set_tests_properties(ocaml_full_findlib PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "OCaml full API passed")
        else()
            message(STATUS "ocamlfind not found: skipping the ocaml_full_findlib test")
        endif()
    endif()
endif()
