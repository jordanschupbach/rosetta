option(ROSETTA_BUILD_PYTHON_EXAMPLES "Build and test CPython native extensions (requires CPython 3.12+ headers)" OFF)
if(ROSETTA_BUILD_PYTHON_EXAMPLES)
    find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter Development.Module)
    find_package(Threads REQUIRED)
    set(python_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/python")
    # Reuse the native-only examples/policies already verified through R.
    rosetta_generate_python(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.hpp"
        MODULE python_demo OUTPUT "${python_generated}/demo.cpp"
        CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.policy.json")
    Python3_add_library(rosetta_python_demo MODULE WITH_SOABI "${python_generated}/demo.cpp")
    set_target_properties(rosetta_python_demo PROPERTIES OUTPUT_NAME python_demo)
    target_compile_features(rosetta_python_demo PRIVATE cxx_std_17)
    rosetta_generate_python(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.hpp"
        MODULE python_verify OUTPUT "${python_generated}/verify.cpp"
        CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.policy.json")
    Python3_add_library(rosetta_python_verify MODULE WITH_SOABI "${python_generated}/verify.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.cpp")
    set_target_properties(rosetta_python_verify PROPERTIES OUTPUT_NAME python_verify)
    target_compile_features(rosetta_python_verify PRIVATE cxx_std_20)
    target_link_libraries(rosetta_python_verify PRIVATE Threads::Threads)
    rosetta_generate_python(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp"
        MODULE python_features OUTPUT "${python_generated}/features.cpp" PYTHON_PARTS 3
        CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json")
    # Split into three sources to cover --python-parts.
    Python3_add_library(rosetta_python_features MODULE WITH_SOABI "${python_generated}/features.cpp"
        "${python_generated}/features_1.cpp" "${python_generated}/features_2.cpp")
    set_target_properties(rosetta_python_features PROPERTIES OUTPUT_NAME python_features)
    target_compile_features(rosetta_python_features PRIVATE cxx_std_17)
    add_custom_target(python-repl
        COMMAND "${Python3_EXECUTABLE}" -I -i "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.py"
            "$<TARGET_FILE:rosetta_python_demo>" "$<TARGET_FILE:rosetta_python_verify>"
        DEPENDS rosetta_python_demo rosetta_python_verify USES_TERMINAL VERBATIM)
    if(BUILD_TESTING)
        foreach(family scalars objects features)
            add_test(NAME python_${family} COMMAND "${Python3_EXECUTABLE}" -I
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_python.py" ${family}
                "$<TARGET_FILE:rosetta_python_demo>" "$<TARGET_FILE:rosetta_python_verify>")
        endforeach()
        set_tests_properties(python_features PROPERTIES TIMEOUT 20)
        add_test(NAME python_library_features COMMAND "${Python3_EXECUTABLE}" -I
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_python_library.py" "$<TARGET_FILE:rosetta_python_features>")
        add_test(NAME python_repl_startup COMMAND "${Python3_EXECUTABLE}" -I
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_python.py" repl
            "$<TARGET_FILE:rosetta_python_demo>" "$<TARGET_FILE:rosetta_python_verify>"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.py")
    endif()
endif()
