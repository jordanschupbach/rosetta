option(ROSETTA_BUILD_PERL_EXAMPLES "Build and test Perl APIs (requires Perl 5.14+ and its headers)" OFF)

# rosetta_perl_flags(<target> [PERL <perl>]): compiles <target> (a library with the generated XS
# glue) with Perl's headers and compile flags. The library resolves Perl's own symbols when perl
# loads it, so it does not link libperl (on macOS, add -undefined dynamic_lookup).
function(rosetta_perl_flags target)
    cmake_parse_arguments(ARG "" "PERL" "" ${ARGN})
    if(NOT ARG_PERL)
        find_program(ROSETTA_PERL_EXECUTABLE NAMES perl REQUIRED)
        set(ARG_PERL "${ROSETTA_PERL_EXECUTABLE}")
    endif()
    execute_process(COMMAND "${ARG_PERL}" -MConfig -e [=[print "$Config{archlibexp}/CORE"]=] OUTPUT_VARIABLE perl_core)
    execute_process(COMMAND "${ARG_PERL}" -MConfig -e [=[print $Config{ccflags}]=] OUTPUT_VARIABLE perl_ccflags)
    separate_arguments(perl_ccflags UNIX_COMMAND "${perl_ccflags}")
    set(definitions)
    foreach(flag ${perl_ccflags})
        if(flag MATCHES "^-D(.+)$")
            list(APPEND definitions "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    target_include_directories(${target} SYSTEM PRIVATE "${perl_core}")
    target_compile_definitions(${target} PRIVATE ${definitions})
    if(APPLE)
        target_link_options(${target} PRIVATE -undefined dynamic_lookup)
    endif()
endfunction()

if(ROSETTA_BUILD_PERL_EXAMPLES AND BUILD_TESTING)
    find_program(ROSETTA_PERL_EXECUTABLE NAMES perl REQUIRED)
    find_program(ROSETTA_PODCHECKER_EXECUTABLE NAMES podchecker)
    set(perl_dir "${CMAKE_CURRENT_BINARY_DIR}/perl")

    # rosetta_perl_test(NAME <test> HEADER <hpp> MODULE <module> POLICY <json> SOURCE <test.pl>
    #                   INCLUDE <dir> [PARTS n])
    # Generates the Perl module and the C++ library, builds the library with Perl's headers, and
    # adds ctests running the test program (MODULE_PERL_LIBRARY names the library), compiling the
    # module (perl -c) and checking its POD (podchecker).
    function(rosetta_perl_test)
        cmake_parse_arguments(PL "" "NAME;HEADER;MODULE;POLICY;SOURCE;INCLUDE;PARTS" "" ${ARGN})
        set(generated "${perl_dir}/${PL_NAME}")
        set(sources "${generated}/${PL_MODULE}.cpp")
        set(parts)
        if(DEFINED PL_PARTS)
            set(parts PARTS ${PL_PARTS})
            math(EXPR last "${PL_PARTS} - 1")
            foreach(part RANGE 1 ${last})
                list(APPEND sources "${generated}/${PL_MODULE}_${part}.cpp")
            endforeach()
        endif()
        rosetta_generate_perl(HEADER "${PL_HEADER}" MODULE ${PL_MODULE} CLANG "${ROSETTA_CLANG_EXECUTABLE}"
            OUTPUT "${generated}/${PL_MODULE}.cpp" PERL_OUTPUT "${generated}/${PL_MODULE}.pm" POLICY "${PL_POLICY}" ${parts})
        add_library(rosetta_perl_${PL_NAME} SHARED ${sources})
        target_compile_features(rosetta_perl_${PL_NAME} PRIVATE cxx_std_17)
        target_include_directories(rosetta_perl_${PL_NAME} PRIVATE "${PL_INCLUDE}")
        rosetta_perl_flags(rosetta_perl_${PL_NAME} PERL "${ROSETTA_PERL_EXECUTABLE}")
        set_target_properties(rosetta_perl_${PL_NAME} PROPERTIES OUTPUT_NAME ${PL_MODULE} LIBRARY_OUTPUT_DIRECTORY "${generated}")
        add_custom_target(rosetta_perl_${PL_NAME}_module ALL DEPENDS "${generated}/${PL_MODULE}.pm")
        string(TOUPPER "${PL_MODULE}" environment)
        add_test(NAME perl_${PL_NAME} COMMAND "${CMAKE_COMMAND}" -E env "${environment}_PERL_LIBRARY=$<TARGET_FILE:rosetta_perl_${PL_NAME}>"
            "${ROSETTA_PERL_EXECUTABLE}" -w "${PL_SOURCE}" "${generated}/${PL_MODULE}.pm")
        set_tests_properties(perl_${PL_NAME} PROPERTIES TIMEOUT 120)
        add_test(NAME perl_${PL_NAME}_compile COMMAND "${ROSETTA_PERL_EXECUTABLE}" -c -w "${generated}/${PL_MODULE}.pm")
        if(ROSETTA_PODCHECKER_EXECUTABLE)
            add_test(NAME perl_${PL_NAME}_pod COMMAND "${ROSETTA_PODCHECKER_EXECUTABLE}" "${generated}/${PL_MODULE}.pm")
        endif()
    endfunction()

    # The example shared with Python, Lua, Lisp, Julia, C#, VB, Go, Rust and PHP.
    rosetta_perl_test(NAME full_features HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp" MODULE features
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_perl_full.pl" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/examples" PARTS 2)
    # Builtins, the runtime's names, reserved subs, renamed virtual methods, multiple inheritance,
    # hiding, operators and 64-bit values.
    rosetta_perl_test(NAME full_names HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/perl_full_names.hpp" MODULE names
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/perl_full_names.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_perl_names.pl" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()
