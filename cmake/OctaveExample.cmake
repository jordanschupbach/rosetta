option(ROSETTA_BUILD_OCTAVE_EXAMPLES "Build and test GNU Octave native extensions (requires Octave 9+ headers/libraries)" OFF)
if(ROSETTA_BUILD_OCTAVE_EXAMPLES)
    find_program(ROSETTA_OCTAVE_EXECUTABLE NAMES octave-cli octave REQUIRED)
    find_program(ROSETTA_MKOCTFILE_EXECUTABLE NAMES mkoctfile REQUIRED)
    execute_process(COMMAND "${ROSETTA_MKOCTFILE_EXECUTABLE}" --version
        OUTPUT_VARIABLE octave_version_text OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    # Match the number after "version": the platform triple can contain one too
    # (aarch64-apple-darwin25.6.0).
    string(REGEX MATCH "version ([0-9]+\\.[0-9]+\\.[0-9]+)" octave_version_match "${octave_version_text}")
    set(octave_VERSION "${CMAKE_MATCH_1}")
    execute_process(COMMAND "${ROSETTA_OCTAVE_EXECUTABLE}" --version
        OUTPUT_VARIABLE octave_interpreter_text ERROR_QUIET COMMAND_ERROR_IS_FATAL ANY)
    string(REGEX MATCH "version ([0-9]+\\.[0-9]+\\.[0-9]+)" octave_interpreter_match "${octave_interpreter_text}")
    set(octave_interpreter_version "${CMAKE_MATCH_1}")
    if(NOT octave_VERSION OR NOT octave_VERSION VERSION_EQUAL octave_interpreter_version)
        message(FATAL_ERROR "Octave interpreter and mkoctfile versions must match")
    endif()
    foreach(setting OCTINCLUDEDIR OCTLIBDIR)
        execute_process(COMMAND "${ROSETTA_MKOCTFILE_EXECUTABLE}" -p ${setting}
            OUTPUT_VARIABLE octave_${setting} OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    endforeach()
    if(octave_VERSION VERSION_LESS 9)
        message(FATAL_ERROR "Octave examples require Octave 9 or newer")
    endif()
    get_filename_component(octave_include_parent "${octave_OCTINCLUDEDIR}" DIRECTORY)
    find_library(ROSETTA_OCTINTERP_LIBRARY NAMES octinterp PATHS "${octave_OCTLIBDIR}" NO_DEFAULT_PATH REQUIRED)
    find_library(ROSETTA_OCTAVE_LIBRARY NAMES octave PATHS "${octave_OCTLIBDIR}" NO_DEFAULT_PATH REQUIRED)
    find_package(Threads REQUIRED)
    set(octave_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/octave")
    rosetta_generate_octave(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.hpp"
        MODULE octave_demo OUTPUT "${octave_generated}/demo.cpp"
        CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.policy.json")
    add_library(rosetta_octave_demo MODULE "${octave_generated}/demo.cpp")
    rosetta_generate_octave(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.hpp"
        MODULE octave_verify OUTPUT "${octave_generated}/verify.cpp"
        CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.policy.json")
    add_library(rosetta_octave_verify MODULE "${octave_generated}/verify.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.cpp")
    rosetta_generate_octave(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/octave_values.hpp"
        MODULE octave_values OUTPUT "${octave_generated}/values.cpp"
        CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/octave_values.policy.json")
    add_library(rosetta_octave_values MODULE "${octave_generated}/values.cpp")
    foreach(example demo verify values)
        set_target_properties(rosetta_octave_${example} PROPERTIES PREFIX "" OUTPUT_NAME octave_${example} SUFFIX ".oct")
        target_include_directories(rosetta_octave_${example} SYSTEM PRIVATE "${octave_OCTINCLUDEDIR}" "${octave_include_parent}")
        target_link_libraries(rosetta_octave_${example} PRIVATE "${ROSETTA_OCTINTERP_LIBRARY}" "${ROSETTA_OCTAVE_LIBRARY}")
        target_compile_features(rosetta_octave_${example} PRIVATE cxx_std_17)
        rosetta_warnings(rosetta_octave_${example})
    endforeach()
    target_compile_features(rosetta_octave_verify PRIVATE cxx_std_20)
    target_link_libraries(rosetta_octave_verify PRIVATE Threads::Threads)
    set(octave_command "${ROSETTA_OCTAVE_EXECUTABLE}" --no-gui --no-init-file --no-site-file --no-history --quiet)
    add_custom_target(octave-repl
        COMMAND ${octave_command} --path "$<TARGET_FILE_DIR:rosetta_octave_demo>" --persist "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.m"
        DEPENDS rosetta_octave_demo rosetta_octave_verify USES_TERMINAL VERBATIM)
    if(BUILD_TESTING)
        foreach(family scalars objects features enums repl)
            add_test(NAME octave_${family} COMMAND ${octave_command}
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_octave.m" ${family}
                "$<TARGET_FILE_DIR:rosetta_octave_demo>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.m")
        endforeach()
        set_tests_properties(octave_features PROPERTIES TIMEOUT 20)
        # The full API (--octave-api full): python_features.hpp (split in two sources) and the names
        # test, each an oct-file __NAME__.oct beside its package folder +NAME.
        set(octave_full "${octave_generated}/full")
        rosetta_generate_octave(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp"
            MODULE features OUTPUT "${octave_full}/features/features.cpp" OCTAVE_API full PARTS 2
            CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json")
        add_library(rosetta_octave_full_features MODULE "${octave_full}/features/features.cpp" "${octave_full}/features/features_1.cpp")
        target_include_directories(rosetta_octave_full_features PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples")
        rosetta_generate_octave(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/octave_full_names.hpp"
            MODULE names OUTPUT "${octave_full}/names/names.cpp" OCTAVE_API full
            CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/octave_full_names.policy.json")
        add_library(rosetta_octave_full_names MODULE "${octave_full}/names/names.cpp")
        target_include_directories(rosetta_octave_full_names PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        foreach(example features names)
            set_target_properties(rosetta_octave_full_${example} PROPERTIES PREFIX "" OUTPUT_NAME __${example}__ SUFFIX ".oct"
                LIBRARY_OUTPUT_DIRECTORY "${octave_full}/${example}")
            target_include_directories(rosetta_octave_full_${example} SYSTEM PRIVATE "${octave_OCTINCLUDEDIR}" "${octave_include_parent}")
            target_link_libraries(rosetta_octave_full_${example} PRIVATE "${ROSETTA_OCTINTERP_LIBRARY}" "${ROSETTA_OCTAVE_LIBRARY}")
            target_compile_features(rosetta_octave_full_${example} PRIVATE cxx_std_17)
            rosetta_warnings(rosetta_octave_full_${example})
        endforeach()
        foreach(family features help)
            add_test(NAME octave_full_${family} COMMAND ${octave_command} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_octave_full.m"
                ${family} "${octave_full}/features" "${CMAKE_CURRENT_SOURCE_DIR}/tests/octave_full")
        endforeach()
        add_test(NAME octave_full_names COMMAND ${octave_command} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_octave_full_names.m"
            "${octave_full}/names")
        set_tests_properties(octave_full_features octave_full_help octave_full_names PROPERTIES TIMEOUT 120)
    endif()
endif()
