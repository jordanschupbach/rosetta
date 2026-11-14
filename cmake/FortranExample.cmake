option(ROSETTA_BUILD_FORTRAN_EXAMPLES "Build and test Fortran ISO_C_BINDING interfaces" OFF)
if(ROSETTA_BUILD_FORTRAN_EXAMPLES)
    enable_language(Fortran)
    find_package(Threads REQUIRED)
    set(fortran_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/fortran")
    set(fortran_modules "${CMAKE_CURRENT_BINARY_DIR}/fortran-modules")
    foreach(example demo verify)
        if(example STREQUAL "demo")
            set(native r_demo)
            set(fortran_name FortranDemo)
        else()
            set(native r_verification)
            set(fortran_name FortranVerify)
        endif()
        rosetta_generate_fortran(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/${native}.hpp"
            MODULE ${fortran_name} OUTPUT "${fortran_generated}/${example}.cpp"
            CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/${native}.policy.json")
        add_library(rosetta_fortran_${example}_native SHARED "${fortran_generated}/${example}.cpp")
        target_compile_features(rosetta_fortran_${example}_native PRIVATE cxx_std_17)
        target_link_libraries(rosetta_fortran_${example}_native PRIVATE Threads::Threads)
        rosetta_warnings(rosetta_fortran_${example}_native)
        add_library(rosetta_fortran_${example} STATIC "${fortran_generated}/${fortran_name}.f90")
        set_target_properties(rosetta_fortran_${example} PROPERTIES
            Fortran_MODULE_DIRECTORY "${fortran_modules}" Fortran_STANDARD 2018 Fortran_STANDARD_REQUIRED YES)
        target_include_directories(rosetta_fortran_${example} PUBLIC "${fortran_modules}")
        target_link_libraries(rosetta_fortran_${example} PUBLIC rosetta_fortran_${example}_native)
        if(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
            target_compile_options(rosetta_fortran_${example} PRIVATE -Wall -Wextra -Werror -fcheck=all -std=f2018)
        endif()
    endforeach()
    target_sources(rosetta_fortran_verify_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.cpp")
    target_compile_features(rosetta_fortran_verify_native PRIVATE cxx_std_20)
    function(rosetta_fortran_program target source)
        add_executable(${target} "${source}")
        set_target_properties(${target} PROPERTIES Fortran_STANDARD 2018 Fortran_STANDARD_REQUIRED YES)
        target_link_libraries(${target} PRIVATE rosetta_fortran_demo rosetta_fortran_verify)
        if(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -fcheck=all -std=f2018)
        endif()
    endfunction()
    rosetta_fortran_program(rosetta_fortran_example "${CMAKE_CURRENT_SOURCE_DIR}/examples/FortranExample.f90")
    add_custom_target(fortran-demo COMMAND rosetta_fortran_example DEPENDS rosetta_fortran_example USES_TERMINAL VERBATIM)
    if(BUILD_TESTING)
        rosetta_generate_fortran(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/fortran_values.hpp"
            MODULE FortranValues OUTPUT "${fortran_generated}/values.cpp" CLANG "${ROSETTA_CLANG_EXECUTABLE}")
        add_library(rosetta_fortran_values_native SHARED "${fortran_generated}/values.cpp")
        target_compile_features(rosetta_fortran_values_native PRIVATE cxx_std_17)
        target_link_libraries(rosetta_fortran_values_native PRIVATE Threads::Threads)
        rosetta_warnings(rosetta_fortran_values_native)
        add_library(rosetta_fortran_values STATIC "${fortran_generated}/FortranValues.f90")
        set_target_properties(rosetta_fortran_values PROPERTIES Fortran_MODULE_DIRECTORY "${fortran_modules}"
            Fortran_STANDARD 2018 Fortran_STANDARD_REQUIRED YES)
        target_include_directories(rosetta_fortran_values PUBLIC "${fortran_modules}")
        target_link_libraries(rosetta_fortran_values PUBLIC rosetta_fortran_values_native)
        if(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
            target_compile_options(rosetta_fortran_values PRIVATE -Wall -Wextra -Werror -fcheck=all -std=f2018)
        endif()
        rosetta_fortran_program(rosetta_fortran_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fortran.f90")
        target_link_libraries(rosetta_fortran_tests PRIVATE rosetta_fortran_values)
        foreach(family scalars objects features fatal)
            add_test(NAME fortran_${family} COMMAND rosetta_fortran_tests ${family})
            set_tests_properties(fortran_${family} PROPERTIES TIMEOUT 30)
        endforeach()
        set_tests_properties(fortran_fatal PROPERTIES WILL_FAIL TRUE)
        add_test(NAME fortran_example COMMAND rosetta_fortran_example)

        # The full Fortran API (--fortran-api full) on the example shared with Python, Lua, Lisp,
        # Julia and C#: a shared native library, the generated modules and the test program.
        set(fortran_full "${fortran_generated}/full")
        set(fortran_full_modules "${CMAKE_CURRENT_BINARY_DIR}/fortran-full-modules")
        rosetta_generate_fortran(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp" MODULE features
            CLANG "${ROSETTA_CLANG_EXECUTABLE}" OUTPUT "${fortran_full}/features.cpp"
            FORTRAN_OUTPUT "${fortran_full}/features.f90" FORTRAN_API full PARTS 2
            POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json")
        add_library(rosetta_fortran_full_features_native SHARED "${fortran_full}/features.cpp" "${fortran_full}/features_1.cpp")
        target_compile_features(rosetta_fortran_full_features_native PRIVATE cxx_std_17)
        target_include_directories(rosetta_fortran_full_features_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples")
        add_library(rosetta_fortran_full_features STATIC "${fortran_full}/features.f90")
        set_target_properties(rosetta_fortran_full_features PROPERTIES Fortran_MODULE_DIRECTORY "${fortran_full_modules}"
            Fortran_STANDARD 2018 Fortran_STANDARD_REQUIRED YES)
        target_include_directories(rosetta_fortran_full_features PUBLIC "${fortran_full_modules}")
        target_link_libraries(rosetta_fortran_full_features PUBLIC rosetta_fortran_full_features_native)
        add_executable(rosetta_fortran_full_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fortran_full.f90")
        set_target_properties(rosetta_fortran_full_tests PROPERTIES Fortran_STANDARD 2018 Fortran_STANDARD_REQUIRED YES
            Fortran_MODULE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fortran-full-test-modules")
        target_link_libraries(rosetta_fortran_full_tests PRIVATE rosetta_fortran_full_features)
        if(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
            target_compile_options(rosetta_fortran_full_features PRIVATE -Wall -Wextra -Werror -fcheck=all -std=f2018)
            target_compile_options(rosetta_fortran_full_tests PRIVATE -Wall -Wextra -Wno-compare-reals -Werror -fcheck=all -std=f2018)
        endif()
        add_test(NAME fortran_full_features COMMAND rosetta_fortran_full_tests "${fortran_full}/features.f90")
        set_tests_properties(fortran_full_features PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "Fortran full API passed")

        # Names colliding with Fortran keywords, intrinsics and generated names, 64-bit integers and
        # overloads Fortran's generic rules reject (reported and dropped: --skip-unsupported).
        add_custom_command(OUTPUT "${fortran_full}/names.cpp" "${fortran_full}/names.f90" "${fortran_full}/names.log"
            COMMAND rosetta "${CMAKE_CURRENT_SOURCE_DIR}/tests/fortran_full_names.hpp" --language fortran --fortran-api full
                --module names --clang "${ROSETTA_CLANG_EXECUTABLE}" -o "${fortran_full}/names.cpp"
                --fortran-output "${fortran_full}/names.f90" --skip-unsupported
                --policy "${CMAKE_CURRENT_SOURCE_DIR}/tests/fortran_full_names.policy.json" 2> "${fortran_full}/names.log"
            DEPENDS rosetta "${CMAKE_CURRENT_SOURCE_DIR}/tests/fortran_full_names.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fortran_full_names.policy.json"
            COMMENT "Generating Fortran bindings: names (full API)")
        add_library(rosetta_fortran_full_names_native SHARED "${fortran_full}/names.cpp")
        target_compile_features(rosetta_fortran_full_names_native PRIVATE cxx_std_17)
        target_include_directories(rosetta_fortran_full_names_native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        add_library(rosetta_fortran_full_names STATIC "${fortran_full}/names.f90")
        set_target_properties(rosetta_fortran_full_names PROPERTIES Fortran_MODULE_DIRECTORY "${fortran_full_modules}"
            Fortran_STANDARD 2018 Fortran_STANDARD_REQUIRED YES)
        target_include_directories(rosetta_fortran_full_names PUBLIC "${fortran_full_modules}")
        target_link_libraries(rosetta_fortran_full_names PUBLIC rosetta_fortran_full_names_native)
        add_executable(rosetta_fortran_names_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fortran_names.f90")
        set_target_properties(rosetta_fortran_names_tests PROPERTIES Fortran_STANDARD 2018 Fortran_STANDARD_REQUIRED YES
            Fortran_MODULE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fortran-names-test-modules")
        target_link_libraries(rosetta_fortran_names_tests PRIVATE rosetta_fortran_full_names)
        if(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
            target_compile_options(rosetta_fortran_full_names PRIVATE -Wall -Wextra -Werror -fcheck=all -std=f2018)
            target_compile_options(rosetta_fortran_names_tests PRIVATE -Wall -Wextra -Werror -fcheck=all -std=f2018)
        endif()
        add_test(NAME fortran_full_names COMMAND rosetta_fortran_names_tests)
        set_tests_properties(fortran_full_names PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "Fortran names passed")
        # The ambiguous and mixed overloads are reported.
        add_test(NAME fortran_full_names_diagnostics COMMAND "${CMAKE_COMMAND}" -E cat "${fortran_full}/names.log")
        set_tests_properties(fortran_full_names_diagnostics PROPERTIES
            PASS_REGULAR_EXPRESSION "which: overload .*ambiguous with another under Fortran's generic rules; dropped")
    endif()
endif()
