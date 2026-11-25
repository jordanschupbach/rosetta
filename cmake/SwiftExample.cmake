option(ROSETTA_BUILD_SWIFT_EXAMPLES "Build and test Swift packages (requires swiftc 5.9+)" OFF)
if(ROSETTA_BUILD_SWIFT_EXAMPLES AND BUILD_TESTING)
    # swiftc: on PATH, or under ROSETTA_SWIFT_ROOT (the Nix dev shell sets it and keeps Swift off
    # PATH, since the toolchain carries its own clang).
    find_program(ROSETTA_SWIFTC_EXECUTABLE NAMES swiftc HINTS "$ENV{ROSETTA_SWIFT_ROOT}/bin" REQUIRED)
    find_program(ROSETTA_SWIFT_PACKAGE_EXECUTABLE NAMES swift-package HINTS "$ENV{ROSETTA_SWIFT_PACKAGE_ROOT}/bin")
    get_filename_component(swift_root "${ROSETTA_SWIFTC_EXECUTABLE}" DIRECTORY)
    get_filename_component(swift_root "${swift_root}" DIRECTORY)
    # Some toolchains (Nix's) do not tell the linker where the Swift runtime is.
    set(swift_link)
    if(EXISTS "${swift_root}/lib/swift")
        set(swift_link -L "${swift_root}/lib" -Xlinker -rpath -Xlinker "${swift_root}/lib")
    endif()
    set(swift_dir "${CMAKE_CURRENT_BINARY_DIR}/swift")

    # rosetta_swift_test(NAME <test> HEADER <hpp> MODULE <module> SWIFT_MODULE <Module> POLICY <json>
    #                    SOURCE <test.swift> INCLUDE <dir> [PARTS n])
    # Generates the Swift package and the C++ library, builds the library, compiles the package's
    # Swift sources into a library module and the test program against it, and adds a ctest
    # running the program on the generated API file (plus a swift package dump-package check of the
    # manifest when swift-package is found).
    function(rosetta_swift_test)
        cmake_parse_arguments(SW "" "NAME;HEADER;MODULE;SWIFT_MODULE;POLICY;SOURCE;INCLUDE;PARTS" "" ${ARGN})
        set(generated "${swift_dir}/${SW_NAME}")
        set(package "${generated}/package")
        set(sources "${generated}/${SW_MODULE}.cpp")
        set(parts)
        if(DEFINED SW_PARTS)
            set(parts PARTS ${SW_PARTS})
            math(EXPR last "${SW_PARTS} - 1")
            foreach(part RANGE 1 ${last})
                list(APPEND sources "${generated}/${SW_MODULE}_${part}.cpp")
            endforeach()
        endif()
        rosetta_generate_swift(HEADER "${SW_HEADER}" MODULE ${SW_MODULE} CLANG "${ROSETTA_CLANG_EXECUTABLE}"
            OUTPUT "${generated}/${SW_MODULE}.cpp" SWIFT_OUTPUT "${package}" POLICY "${SW_POLICY}" ${parts})
        add_library(rosetta_swift_${SW_NAME} SHARED ${sources})
        target_compile_features(rosetta_swift_${SW_NAME} PRIVATE cxx_std_17)
        target_include_directories(rosetta_swift_${SW_NAME} PRIVATE "${SW_INCLUDE}")
        set_target_properties(rosetta_swift_${SW_NAME} PROPERTIES OUTPUT_NAME ${SW_MODULE} LIBRARY_OUTPUT_DIRECTORY "${generated}")
        set(module "${SW_SWIFT_MODULE}")
        set(api "${package}/Sources/${module}/${module}.swift")
        set(runtime "${package}/Sources/${module}/Rosetta.swift")
        set(cmodule "${package}/Sources/C${module}")
        set(build "${generated}/build")
        set(library "${build}/lib${module}Swift${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(program "${build}/test_${SW_MODULE}")
        add_custom_command(OUTPUT "${library}" "${build}/${module}.swiftmodule"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${build}"
            COMMAND "${ROSETTA_SWIFTC_EXECUTABLE}" -swift-version 5 -emit-library -emit-module -module-name ${module}
                -module-link-name ${module}Swift -I "${cmodule}" -L "${generated}" "${api}" "${runtime}" -o "${library}"
                -emit-module-path "${build}/${module}.swiftmodule" ${swift_link} -Xlinker -rpath -Xlinker "${generated}"
            DEPENDS "${api}" "${runtime}" "${cmodule}/module.modulemap" "${cmodule}/${SW_MODULE}.h" rosetta_swift_${SW_NAME}
            COMMENT "Compiling the Swift module ${module} (${SW_NAME})" VERBATIM)
        add_custom_command(OUTPUT "${program}"
            COMMAND "${ROSETTA_SWIFTC_EXECUTABLE}" -swift-version 5 "${SW_SOURCE}" -I "${build}" -I "${cmodule}" -L "${build}" -L "${generated}"
                -o "${program}" ${swift_link} -Xlinker -rpath -Xlinker "${build}" -Xlinker -rpath -Xlinker "${generated}"
            DEPENDS "${SW_SOURCE}" "${library}" "${build}/${module}.swiftmodule"
            COMMENT "Building the Swift ${SW_NAME} test" VERBATIM)
        add_custom_target(rosetta_swift_${SW_NAME}_program ALL DEPENDS "${program}")
        add_test(NAME swift_${SW_NAME} COMMAND "${program}" "${api}")
        set_tests_properties(swift_${SW_NAME} PROPERTIES TIMEOUT 120)
        if(ROSETTA_SWIFT_PACKAGE_EXECUTABLE)
            add_test(NAME swift_${SW_NAME}_manifest COMMAND "${CMAKE_COMMAND}" -E env "PATH=${swift_root}/bin:$ENV{PATH}"
                "LIBRARY_PATH=${swift_root}/lib" "HOME=${generated}/home" "${ROSETTA_SWIFT_PACKAGE_EXECUTABLE}" --package-path "${package}"
                dump-package)
            set_tests_properties(swift_${SW_NAME}_manifest PROPERTIES TIMEOUT 300 PASS_REGULAR_EXPRESSION "\"C${module}\"")
        endif()
    endfunction()

    # The example shared with Python, Lua, Lisp, Julia, C#, VB, Go, Rust, PHP and Perl.
    rosetta_swift_test(NAME full_features HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp" MODULE features
        SWIFT_MODULE Features POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_swift_full.swift" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/examples" PARTS 2)
    # The runtime's and the standard library's names, keywords, NativeObject's members, renamed
    # virtual methods, multiple inheritance, hiding, operators and 64-bit values.
    rosetta_swift_test(NAME full_names HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/swift_full_names.hpp" MODULE names
        SWIFT_MODULE Names POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/swift_full_names.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_swift_names.swift" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()
