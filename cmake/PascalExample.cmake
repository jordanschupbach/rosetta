option(ROSETTA_BUILD_PASCAL_EXAMPLES "Build and test Free Pascal units (requires fpc 3.2+)" OFF)
if(ROSETTA_BUILD_PASCAL_EXAMPLES AND BUILD_TESTING)
    # fpc: on PATH, or under ROSETTA_FPC_ROOT (the Nix dev shell sets it).
    find_program(ROSETTA_FPC_EXECUTABLE NAMES fpc HINTS "$ENV{ROSETTA_FPC_ROOT}/bin" REQUIRED)
    set(pascal_dir "${CMAKE_CURRENT_BINARY_DIR}/pascal")

    # rosetta_pascal_test(NAME <test> HEADER <hpp> MODULE <module> POLICY <json> SOURCE <test.pas>
    #                     INCLUDE <dir> [PARTS n])
    # Generates the Pascal unit and the C++ library, builds the library, compiles the test program
    # (with the unit) with fpc, and adds a ctest running the program on the generated unit.
    function(rosetta_pascal_test)
        cmake_parse_arguments(PT "" "NAME;HEADER;MODULE;POLICY;SOURCE;INCLUDE;PARTS" "" ${ARGN})
        set(generated "${pascal_dir}/${PT_NAME}")
        set(sources "${generated}/${PT_MODULE}.cpp")
        set(parts)
        if(DEFINED PT_PARTS)
            set(parts PARTS ${PT_PARTS})
            math(EXPR last "${PT_PARTS} - 1")
            foreach(part RANGE 1 ${last})
                list(APPEND sources "${generated}/${PT_MODULE}_${part}.cpp")
            endforeach()
        endif()
        set(unit "${generated}/${PT_MODULE}.pas")
        rosetta_generate_pascal(HEADER "${PT_HEADER}" MODULE ${PT_MODULE} CLANG "${ROSETTA_CLANG_EXECUTABLE}"
            OUTPUT "${generated}/${PT_MODULE}.cpp" PASCAL_OUTPUT "${unit}" POLICY "${PT_POLICY}" ${parts})
        add_library(rosetta_pascal_${PT_NAME} SHARED ${sources})
        target_compile_features(rosetta_pascal_${PT_NAME} PRIVATE cxx_std_17)
        target_include_directories(rosetta_pascal_${PT_NAME} PRIVATE "${PT_INCLUDE}")
        set_target_properties(rosetta_pascal_${PT_NAME} PROPERTIES OUTPUT_NAME ${PT_MODULE} LIBRARY_OUTPUT_DIRECTORY "${generated}")
        set(build "${generated}/build")
        get_filename_component(program_name "${PT_SOURCE}" NAME_WE)
        set(program "${build}/${program_name}")
        # Warnings are errors (-Sew), and -vw shows them; -gh checks the heap at exit.
        add_custom_command(OUTPUT "${program}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${build}/units"
            COMMAND "${ROSETTA_FPC_EXECUTABLE}" -vw -Sew -gl -gh "-Fu${generated}" "-FU${build}/units" "-Fl${generated}" -k-rpath "-k${generated}"
                "-o${program}" "${PT_SOURCE}"
            DEPENDS "${PT_SOURCE}" "${unit}" rosetta_pascal_${PT_NAME}
            COMMENT "Building the Pascal ${PT_NAME} test" VERBATIM)
        add_custom_target(rosetta_pascal_${PT_NAME}_program ALL DEPENDS "${program}")
        add_test(NAME pascal_${PT_NAME} COMMAND "${program}" "${unit}")
        # heaptrc reports leaks on stderr ("N unfreed memory blocks"), which fail the test.
        set_tests_properties(pascal_${PT_NAME} PROPERTIES TIMEOUT 120 FAIL_REGULAR_EXPRESSION "[1-9][0-9]* unfreed memory blocks")
    endfunction()

    # The example shared with Python, Lua, Lisp, Julia, C#, VB, Go, Rust, PHP, Perl and Swift.
    rosetta_pascal_test(NAME full_features HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp" MODULE features
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pascal_full.pas" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/examples" PARTS 2)
    # Names alike but for case, keywords, the runtime's names, TObject's members, renamed virtual
    # methods, multiple inheritance, hiding, operators and 64-bit values.
    rosetta_pascal_test(NAME full_names HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/pascal_full_names.hpp" MODULE names
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/pascal_full_names.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pascal_names.pas" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()
