option(ROSETTA_BUILD_ADA_EXAMPLES "Build and test Ada packages (requires GNAT with Ada 2022)" OFF)
if(ROSETTA_BUILD_ADA_EXAMPLES AND BUILD_TESTING)
    # gnatmake: on PATH, or under ROSETTA_GNAT_ROOT (the Nix dev shell sets it).
    find_program(ROSETTA_GNATMAKE_EXECUTABLE NAMES gnatmake HINTS "$ENV{ROSETTA_GNAT_ROOT}/bin" REQUIRED)
    get_filename_component(gnat_bin "${ROSETTA_GNATMAKE_EXECUTABLE}" DIRECTORY)
    set(ada_dir "${CMAKE_CURRENT_BINARY_DIR}/ada")

    # rosetta_ada_test(NAME <test> HEADER <hpp> MODULE <module> POLICY <json> SOURCE <test.adb>
    #                  INCLUDE <dir> [SUPPORT <dir>] [PARTS n])
    # Generates the Ada package and the C++ library, builds the library, compiles the test program
    # (with the package, and the Ada sources in SUPPORT) with gnatmake, and adds a ctest running the
    # program on the generated spec.
    function(rosetta_ada_test)
        cmake_parse_arguments(AT "" "NAME;HEADER;MODULE;POLICY;SOURCE;INCLUDE;SUPPORT;PARTS" "" ${ARGN})
        set(generated "${ada_dir}/${AT_NAME}")
        set(sources "${generated}/${AT_MODULE}.cpp")
        set(parts)
        if(DEFINED AT_PARTS)
            set(parts PARTS ${AT_PARTS})
            math(EXPR last "${AT_PARTS} - 1")
            foreach(part RANGE 1 ${last})
                list(APPEND sources "${generated}/${AT_MODULE}_${part}.cpp")
            endforeach()
        endif()
        set(package "${generated}/package")
        rosetta_generate_ada(HEADER "${AT_HEADER}" MODULE ${AT_MODULE} CLANG "${ROSETTA_CLANG_EXECUTABLE}"
            OUTPUT "${generated}/${AT_MODULE}.cpp" ADA_OUTPUT "${package}" POLICY "${AT_POLICY}" ${parts})
        add_library(rosetta_ada_${AT_NAME} SHARED ${sources})
        target_compile_features(rosetta_ada_${AT_NAME} PRIVATE cxx_std_17)
        target_include_directories(rosetta_ada_${AT_NAME} PRIVATE "${AT_INCLUDE}")
        set_target_properties(rosetta_ada_${AT_NAME} PROPERTIES OUTPUT_NAME ${AT_MODULE} LIBRARY_OUTPUT_DIRECTORY "${generated}")
        set(build "${generated}/build")
        get_filename_component(program_name "${AT_SOURCE}" NAME_WE)
        set(program "${build}/${program_name}")
        set(support)
        set(support_sources)
        if(DEFINED AT_SUPPORT)
            set(support "-aI${AT_SUPPORT}")
            file(GLOB support_sources "${AT_SUPPORT}/*.ads" "${AT_SUPPORT}/*.adb")
        endif()
        # Warnings are errors (-gnatwa -gnatwe), the generated package included; -gnata checks assertions.
        add_custom_command(OUTPUT "${program}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${build}"
            COMMAND "${CMAKE_COMMAND}" -E env "PATH=${gnat_bin}:$ENV{PATH}"
                "${ROSETTA_GNATMAKE_EXECUTABLE}" -q -gnat2022 -gnatwa -gnatwe -gnata "-D" "${build}" "-aI${package}" ${support}
                "${AT_SOURCE}" -o "${program}" -largs "-L${generated}" "-Wl,-rpath,${generated}"
            WORKING_DIRECTORY "${build}"
            DEPENDS "${AT_SOURCE}" ${support_sources} "${package}/${AT_MODULE}.ads" rosetta_ada_${AT_NAME}
            COMMENT "Building the Ada ${AT_NAME} test" VERBATIM)
        add_custom_target(rosetta_ada_${AT_NAME}_program ALL DEPENDS "${program}")
        add_test(NAME ada_${AT_NAME} COMMAND "${program}" "${package}/${AT_MODULE}.ads")
        set_tests_properties(ada_${AT_NAME} PROPERTIES TIMEOUT 120)
    endfunction()

    # The example shared with Python, Lua, Lisp, Julia, C#, VB, Go, Rust, PHP, Perl, Swift, Pascal and Dart.
    rosetta_ada_test(NAME full_features HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp" MODULE features
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/ada/test_ada_full.adb" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/examples"
        SUPPORT "${CMAKE_CURRENT_SOURCE_DIR}/tests/ada/full" PARTS 2)
    # Names alike but for case, reserved words, the runtime's and Standard's names, renamed virtual
    # methods, multiple inheritance, hiding, operators and 64-bit values.
    rosetta_ada_test(NAME full_names HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/ada_full_names.hpp" MODULE names
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/ada_full_names.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/ada/test_ada_names.adb" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        SUPPORT "${CMAKE_CURRENT_SOURCE_DIR}/tests/ada/names")
endif()
