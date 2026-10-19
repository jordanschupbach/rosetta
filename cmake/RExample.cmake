# R is optional: generating bindings does not require R or R6 to be installed.
option(ROSETTA_BUILD_R_EXAMPLES "Build and test the R6 example (requires R and R6)" OFF)
if(ROSETTA_BUILD_R_EXAMPLES)
    find_program(ROSETTA_R_EXECUTABLE NAMES R REQUIRED)
    find_program(ROSETTA_RSCRIPT_EXECUTABLE NAMES Rscript REQUIRED)
    execute_process(COMMAND "${ROSETTA_RSCRIPT_EXECUTABLE}" --vanilla -e
        "if (!requireNamespace('R6', quietly = TRUE)) stop('Install the R6 package')"
        RESULT_VARIABLE r6_status)
    if(NOT r6_status EQUAL 0)
        message(FATAL_ERROR "The R6 example requires the R6 package in Rscript's library path.")
    endif()
    execute_process(COMMAND "${ROSETTA_R_EXECUTABLE}" CMD config --cppflags
        OUTPUT_VARIABLE r_cppflags OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${ROSETTA_R_EXECUTABLE}" RHOME
        OUTPUT_VARIABLE r_home OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    find_library(ROSETTA_R_LIBRARY NAMES R HINTS "${r_home}/lib" REQUIRED)
    separate_arguments(r_cppflags UNIX_COMMAND "${r_cppflags}")
    set(r_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/r")
    rosetta_generate_r(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.hpp"
        MODULE r_demo OBJECT_SYSTEM r6 OUTPUT "${r_generated}/r_demo.cpp" R_OUTPUT "${r_generated}/r_demo.R"
        CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.policy.json")
    add_library(rosetta_r_demo MODULE "${r_generated}/r_demo.cpp")
    target_compile_features(rosetta_r_demo PRIVATE cxx_std_17)
    target_compile_options(rosetta_r_demo PRIVATE ${r_cppflags})
    target_link_libraries(rosetta_r_demo PRIVATE "${ROSETTA_R_LIBRARY}")
    set_target_properties(rosetta_r_demo PROPERTIES PREFIX "" OUTPUT_NAME r_demo)
    # Native facades deliberately share the Lua example's target-independent libraries.
    find_package(Threads REQUIRED)
    rosetta_generate_r(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.hpp"
        MODULE r_verify OBJECT_SYSTEM r6 OUTPUT "${r_generated}/r_verify.cpp" R_OUTPUT "${r_generated}/r_verify.R"
        CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.policy.json")
    add_library(rosetta_r_verify MODULE "${r_generated}/r_verify.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.cpp")
    target_compile_features(rosetta_r_verify PRIVATE cxx_std_20)
    target_compile_options(rosetta_r_verify PRIVATE ${r_cppflags})
    target_link_libraries(rosetta_r_verify PRIVATE "${ROSETTA_R_LIBRARY}" Threads::Threads)
    set_target_properties(rosetta_r_verify PROPERTIES PREFIX "" OUTPUT_NAME r_verify)
    set(functional_interfaces)
    foreach(system s3 s4)
        foreach(example demo verify)
            if(example STREQUAL "demo")
                set(header r_demo)
            else()
                set(header r_verification)
            endif()
            rosetta_generate_r(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/${header}.hpp"
                MODULE r_${example} OBJECT_SYSTEM ${system}
                OUTPUT "${r_generated}/${system}/r_${example}.cpp" R_OUTPUT "${r_generated}/${system}/r_${example}.R"
                CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/${header}.policy.json")
            list(APPEND functional_interfaces "${r_generated}/${system}/r_${example}.cpp" "${r_generated}/${system}/r_${example}.R")
        endforeach()
    endforeach()
    # Interface-only generation reuses the identical, already compiled bridges.
    add_custom_target(rosetta_r_interfaces DEPENDS ${functional_interfaces})
    add_dependencies(rosetta_r_demo rosetta_r_interfaces)
    add_dependencies(rosetta_r_verify rosetta_r_interfaces)
    add_custom_target(r-repl
        COMMAND "${CMAKE_COMMAND}" -E env
            "R_PROFILE_USER=${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.R"
            "ROSETTA_R_MODULE=$<TARGET_FILE:rosetta_r_demo>"
            "ROSETTA_R_INTERFACE=${r_generated}/r_demo.R"
            "ROSETTA_R_VERIFY_MODULE=$<TARGET_FILE:rosetta_r_verify>"
            "ROSETTA_R_VERIFY_INTERFACE=${r_generated}/r_verify.R"
            "ROSETTA_R_INTERFACES=${r_generated}"
            "${ROSETTA_R_EXECUTABLE}" --no-save --no-restore --no-site-file --no-environ
        DEPENDS rosetta_r_demo rosetta_r_verify
        USES_TERMINAL VERBATIM)
    if(BUILD_TESTING)
        add_test(NAME r_integration COMMAND "${ROSETTA_RSCRIPT_EXECUTABLE}" --vanilla
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_r.R" "$<TARGET_FILE:rosetta_r_demo>" "${r_generated}/r_demo.R")
        foreach(family scalars features)
            add_test(NAME r_${family} COMMAND "${ROSETTA_RSCRIPT_EXECUTABLE}" --vanilla
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_r_${family}.R" "$<TARGET_FILE:rosetta_r_verify>"
                "${r_generated}/r_verify.R" "${CMAKE_CURRENT_SOURCE_DIR}/tests/r_helpers.R")
        endforeach()
        set_tests_properties(r_features PROPERTIES TIMEOUT 20)
        foreach(system s3 s4)
            add_test(NAME r_${system}_scalars COMMAND "${ROSETTA_RSCRIPT_EXECUTABLE}" --vanilla
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_r_scalars.R" "$<TARGET_FILE:rosetta_r_verify>"
                "${r_generated}/${system}/r_verify.R" "${CMAKE_CURRENT_SOURCE_DIR}/tests/r_helpers.R")
            add_test(NAME r_${system}_classes COMMAND "${ROSETTA_RSCRIPT_EXECUTABLE}" --vanilla
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_r_object_systems.R"
                "$<TARGET_FILE:rosetta_r_demo>" "${r_generated}/${system}/r_demo.R"
                "$<TARGET_FILE:rosetta_r_verify>" "${r_generated}/${system}/r_verify.R" "${system}")
            set_tests_properties(r_${system}_classes PROPERTIES TIMEOUT 20)
        endforeach()
        add_test(NAME r_repl_startup COMMAND "${ROSETTA_RSCRIPT_EXECUTABLE}" --vanilla
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_r_repl.R" "$<TARGET_FILE:rosetta_r_demo>" "${r_generated}/r_demo.R"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.R" "$<TARGET_FILE:rosetta_r_verify>" "${r_generated}/r_verify.R" "${r_generated}")
    endif()
endif()
