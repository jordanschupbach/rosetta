option(ROSETTA_BUILD_PROLOG_EXAMPLES "Build and test Prolog APIs (requires SWI-Prolog 9 and its headers)" OFF)

# rosetta_prolog_flags(<target> [SWIPL <swipl>]): compiles <target> (a library with the generated
# foreign glue) with SWI-Prolog's headers. The library resolves SWI-Prolog's symbols when swipl
# loads it, so it does not link libswipl (on macOS, add -undefined dynamic_lookup).
function(rosetta_prolog_flags target)
    cmake_parse_arguments(ARG "" "SWIPL" "" ${ARGN})
    if(NOT ARG_SWIPL)
        find_program(ROSETTA_SWIPL_EXECUTABLE NAMES swipl REQUIRED)
        set(ARG_SWIPL "${ROSETTA_SWIPL_EXECUTABLE}")
    endif()
    execute_process(COMMAND "${ARG_SWIPL}" --dump-runtime-variables OUTPUT_VARIABLE variables)
    if(NOT variables MATCHES "PLBASE=\"([^\"]*)\"")
        message(FATAL_ERROR "cannot find SWI-Prolog's home (swipl --dump-runtime-variables)")
    endif()
    target_include_directories(${target} SYSTEM PRIVATE "${CMAKE_MATCH_1}/include")
    if(APPLE)
        target_link_options(${target} PRIVATE -undefined dynamic_lookup)
    endif()
endfunction()

if(ROSETTA_BUILD_PROLOG_EXAMPLES AND BUILD_TESTING)
    find_program(ROSETTA_SWIPL_EXECUTABLE NAMES swipl REQUIRED)
    set(prolog_dir "${CMAKE_CURRENT_BINARY_DIR}/prolog")

    # rosetta_prolog_test(NAME <test> HEADER <hpp> MODULE <module> POLICY <json> SOURCE <test.pl>
    #                     INCLUDE <dir> [PARTS n])
    # Generates the Prolog module and the C++ library, builds the library with SWI-Prolog's headers,
    # and adds ctests running the test program (MODULE_PROLOG_LIBRARY names the library, and again without it) and
    # checking the module's PlDoc (tests/check_prolog_pldoc.pl).
    function(rosetta_prolog_test)
        cmake_parse_arguments(PL "" "NAME;HEADER;MODULE;POLICY;SOURCE;INCLUDE;PARTS" "" ${ARGN})
        set(generated "${prolog_dir}/${PL_NAME}")
        set(sources "${generated}/${PL_MODULE}.cpp")
        set(parts)
        if(DEFINED PL_PARTS)
            set(parts PARTS ${PL_PARTS})
            math(EXPR last "${PL_PARTS} - 1")
            foreach(part RANGE 1 ${last})
                list(APPEND sources "${generated}/${PL_MODULE}_${part}.cpp")
            endforeach()
        endif()
        rosetta_generate_prolog(HEADER "${PL_HEADER}" MODULE ${PL_MODULE} CLANG "${ROSETTA_CLANG_EXECUTABLE}"
            OUTPUT "${generated}/${PL_MODULE}.cpp" PROLOG_OUTPUT "${generated}/${PL_MODULE}.pl" POLICY "${PL_POLICY}" ${parts})
        add_library(rosetta_prolog_${PL_NAME} SHARED ${sources})
        target_compile_features(rosetta_prolog_${PL_NAME} PRIVATE cxx_std_17)
        target_include_directories(rosetta_prolog_${PL_NAME} PRIVATE "${PL_INCLUDE}")
        rosetta_prolog_flags(rosetta_prolog_${PL_NAME} SWIPL "${ROSETTA_SWIPL_EXECUTABLE}")
        set_target_properties(rosetta_prolog_${PL_NAME} PROPERTIES OUTPUT_NAME ${PL_MODULE} LIBRARY_OUTPUT_DIRECTORY "${generated}")
        add_custom_target(rosetta_prolog_${PL_NAME}_module ALL DEPENDS "${generated}/${PL_MODULE}.pl")
        string(TOUPPER "${PL_MODULE}" environment)
        set(library_env "${environment}_PROLOG_LIBRARY=$<TARGET_FILE:rosetta_prolog_${PL_NAME}>")
        add_test(NAME prolog_${PL_NAME} COMMAND "${CMAKE_COMMAND}" -E env "${library_env}"
            "${ROSETTA_SWIPL_EXECUTABLE}" "${PL_SOURCE}" -- "${generated}/${PL_MODULE}.pl")
        set_tests_properties(prolog_${PL_NAME} PROPERTIES TIMEOUT 120)
        # Without the variable, the module finds the library beside it (the default installed layout).
        add_test(NAME prolog_${PL_NAME}_beside COMMAND "${CMAKE_COMMAND}" -E env "--unset=${environment}_PROLOG_LIBRARY"
            "${ROSETTA_SWIPL_EXECUTABLE}" "${PL_SOURCE}" -- "${generated}/${PL_MODULE}.pl")
        set_tests_properties(prolog_${PL_NAME}_beside PROPERTIES TIMEOUT 120)
        add_test(NAME prolog_${PL_NAME}_pldoc COMMAND "${CMAKE_COMMAND}" -E env "${library_env}"
            "${ROSETTA_SWIPL_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_prolog_pldoc.pl" -- "${generated}/${PL_MODULE}.pl")
        set_tests_properties(prolog_${PL_NAME}_pldoc PROPERTIES TIMEOUT 120)
    endfunction()

    # The example shared with Python, Lua, Lisp, Julia, C#, VB, Go, Rust, PHP and Perl.
    rosetta_prolog_test(NAME full_features HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp" MODULE features
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_prolog_full.pl" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/examples" PARTS 2)
    # System predicates, concatenated prefixes, the runtime's names, enumerator atoms, renumbered
    # virtual methods, multiple inheritance, hiding, operators and 64-bit values.
    rosetta_prolog_test(NAME full_names HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/prolog_full_names.hpp" MODULE names
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/prolog_full_names.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_prolog_names.pl" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()
