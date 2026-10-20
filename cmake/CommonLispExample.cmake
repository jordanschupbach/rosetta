option(ROSETTA_BUILD_COMMON_LISP_EXAMPLES "Build and test Common Lisp CFFI bindings (requires SBCL with CFFI and trivial-garbage)" OFF)
if(ROSETTA_BUILD_COMMON_LISP_EXAMPLES)
    find_program(ROSETTA_SBCL_EXECUTABLE NAMES sbcl REQUIRED)
    set(lisp_command "${ROSETTA_SBCL_EXECUTABLE}" --noinform --no-sysinit --no-userinit)
    # The generated interface loads these through ASDF; fail at configure time, not in tests.
    execute_process(COMMAND ${lisp_command} --non-interactive --eval "(require \"asdf\")"
            --eval "(asdf:load-system \"cffi\")" --eval "(asdf:load-system \"trivial-garbage\")"
        RESULT_VARIABLE lisp_status OUTPUT_QUIET ERROR_VARIABLE lisp_error)
    if(NOT lisp_status EQUAL 0)
        message(FATAL_ERROR "Common Lisp examples require ASDF systems cffi and trivial-garbage:\n${lisp_error}")
    endif()
    find_package(Threads REQUIRED)
    set(lisp_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/common-lisp")
    foreach(example demo verify)
        if(example STREQUAL "demo")
            set(native r_demo)
        else()
            set(native r_verification)
        endif()
        rosetta_generate_common_lisp(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/${native}.hpp"
            MODULE lisp_${example} OUTPUT "${lisp_generated}/${example}.cpp"
            LISP_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lisp_${example}.lisp"
            CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/${native}.policy.json")
        add_library(rosetta_lisp_${example} SHARED "${lisp_generated}/${example}.cpp")
        set_target_properties(rosetta_lisp_${example} PROPERTIES PREFIX "" OUTPUT_NAME lisp_${example})
        target_compile_features(rosetta_lisp_${example} PRIVATE cxx_std_17)
        target_link_libraries(rosetta_lisp_${example} PRIVATE Threads::Threads)
        rosetta_warnings(rosetta_lisp_${example})
    endforeach()
    target_sources(rosetta_lisp_verify PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.cpp")
    target_compile_features(rosetta_lisp_verify PRIVATE cxx_std_20)
    add_custom_target(lisp-repl
        COMMAND "${CMAKE_COMMAND}" -E env "ROSETTA_LISP_BUILD=${CMAKE_CURRENT_BINARY_DIR}"
            ${lisp_command} --load "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.lisp"
        DEPENDS rosetta_lisp_demo rosetta_lisp_verify USES_TERMINAL VERBATIM)
    if(BUILD_TESTING)
        rosetta_generate_common_lisp(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/lisp_names.hpp"
            MODULE lisp_names OUTPUT "${lisp_generated}/names.cpp"
            LISP_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lisp_names.lisp"
            CLANG "${ROSETTA_CLANG_EXECUTABLE}")
        add_library(rosetta_lisp_names SHARED "${lisp_generated}/names.cpp")
        set_target_properties(rosetta_lisp_names PROPERTIES PREFIX "" OUTPUT_NAME lisp_names)
        target_compile_features(rosetta_lisp_names PRIVATE cxx_std_17)
        target_link_libraries(rosetta_lisp_names PRIVATE Threads::Threads)
        rosetta_warnings(rosetta_lisp_names)
        foreach(family scalars objects features repl)
            add_test(NAME common_lisp_${family} COMMAND ${lisp_command} --non-interactive
                --load "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_common_lisp.lisp")
            set_tests_properties(common_lisp_${family} PROPERTIES TIMEOUT 45 ENVIRONMENT
                "ROSETTA_LISP_FAMILY=${family};ROSETTA_LISP_BUILD=${CMAKE_CURRENT_BINARY_DIR};ROSETTA_LISP_REPL=${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.lisp")
        endforeach()
    endif()
endif()
