option(ROSETTA_BUILD_DART_EXAMPLES "Build and test Dart packages (requires the Dart SDK 3.3+)" OFF)
if(ROSETTA_BUILD_DART_EXAMPLES AND BUILD_TESTING)
    # dart: on PATH, or under ROSETTA_DART_ROOT (the Nix dev shell sets it).
    find_program(ROSETTA_DART_EXECUTABLE NAMES dart HINTS "$ENV{ROSETTA_DART_ROOT}/bin" REQUIRED)
    set(dart_dir "${CMAKE_CURRENT_BINARY_DIR}/dart")

    # rosetta_dart_test(NAME <test> HEADER <hpp> MODULE <module> POLICY <json> SOURCE <test.dart>
    #                   INCLUDE <dir> [PARTS n])
    # Generates the Dart package and the C++ library, builds the library, and adds two ctests: the
    # test program run with the Dart VM (with a package config naming the generated package, and
    # MODULE_DART_LIBRARY naming the library), and dart analyze --fatal-infos on the package.
    function(rosetta_dart_test)
        cmake_parse_arguments(DT "" "NAME;HEADER;MODULE;POLICY;SOURCE;INCLUDE;PARTS" "" ${ARGN})
        set(generated "${dart_dir}/${DT_NAME}")
        set(sources "${generated}/${DT_MODULE}.cpp")
        set(parts)
        if(DEFINED DT_PARTS)
            set(parts PARTS ${DT_PARTS})
            math(EXPR last "${DT_PARTS} - 1")
            foreach(part RANGE 1 ${last})
                list(APPEND sources "${generated}/${DT_MODULE}_${part}.cpp")
            endforeach()
        endif()
        set(package "${generated}/${DT_MODULE}")
        rosetta_generate_dart(HEADER "${DT_HEADER}" MODULE ${DT_MODULE} CLANG "${ROSETTA_CLANG_EXECUTABLE}"
            OUTPUT "${generated}/${DT_MODULE}.cpp" DART_OUTPUT "${package}" POLICY "${DT_POLICY}" ${parts})
        add_library(rosetta_dart_${DT_NAME} SHARED ${sources})
        target_compile_features(rosetta_dart_${DT_NAME} PRIVATE cxx_std_17)
        target_include_directories(rosetta_dart_${DT_NAME} PRIVATE "${DT_INCLUDE}")
        set_target_properties(rosetta_dart_${DT_NAME} PROPERTIES OUTPUT_NAME ${DT_MODULE} LIBRARY_OUTPUT_DIRECTORY "${generated}")
        add_custom_target(rosetta_dart_${DT_NAME}_package ALL DEPENDS "${package}/lib/${DT_MODULE}.dart" rosetta_dart_${DT_NAME})
        # The package config the VM resolves package:MODULE/ through (what dart pub get would write).
        set(config "${generated}/package_config.json")
        file(WRITE "${config}" "{\"configVersion\":2,\"packages\":[{\"name\":\"${DT_MODULE}\",\"rootUri\":\"file://${package}\","
                               "\"packageUri\":\"lib/\",\"languageVersion\":\"3.3\"}]}\n")
        string(TOUPPER "${DT_MODULE}" upper)
        # The SDK writes its settings under HOME.
        set(home "${generated}/home")
        file(MAKE_DIRECTORY "${home}")
        add_test(NAME dart_${DT_NAME} COMMAND "${ROSETTA_DART_EXECUTABLE}" "--packages=${config}" "${DT_SOURCE}" "${package}/lib/${DT_MODULE}.dart")
        set_tests_properties(dart_${DT_NAME} PROPERTIES TIMEOUT 180
            ENVIRONMENT "HOME=${home};${upper}_DART_LIBRARY=$<TARGET_FILE:rosetta_dart_${DT_NAME}>")
        add_test(NAME dart_${DT_NAME}_analyze COMMAND "${ROSETTA_DART_EXECUTABLE}" analyze --fatal-infos "${package}")
        set_tests_properties(dart_${DT_NAME}_analyze PROPERTIES TIMEOUT 180 ENVIRONMENT "HOME=${home}")
    endfunction()

    # The example shared with Python, Lua, Lisp, Julia, C#, VB, Go, Rust, PHP, Perl, Swift and Pascal.
    rosetta_dart_test(NAME full_features HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp" MODULE features
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dart_full.dart" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/examples" PARTS 2)
    # Keywords, dart:core's and the runtime's names, Object's members, camelCase collisions, merged
    # overloads and methods, renamed virtual methods, multiple inheritance, operators and 64-bit values.
    rosetta_dart_test(NAME full_names HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/dart_full_names.hpp" MODULE names
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/dart_full_names.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dart_names.dart" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()
