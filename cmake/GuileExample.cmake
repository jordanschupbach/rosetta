option(ROSETTA_BUILD_GUILE_EXAMPLES "Build and test Guile Scheme extensions (requires Guile 3.0 and pkg-config)" OFF)
if(ROSETTA_BUILD_GUILE_EXAMPLES)
    find_program(ROSETTA_GUILE_EXECUTABLE NAMES guile3.0 guile REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ROSETTA_GUILE REQUIRED IMPORTED_TARGET guile-3.0)
    find_package(Threads REQUIRED)
    set(guile_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/guile")
    set(guile_modules demo verify)
    if(BUILD_TESTING)
        list(APPEND guile_modules names)
    endif()
    foreach(example ${guile_modules})
        if(example STREQUAL "demo")
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.hpp")
            set(policy POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.policy.json")
        elseif(example STREQUAL "verify")
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.hpp")
            set(policy POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.policy.json")
        else()
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/tests/guile_names.hpp")
            set(policy)
        endif()
        # (use-modules (guile_demo)) finds guile_demo.scm on GUILE_LOAD_PATH;
        # it loads the extension from its own directory.
        rosetta_generate_guile(HEADER "${header}" MODULE guile_${example} OUTPUT "${guile_generated}/${example}.cpp"
            GUILE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/guile_${example}.scm" CLANG "${ROSETTA_CLANG_EXECUTABLE}" ${policy})
        add_library(rosetta_guile_${example} MODULE "${guile_generated}/${example}.cpp")
        set_target_properties(rosetta_guile_${example} PROPERTIES PREFIX "" OUTPUT_NAME guile_${example})
        target_link_libraries(rosetta_guile_${example} PRIVATE PkgConfig::ROSETTA_GUILE Threads::Threads)
        target_compile_features(rosetta_guile_${example} PRIVATE cxx_std_17)
        rosetta_warnings(rosetta_guile_${example})
    endforeach()
    target_sources(rosetta_guile_verify PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.cpp")
    target_compile_features(rosetta_guile_verify PRIVATE cxx_std_20)
    set(guile_command "${CMAKE_COMMAND}" -E env "GUILE_LOAD_PATH=${CMAKE_CURRENT_BINARY_DIR}" GUILE_AUTO_COMPILE=0
        "${ROSETTA_GUILE_EXECUTABLE}" --no-auto-compile)
    add_custom_target(guile-repl
        COMMAND ${guile_command} -l "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.scm"
        DEPENDS rosetta_guile_demo rosetta_guile_verify USES_TERMINAL VERBATIM)
    if(BUILD_TESTING)
        foreach(family scalars objects features repl)
            add_test(NAME guile_${family} COMMAND ${guile_command} -s "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_guile.scm"
                ${family} "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.scm")
            set_tests_properties(guile_${family} PROPERTIES TIMEOUT 45)
        endforeach()
    endif()

    # Full API (--guile-api full): GOOPS classes, generics, directors and docstrings. Each module
    # file (features.scm, names.scm) loads lib<module>_guile from its own directory; policy
    # submodules are written beside it (features/data.scm, names/stats.scm).
    set(guile_full_dir "${CMAKE_CURRENT_BINARY_DIR}/guile-full")
    set(guile_full_modules features)
    if(BUILD_TESTING)
        list(APPEND guile_full_modules names)
    endif()
    foreach(example ${guile_full_modules})
        if(example STREQUAL "features")
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp")
            set(policy "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json")
            set(include_dir "${CMAKE_CURRENT_SOURCE_DIR}/examples")
        else()
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/tests/guile_full_names.hpp")
            set(policy "${CMAKE_CURRENT_SOURCE_DIR}/tests/guile_full_names.policy.json")
            set(include_dir "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        endif()
        rosetta_generate_guile(HEADER "${header}" MODULE ${example} OUTPUT "${guile_generated}/full/${example}.cpp"
            GUILE_API full PARTS 2 GUILE_OUTPUT "${guile_full_dir}/${example}.scm" CLANG "${ROSETTA_CLANG_EXECUTABLE}"
            POLICY "${policy}")
        add_library(rosetta_guile_full_${example} MODULE "${guile_generated}/full/${example}.cpp" "${guile_generated}/full/${example}_1.cpp")
        set_target_properties(rosetta_guile_full_${example} PROPERTIES PREFIX "" OUTPUT_NAME lib${example}_guile
            LIBRARY_OUTPUT_DIRECTORY "${guile_full_dir}")
        target_include_directories(rosetta_guile_full_${example} PRIVATE "${include_dir}")
        target_link_libraries(rosetta_guile_full_${example} PRIVATE PkgConfig::ROSETTA_GUILE Threads::Threads)
        target_compile_features(rosetta_guile_full_${example} PRIVATE cxx_std_17)
        rosetta_warnings(rosetta_guile_full_${example})
        # Precompiled .go files (guild compile at build time); any compiler warning -- a shadowed
        # definition, an unbound variable -- fails the build.
        rosetta_compile_guile(TARGET rosetta_guile_full_${example}_go MODULE_FILE "${guile_full_dir}/${example}.scm"
            OUTPUT_DIR "${guile_full_dir}-ccache" DEPENDS rosetta_guile_full_${example} WARNINGS_AS_ERRORS)
    endforeach()
    if(TARGET rosetta_guile_full_names_go)
        # Both compile steps write guile-full-ccache; one at a time.
        add_dependencies(rosetta_guile_full_names_go rosetta_guile_full_features_go)
    endif()
    if(BUILD_TESTING)
        set(guile_full_command "${CMAKE_COMMAND}" -E env "GUILE_LOAD_PATH=${guile_full_dir}" GUILE_AUTO_COMPILE=0
            "${ROSETTA_GUILE_EXECUTABLE}" --no-auto-compile)
        add_test(NAME guile_full COMMAND ${guile_full_command} -s "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_guile_full.scm")
        add_test(NAME guile_full_names COMMAND ${guile_full_command} -s "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_guile_full_names.scm")
        add_test(NAME guile_full_docs COMMAND ${guile_full_command} -s "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_guile_full_docs.scm"
            "${guile_full_dir}/features.scm")
        # The same checks against the precompiled .go files (Guile's compiler rewrites some calls).
        add_test(NAME guile_full_compiled COMMAND "${CMAKE_COMMAND}" -E env "GUILE_LOAD_PATH=${guile_full_dir}"
            "GUILE_LOAD_COMPILED_PATH=${guile_full_dir}-ccache" GUILE_AUTO_COMPILE=0 "${ROSETTA_GUILE_EXECUTABLE}" --no-auto-compile
            -s "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_guile_full.scm" --compiled)
        set_tests_properties(guile_full guile_full_names guile_full_docs guile_full_compiled PROPERTIES TIMEOUT 180)
    endif()
endif()
