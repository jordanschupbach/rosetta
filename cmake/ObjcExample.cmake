option(ROSETTA_BUILD_OBJC_EXAMPLES "Build and test Objective-C classes (requires clang, GNUstep Base and libobjc2)" OFF)
if(ROSETTA_BUILD_OBJC_EXAMPLES AND BUILD_TESTING)
    # clang with Objective-C ARC, GNUstep Base (Foundation) and libobjc2: found on the usual paths,
    # or under ROSETTA_GNUSTEP_BASE_ROOT, ROSETTA_GNUSTEP_BASE_LIB and ROSETTA_LIBOBJC_ROOT (the Nix
    # dev shell sets them).
    find_program(ROSETTA_OBJC_COMPILER NAMES clang REQUIRED)
    find_path(ROSETTA_FOUNDATION_INCLUDE_DIR Foundation/Foundation.h HINTS "$ENV{ROSETTA_GNUSTEP_BASE_ROOT}/include" PATH_SUFFIXES GNUstep REQUIRED)
    find_library(ROSETTA_GNUSTEP_BASE_LIBRARY gnustep-base HINTS "$ENV{ROSETTA_GNUSTEP_BASE_LIB}/lib" "$ENV{ROSETTA_GNUSTEP_BASE_ROOT}/lib" REQUIRED)
    find_path(ROSETTA_OBJC_INCLUDE_DIR objc/runtime.h HINTS "$ENV{ROSETTA_LIBOBJC_ROOT}/include" REQUIRED)
    find_library(ROSETTA_OBJC_LIBRARY objc HINTS "$ENV{ROSETTA_LIBOBJC_ROOT}/lib" REQUIRED)
    get_filename_component(objc_base_lib "${ROSETTA_GNUSTEP_BASE_LIBRARY}" DIRECTORY)
    get_filename_component(objc_runtime_lib "${ROSETTA_OBJC_LIBRARY}" DIRECTORY)
    set(objc_dir "${CMAKE_CURRENT_BINARY_DIR}/objc")
    # GNUstep's headers go in as system headers (they do not build warning-free).
    set(objc_flags -fobjc-arc -fobjc-arc-exceptions -fobjc-runtime=gnustep-2.0 -fblocks
        -isystem "${ROSETTA_FOUNDATION_INCLUDE_DIR}" -isystem "${ROSETTA_OBJC_INCLUDE_DIR}" -Wall -Wextra -Werror -g)

    # rosetta_objc_test(NAME <test> HEADER <hpp> MODULE <module> PREFIX <prefix> POLICY <json>
    #                   SOURCE <test.m> INCLUDE <dir> [PARTS n])
    # Generates the Objective-C files and the C++ library, builds the library, compiles PREFIX.m
    # with the test program (warnings are errors), and adds a ctest running the program on the
    # generated PREFIX.h.
    function(rosetta_objc_test)
        cmake_parse_arguments(OT "" "NAME;HEADER;MODULE;PREFIX;POLICY;SOURCE;INCLUDE;PARTS" "" ${ARGN})
        set(generated "${objc_dir}/${OT_NAME}")
        set(sources "${generated}/${OT_MODULE}.cpp")
        set(parts)
        if(DEFINED OT_PARTS)
            set(parts PARTS ${OT_PARTS})
            math(EXPR last "${OT_PARTS} - 1")
            foreach(part RANGE 1 ${last})
                list(APPEND sources "${generated}/${OT_MODULE}_${part}.cpp")
            endforeach()
        endif()
        set(files "${generated}/src")
        rosetta_generate_objc(HEADER "${OT_HEADER}" MODULE ${OT_MODULE} CLANG "${ROSETTA_CLANG_EXECUTABLE}"
            OUTPUT "${generated}/${OT_MODULE}.cpp" OBJC_OUTPUT "${files}" POLICY "${OT_POLICY}" ${parts})
        add_library(rosetta_objc_${OT_NAME} SHARED ${sources})
        target_compile_features(rosetta_objc_${OT_NAME} PRIVATE cxx_std_17)
        target_include_directories(rosetta_objc_${OT_NAME} PRIVATE "${OT_INCLUDE}")
        set_target_properties(rosetta_objc_${OT_NAME} PROPERTIES OUTPUT_NAME ${OT_MODULE} LIBRARY_OUTPUT_DIRECTORY "${generated}")
        get_filename_component(program_name "${OT_SOURCE}" NAME_WE)
        set(program "${generated}/${program_name}")
        add_custom_command(OUTPUT "${program}"
            COMMAND "${ROSETTA_OBJC_COMPILER}" ${objc_flags} -I "${files}" "${files}/${OT_PREFIX}.m" "${OT_SOURCE}" -o "${program}"
                "-L${generated}" -l${OT_MODULE} "${ROSETTA_GNUSTEP_BASE_LIBRARY}" "${ROSETTA_OBJC_LIBRARY}"
                "-Wl,-rpath,${generated}:${objc_base_lib}:${objc_runtime_lib}"
            DEPENDS "${OT_SOURCE}" "${files}/${OT_PREFIX}.h" "${files}/${OT_PREFIX}.m" rosetta_objc_${OT_NAME}
            COMMENT "Building the Objective-C ${OT_NAME} test" VERBATIM)
        add_custom_target(rosetta_objc_${OT_NAME}_program ALL DEPENDS "${program}")
        add_test(NAME objc_${OT_NAME} COMMAND "${program}" "${files}/${OT_PREFIX}.h")
        set_tests_properties(objc_${OT_NAME} PROPERTIES TIMEOUT 120)
    endfunction()

    # The example shared with Python, Lua, Lisp, Julia, C#, VB, Go, Rust, PHP, Perl, Swift, Pascal, Dart and Ada.
    rosetta_objc_test(NAME full_features HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp" MODULE features PREFIX Features
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_objc_full.m" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/examples" PARTS 2)
    # The runtime's names, keywords, NSObject's selectors, method families, renamed virtual methods,
    # multiple inheritance, hiding, operators and 64-bit values.
    rosetta_objc_test(NAME full_names HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/objc_full_names.hpp" MODULE names PREFIX Names
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/objc_full_names.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_objc_names.m" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()
