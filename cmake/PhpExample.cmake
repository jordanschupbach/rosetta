option(ROSETTA_BUILD_PHP_EXAMPLES "Build and test PHP APIs (requires PHP 8.1+ with the FFI extension)" OFF)
if(ROSETTA_BUILD_PHP_EXAMPLES AND BUILD_TESTING)
    find_program(ROSETTA_PHP_EXECUTABLE NAMES php REQUIRED)
    execute_process(COMMAND "${ROSETTA_PHP_EXECUTABLE}" -r "echo extension_loaded('ffi') ? 'yes' : 'no';"
        OUTPUT_VARIABLE php_ffi OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT php_ffi STREQUAL "yes")
        message(FATAL_ERROR "ROSETTA_BUILD_PHP_EXAMPLES: ${ROSETTA_PHP_EXECUTABLE} has no FFI extension")
    endif()
    set(php_dir "${CMAKE_CURRENT_BINARY_DIR}/php")

    # rosetta_php_test(NAME <test> HEADER <hpp> MODULE <module> POLICY <json> SOURCE <test.php>
    #                  INCLUDE <dir> [PARTS n])
    # Generates the PHP file and the C++ library, builds the library, lints the PHP file and adds a
    # ctest running the test program with the generated file (MODULE_PHP_LIBRARY names the library).
    function(rosetta_php_test)
        cmake_parse_arguments(PH "" "NAME;HEADER;MODULE;POLICY;SOURCE;INCLUDE;PARTS" "" ${ARGN})
        set(generated "${php_dir}/${PH_NAME}")
        set(sources "${generated}/${PH_MODULE}.cpp")
        set(parts)
        if(DEFINED PH_PARTS)
            set(parts PARTS ${PH_PARTS})
            math(EXPR last "${PH_PARTS} - 1")
            foreach(part RANGE 1 ${last})
                list(APPEND sources "${generated}/${PH_MODULE}_${part}.cpp")
            endforeach()
        endif()
        rosetta_generate_php(HEADER "${PH_HEADER}" MODULE ${PH_MODULE} CLANG "${ROSETTA_CLANG_EXECUTABLE}"
            OUTPUT "${generated}/${PH_MODULE}.cpp" PHP_OUTPUT "${generated}/${PH_MODULE}.php" POLICY "${PH_POLICY}" ${parts})
        add_library(rosetta_php_${PH_NAME} SHARED ${sources})
        target_compile_features(rosetta_php_${PH_NAME} PRIVATE cxx_std_17)
        target_include_directories(rosetta_php_${PH_NAME} PRIVATE "${PH_INCLUDE}")
        set_target_properties(rosetta_php_${PH_NAME} PROPERTIES OUTPUT_NAME ${PH_MODULE} LIBRARY_OUTPUT_DIRECTORY "${generated}")
        add_custom_target(rosetta_php_${PH_NAME}_file ALL DEPENDS "${generated}/${PH_MODULE}.php")
        string(TOUPPER "${PH_MODULE}" environment)
        add_test(NAME php_${PH_NAME} COMMAND "${CMAKE_COMMAND}" -E env "${environment}_PHP_LIBRARY=$<TARGET_FILE:rosetta_php_${PH_NAME}>"
            "${ROSETTA_PHP_EXECUTABLE}" -d zend.assertions=1 -d error_reporting=-1 -d display_errors=stderr
            "${PH_SOURCE}" "${generated}/${PH_MODULE}.php")
        set_tests_properties(php_${PH_NAME} PROPERTIES TIMEOUT 120)
        # The file passes PHP's own lint.
        add_test(NAME php_${PH_NAME}_lint COMMAND "${ROSETTA_PHP_EXECUTABLE}" -l "${generated}/${PH_MODULE}.php")
    endfunction()

    # The example shared with Python, Lua, Lisp, Julia, C#, VB, Go and Rust.
    rosetta_php_test(NAME full_features HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp" MODULE features
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_php_full.php" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/examples" PARTS 2)
    # Reserved words, case-insensitive names, the runtime's names, hiding and multiple inheritance.
    rosetta_php_test(NAME full_names HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/php_full_names.hpp" MODULE names
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/php_full_names.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_php_names.php" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()
