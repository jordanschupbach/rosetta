option(ROSETTA_BUILD_JULIA_EXAMPLES "Build and test Julia ccall bindings (requires Julia 1.10+)" OFF)
if(ROSETTA_BUILD_JULIA_EXAMPLES)
    find_program(ROSETTA_JULIA_EXECUTABLE NAMES julia REQUIRED)
    execute_process(COMMAND "${ROSETTA_JULIA_EXECUTABLE}" --startup-file=no --history-file=no -e "print(VERSION)"
        OUTPUT_VARIABLE julia_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    if(julia_VERSION VERSION_LESS 1.10)
        message(FATAL_ERROR "Julia examples require Julia 1.10 or newer")
    endif()
    find_package(Threads REQUIRED)
    set(julia_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/julia")
    foreach(example demo verify)
        if(example STREQUAL "demo")
            set(native r_demo)
        else()
            set(native r_verification)
        endif()
        rosetta_generate_julia(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/${native}.hpp"
            MODULE julia_${example} OUTPUT "${julia_generated}/${example}.cpp"
            JULIA_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/julia_${example}.jl"
            CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/${native}.policy.json")
        add_library(rosetta_julia_${example} SHARED "${julia_generated}/${example}.cpp")
        set_target_properties(rosetta_julia_${example} PROPERTIES PREFIX "" OUTPUT_NAME julia_${example})
        target_compile_features(rosetta_julia_${example} PRIVATE cxx_std_17)
        target_link_libraries(rosetta_julia_${example} PRIVATE Threads::Threads)
        rosetta_warnings(rosetta_julia_${example})
    endforeach()
    target_sources(rosetta_julia_verify PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.cpp")
    target_compile_features(rosetta_julia_verify PRIVATE cxx_std_20)
    set(julia_command "${ROSETTA_JULIA_EXECUTABLE}" --startup-file=no --history-file=no)
    add_custom_target(julia-repl
        COMMAND ${julia_command} -i "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.jl" "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS rosetta_julia_demo rosetta_julia_verify USES_TERMINAL VERBATIM)
    if(BUILD_TESTING)
        rosetta_generate_julia(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/julia_names.hpp"
            MODULE julia_names OUTPUT "${julia_generated}/names.cpp"
            JULIA_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/julia_names.jl"
            CLANG "${ROSETTA_CLANG_EXECUTABLE}")
        add_library(rosetta_julia_names SHARED "${julia_generated}/names.cpp")
        set_target_properties(rosetta_julia_names PROPERTIES PREFIX "" OUTPUT_NAME julia_names)
        target_compile_features(rosetta_julia_names PRIVATE cxx_std_17)
        target_link_libraries(rosetta_julia_names PRIVATE Threads::Threads)
        rosetta_warnings(rosetta_julia_names)
        foreach(family scalars objects features repl)
            add_test(NAME julia_${family} COMMAND ${julia_command} --threads=2
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_julia.jl" ${family} "${CMAKE_CURRENT_BINARY_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.jl")
            set_tests_properties(julia_${family} PROPERTIES
                ENVIRONMENT "JULIA_DEPOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/julia-depot" TIMEOUT 45)
        endforeach()
    endif()
endif()
