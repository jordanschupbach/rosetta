option(ROSETTA_BUILD_GNU_APL_EXAMPLES "Build and test GNU APL bindings (requires GNU APL and its configured source tree)" OFF)
option(ROSETTA_BUILD_DYALOG_EXAMPLES "Build and test Dyalog APL bindings (requires dyalogscript, Dyalog 19 or later)" OFF)
if(ROSETTA_BUILD_GNU_APL_EXAMPLES OR ROSETTA_BUILD_DYALOG_EXAMPLES)
    # build/apl is a GNU APL library root (APL_LIB_ROOT): the GNU APL scripts
    # are in wslib1, so `)COPY 1 apl_demo` loads them. Libraries are in lib,
    # Dyalog namespace scripts in dyalog.
    set(apl_dir "${CMAKE_CURRENT_BINARY_DIR}/apl")
    set(apl_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/apl")
    if(ROSETTA_BUILD_GNU_APL_EXAMPLES)
        find_program(ROSETTA_GNU_APL_EXECUTABLE apl REQUIRED)
        # Native functions compile against GNU APL's own headers, from a source
        # tree configured like the interpreter (./configure writes config.h).
        set(ROSETTA_GNU_APL_SOURCE_DIR "$ENV{ROSETTA_GNU_APL_SOURCE_DIR}" CACHE PATH "Configured GNU APL source tree")
        if(NOT EXISTS "${ROSETTA_GNU_APL_SOURCE_DIR}/config.h" OR NOT EXISTS "${ROSETTA_GNU_APL_SOURCE_DIR}/src/Native_interface.hh")
            message(FATAL_ERROR "ROSETTA_GNU_APL_SOURCE_DIR must be a configured GNU APL source tree (with config.h)")
        endif()
        file(STRINGS "${ROSETTA_GNU_APL_SOURCE_DIR}/config.h" apl_header_version REGEX "^#define PACKAGE_VERSION ")
        string(REGEX REPLACE ".*\"(.*)\".*" "\\1" apl_header_version "${apl_header_version}")
        execute_process(COMMAND "${ROSETTA_GNU_APL_EXECUTABLE}" --version OUTPUT_VARIABLE apl_version ERROR_QUIET)
        string(REGEX MATCH "Version / SVN: +([^ ]+)" apl_version "${apl_version}")
        if(NOT CMAKE_MATCH_1 STREQUAL apl_header_version)
            message(FATAL_ERROR "GNU APL ${CMAKE_MATCH_1} does not match the headers in ROSETTA_GNU_APL_SOURCE_DIR (${apl_header_version})")
        endif()
    endif()
    set(apl_modules demo verify)
    if(BUILD_TESTING)
        list(APPEND apl_modules names)
    endif()
    set(apl_scripts)
    foreach(example ${apl_modules})
        set(module apl_${example})
        if(example STREQUAL "demo")
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.hpp")
            set(policy POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.policy.json")
        elseif(example STREQUAL "verify")
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.hpp")
            set(policy POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.policy.json")
        else()
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/tests/apl_names.hpp")
            set(policy POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/apl_names.policy.json")
        endif()
        rosetta_generate_apl(HEADER "${header}" MODULE ${module} OUTPUT "${apl_generated}/${example}.cpp"
            GNU_APL_OUTPUT "${apl_dir}/wslib1/${module}.apl" DYALOG_OUTPUT "${apl_dir}/dyalog/${module}.apln"
            APL_LIBRARY_DIR "${apl_dir}/lib" CLANG "${ROSETTA_CLANG_EXECUTABLE}" ${policy})
        list(APPEND apl_scripts "${apl_dir}/wslib1/${module}.apl" "${apl_dir}/dyalog/${module}.apln")
        # libMODULE_apl.so is the plain C ABI Dyalog's ⎕NA calls; it is built
        # either way. Both libraries use .so, which GNU APL and Dyalog load on every platform.
        set(variants dyalog)
        if(ROSETTA_BUILD_GNU_APL_EXAMPLES)
            list(APPEND variants gnu)
        endif()
        foreach(variant ${variants})
            set(target rosetta_${variant}_apl_${example})
            add_library(${target} MODULE "${apl_generated}/${example}.cpp")
            set(suffix _apl)
            if(variant STREQUAL "gnu")
                set(suffix _gnu_apl)
                target_compile_definitions(${target} PRIVATE ROSETTA_GNU_APL HAVE_CONFIG_H)
                target_include_directories(${target} SYSTEM PRIVATE "${ROSETTA_GNU_APL_SOURCE_DIR}" "${ROSETTA_GNU_APL_SOURCE_DIR}/src")
                # The interpreter supplies GNU APL's symbols when it loads the library.
                if(APPLE)
                    target_link_options(${target} PRIVATE -undefined dynamic_lookup)
                endif()
            endif()
            set_target_properties(${target} PROPERTIES OUTPUT_NAME ${module}${suffix} PREFIX "lib" SUFFIX ".so"
                LIBRARY_OUTPUT_DIRECTORY "${apl_dir}/lib")
            target_compile_features(${target} PRIVATE cxx_std_17)
            rosetta_warnings(${target})
            if(example STREQUAL "verify")
                target_sources(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.cpp")
                target_compile_features(${target} PRIVATE cxx_std_20)
            endif()
        endforeach()
    endforeach()
    add_custom_target(rosetta_apl_scripts ALL DEPENDS ${apl_scripts})
    if(ROSETTA_BUILD_GNU_APL_EXAMPLES)
        # The interpreter reads the bindings and the banner, then the terminal.
        add_custom_target(apl-repl
            COMMAND "${ROSETTA_GNU_APL_EXECUTABLE}" --noSV --silent --noCIN -f "${apl_dir}/wslib1/apl_demo.apl"
                -f "${apl_dir}/wslib1/apl_verify.apl" -f "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.apl"
            DEPENDS rosetta_gnu_apl_demo rosetta_gnu_apl_verify rosetta_apl_scripts USES_TERMINAL VERBATIM)
    endif()
    if(BUILD_TESTING AND ROSETTA_BUILD_GNU_APL_EXAMPLES)
        foreach(family scalars names objects features exit)
            add_test(NAME gnu_apl_${family} COMMAND "${ROSETTA_GNU_APL_EXECUTABLE}" --script --noSV --noColor --silent
                -f "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gnu_apl.apl")
            set_tests_properties(gnu_apl_${family} PROPERTIES TIMEOUT 60
                ENVIRONMENT "APL_LIB_ROOT=${apl_dir};ROSETTA_APL_FAMILY=${family}"
                PASS_REGULAR_EXPRESSION "ROSETTA PASS ${family}" FAIL_REGULAR_EXPRESSION "FAIL|ERROR")
        endforeach()
        add_test(NAME gnu_apl_clear COMMAND "${ROSETTA_GNU_APL_EXECUTABLE}" --script --noSV --noColor --silent
            -f "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gnu_apl_clear.apl")
        set_tests_properties(gnu_apl_clear PROPERTIES TIMEOUT 60 ENVIRONMENT "APL_LIB_ROOT=${apl_dir}"
            PASS_REGULAR_EXPRESSION "ROSETTA PASS clear" FAIL_REGULAR_EXPRESSION "FAIL|ERROR")
    endif()
    if(BUILD_TESTING AND ROSETTA_BUILD_DYALOG_EXAMPLES)
        find_program(ROSETTA_DYALOG_SCRIPT_EXECUTABLE dyalogscript REQUIRED)
        foreach(family scalars names objects features)
            add_test(NAME dyalog_${family} COMMAND "${ROSETTA_DYALOG_SCRIPT_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dyalog.apls")
            set_tests_properties(dyalog_${family} PROPERTIES TIMEOUT 60
                ENVIRONMENT "ROSETTA_DYALOG_DIR=${apl_dir}/dyalog;ROSETTA_APL_FAMILY=${family}"
                PASS_REGULAR_EXPRESSION "ROSETTA PASS ${family}" FAIL_REGULAR_EXPRESSION "FAIL")
        endforeach()
    endif()
endif()
