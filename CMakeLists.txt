cmake_minimum_required(VERSION 3.20)
project(rosetta VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
include(GNUInstallDirs)
include(cmake/Rosetta.cmake)

if(NOT UNIX)
    message(FATAL_ERROR "The compiler process adapter currently requires POSIX (Linux/macOS).")
endif()

find_package(nlohmann_json 3.9 CONFIG REQUIRED)
find_program(ROSETTA_CLANG_EXECUTABLE NAMES clang++ REQUIRED)
option(ROSETTA_BUILD_EXAMPLES "Generate and compile the Lua example" ON)
option(ROSETTA_WARNINGS_AS_ERRORS "Treat Rosetta compiler warnings as errors" OFF)

function(rosetta_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(ROSETTA_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()

add_library(rosetta_core
    src/model.cpp
    src/process.cpp
    src/frontend.cpp
    src/lua.cpp
    src/r.cpp
    src/python.cpp
    src/ruby.cpp
    src/octave.cpp
    src/julia.cpp
    src/cli.cpp
    src/policy.cpp
    src/compilation_database.cpp
    src/dependencies.cpp
)
target_compile_features(rosetta_core PUBLIC cxx_std_17)
target_include_directories(rosetta_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(rosetta_core PRIVATE nlohmann_json::nlohmann_json)
rosetta_warnings(rosetta_core)

add_executable(rosetta src/main.cpp)
target_link_libraries(rosetta PRIVATE rosetta_core)
rosetta_warnings(rosetta)
install(TARGETS rosetta RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES cmake/Rosetta.cmake DESTINATION ${CMAKE_INSTALL_DATADIR}/rosetta/cmake)
include(cmake/RExample.cmake)
include(cmake/PythonExample.cmake)
include(cmake/RubyExample.cmake)
include(cmake/OctaveExample.cmake)
include(cmake/JuliaExample.cmake)

if(BUILD_TESTING)
    add_executable(rosetta_julia_capability_tests tests/test_julia_capabilities.cpp)
    target_link_libraries(rosetta_julia_capability_tests PRIVATE rosetta_core)
    target_include_directories(rosetta_julia_capability_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    rosetta_warnings(rosetta_julia_capability_tests)
    add_test(NAME julia_capabilities COMMAND rosetta_julia_capability_tests "${ROSETTA_CLANG_EXECUTABLE}")
    add_executable(rosetta_octave_capability_tests tests/test_octave_capabilities.cpp)
    target_link_libraries(rosetta_octave_capability_tests PRIVATE rosetta_core)
    target_include_directories(rosetta_octave_capability_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    rosetta_warnings(rosetta_octave_capability_tests)
    add_test(NAME octave_capabilities COMMAND rosetta_octave_capability_tests "${ROSETTA_CLANG_EXECUTABLE}")
    add_executable(rosetta_ruby_capability_tests tests/test_ruby_capabilities.cpp)
    target_link_libraries(rosetta_ruby_capability_tests PRIVATE rosetta_core)
    target_include_directories(rosetta_ruby_capability_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    rosetta_warnings(rosetta_ruby_capability_tests)
    add_test(NAME ruby_capabilities COMMAND rosetta_ruby_capability_tests "${ROSETTA_CLANG_EXECUTABLE}")
    add_executable(rosetta_python_capability_tests tests/test_python_capabilities.cpp)
    target_link_libraries(rosetta_python_capability_tests PRIVATE rosetta_core)
    target_include_directories(rosetta_python_capability_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    rosetta_warnings(rosetta_python_capability_tests)
    add_test(NAME python_capabilities COMMAND rosetta_python_capability_tests "${ROSETTA_CLANG_EXECUTABLE}")
    add_executable(rosetta_r_capability_tests tests/test_r_capabilities.cpp)
    target_link_libraries(rosetta_r_capability_tests PRIVATE rosetta_core)
    target_include_directories(rosetta_r_capability_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    rosetta_warnings(rosetta_r_capability_tests)
    add_test(NAME r_capabilities COMMAND rosetta_r_capability_tests "${ROSETTA_CLANG_EXECUTABLE}")
    add_executable(rosetta_tests tests/test_generator.cpp)
    target_link_libraries(rosetta_tests PRIVATE rosetta_core nlohmann_json::nlohmann_json)
    target_include_directories(rosetta_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    rosetta_warnings(rosetta_tests)
    add_test(NAME generator COMMAND rosetta_tests "${ROSETTA_CLANG_EXECUTABLE}")
    add_executable(rosetta_compilation_tests tests/test_compilation_database.cpp)
    target_link_libraries(rosetta_compilation_tests PRIVATE rosetta_core nlohmann_json::nlohmann_json)
    target_include_directories(rosetta_compilation_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    rosetta_warnings(rosetta_compilation_tests)
    add_test(NAME compilation_database COMMAND rosetta_compilation_tests "${ROSETTA_CLANG_EXECUTABLE}")
    add_executable(rosetta_dependency_tests tests/test_dependencies.cpp)
    target_link_libraries(rosetta_dependency_tests PRIVATE rosetta_core nlohmann_json::nlohmann_json)
    target_include_directories(rosetta_dependency_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    rosetta_warnings(rosetta_dependency_tests)
    add_dependencies(rosetta_dependency_tests rosetta)
    find_program(ROSETTA_NINJA_EXECUTABLE NAMES ninja)
    if(ROSETTA_NINJA_EXECUTABLE)
        add_test(NAME dependencies COMMAND rosetta_dependency_tests "${ROSETTA_CLANG_EXECUTABLE}"
            "$<TARGET_FILE:rosetta>" "${CMAKE_COMMAND}" "${ROSETTA_NINJA_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Rosetta.cmake")
    else()
        add_test(NAME dependencies COMMAND rosetta_dependency_tests "${ROSETTA_CLANG_EXECUTABLE}")
    endif()
    add_test(NAME cli_help COMMAND rosetta --help)
    set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: rosetta")
endif()

if(ROSETTA_BUILD_EXAMPLES)
    include(cmake/NativeModuleExample.cmake)
    find_package(Threads REQUIRED)
    # Keep native compiler settings private: the generated bridge needs exceptions.
    add_library(rosetta_native_abi_objects OBJECT examples/native_abi.cpp)
    target_compile_features(rosetta_native_abi_objects PRIVATE cxx_std_17)
    target_compile_options(rosetta_native_abi_objects PRIVATE -fno-exceptions -fno-rtti -fno-sanitize=vptr)
    set_target_properties(rosetta_native_abi_objects PROPERTIES POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    rosetta_warnings(rosetta_native_abi_objects)
    add_library(rosetta_native_abi_static STATIC $<TARGET_OBJECTS:rosetta_native_abi_objects>)
    add_library(rosetta_native_abi_shared SHARED $<TARGET_OBJECTS:rosetta_native_abi_objects>)
    if(BUILD_TESTING)
        add_executable(rosetta_concurrency_tests tests/test_native_concurrency.cpp)
        target_compile_features(rosetta_concurrency_tests PRIVATE cxx_std_17)
        target_include_directories(rosetta_concurrency_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples")
        target_link_libraries(rosetta_concurrency_tests PRIVATE Threads::Threads)
        rosetta_warnings(rosetta_concurrency_tests)
        add_test(NAME native_concurrency COMMAND rosetta_concurrency_tests)
    endif()
    find_package(Lua 5.4 REQUIRED)
    if(NOT LUA_VERSION_STRING VERSION_LESS 5.5)
        message(FATAL_ERROR "The Lua backend currently supports Lua 5.4 only.")
    endif()
    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    rosetta_generate_lua(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo.hpp" MODULE demo
        CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo.policy.json"
        OUTPUT "${generated_dir}/demo_bindings.cpp" IR "${generated_dir}/demo.json"
        DEPFILE "${generated_dir}/demo.d" FLAGS -std=c++20)
    add_library(rosetta_exchange SHARED examples/interop/shared_record.cpp)
    target_compile_features(rosetta_exchange PRIVATE cxx_std_17)
    target_link_libraries(rosetta_exchange PRIVATE Threads::Threads)
    rosetta_warnings(rosetta_exchange)
    foreach(side a b)
        rosetta_generate_lua(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/interop/${side}.hpp" MODULE "interop_${side}"
            CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/interop/${side}.policy.json"
            OUTPUT "${generated_dir}/interop_${side}.cpp" DEPFILE "${generated_dir}/interop_${side}.d")
        add_library(rosetta_interop_${side} MODULE "${generated_dir}/interop_${side}.cpp")
        target_compile_features(rosetta_interop_${side} PRIVATE cxx_std_17)
        target_include_directories(rosetta_interop_${side} PRIVATE "${LUA_INCLUDE_DIR}")
        target_link_libraries(rosetta_interop_${side} PRIVATE ${LUA_LIBRARIES} rosetta_exchange)
        set_target_properties(rosetta_interop_${side} PROPERTIES PREFIX "" OUTPUT_NAME "interop_${side}")
        rosetta_warnings(rosetta_interop_${side})
    endforeach()
    add_library(rosetta_demo MODULE "${generated_dir}/demo_bindings.cpp" examples/demo.cpp examples/compile_time.cpp examples/buffer.cpp examples/calls.cpp examples/errors.cpp examples/ownership.cpp examples/coroutines.cpp examples/unicode.cpp examples/numerics.cpp examples/chrono.cpp examples/products.cpp examples/enum_flags.cpp examples/enum_values.cpp examples/nested.cpp examples/maps.cpp examples/variants.cpp examples/filesystem.cpp examples/containers.cpp examples/names.cpp examples/worklists.cpp examples/concurrency.cpp examples/members.cpp examples/native_boundary.cpp examples/native_forms.cpp examples/callables.cpp examples/class_callbacks.cpp examples/awaitables.cpp)
    target_compile_features(rosetta_demo PRIVATE cxx_std_20)
    target_include_directories(rosetta_demo PRIVATE "${LUA_INCLUDE_DIR}")
    target_link_libraries(rosetta_demo PRIVATE ${LUA_LIBRARIES} Threads::Threads rosetta_native_abi_shared rosetta_module_native rosetta_pch_native)
    set_target_properties(rosetta_demo PROPERTIES PREFIX "" OUTPUT_NAME demo)
    rosetta_warnings(rosetta_demo)
    find_program(ROSETTA_LUA_EXECUTABLE NAMES lua5.4 lua54 lua REQUIRED)
    execute_process(COMMAND "${ROSETTA_LUA_EXECUTABLE}" -e "assert(_VERSION == 'Lua 5.4')"
        RESULT_VARIABLE lua_version_status ERROR_VARIABLE lua_version_error)
    if(NOT lua_version_status EQUAL 0)
        message(FATAL_ERROR "ROSETTA_LUA_EXECUTABLE must be Lua 5.4: ${lua_version_error}")
    endif()
    set(lua_demo_search_path "$<TARGET_FILE_DIR:rosetta_demo>/?$<TARGET_FILE_SUFFIX:rosetta_demo>")
    add_custom_target(lua-repl
        COMMAND "${ROSETTA_LUA_EXECUTABLE}" -i "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.lua"
            "${lua_demo_search_path}"
        DEPENDS rosetta_demo rosetta_interop_a rosetta_interop_b
        USES_TERMINAL
        VERBATIM
        COMMENT "Starting Lua with the generated demo module loaded"
    )
    if(BUILD_TESTING)
        add_executable(rosetta_enum_policy_tests tests/test_enum_flags.cpp)
        target_link_libraries(rosetta_enum_policy_tests PRIVATE rosetta_core)
        target_include_directories(rosetta_enum_policy_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        rosetta_warnings(rosetta_enum_policy_tests)
        add_test(NAME enum_flag_compilation COMMAND rosetta_enum_policy_tests "${ROSETTA_CLANG_EXECUTABLE}" "${LUA_INCLUDE_DIR}")
        add_executable(rosetta_numeric_capability_tests tests/test_numeric_capability.cpp)
        target_include_directories(rosetta_numeric_capability_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples")
        target_compile_features(rosetta_numeric_capability_tests PRIVATE cxx_std_20)
        rosetta_warnings(rosetta_numeric_capability_tests)
        add_test(NAME numeric_capability COMMAND rosetta_numeric_capability_tests)
        rosetta_generate_lua(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/scalar_api.hpp" MODULE baseline
            CLANG "${ROSETTA_CLANG_EXECUTABLE}" OUTPUT "${generated_dir}/scalar_bindings.cpp" DEPFILE "${generated_dir}/scalar.d")
        add_library(rosetta_scalar_test MODULE "${generated_dir}/scalar_bindings.cpp")
        target_compile_features(rosetta_scalar_test PRIVATE cxx_std_17)
        target_include_directories(rosetta_scalar_test PRIVATE "${LUA_INCLUDE_DIR}")
        target_link_libraries(rosetta_scalar_test PRIVATE ${LUA_LIBRARIES})
        set_target_properties(rosetta_scalar_test PROPERTIES PREFIX "" OUTPUT_NAME baseline)
        rosetta_warnings(rosetta_scalar_test)
        add_test(NAME lua_scalar COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scalar.lua" "$<TARGET_FILE:rosetta_scalar_test>")
        add_executable(rosetta_lua_value_tests tests/test_lua_values.cpp "${generated_dir}/interop_a.cpp" "${generated_dir}/interop_b.cpp"
            "${generated_dir}/demo_bindings.cpp" examples/demo.cpp examples/compile_time.cpp examples/buffer.cpp examples/calls.cpp examples/errors.cpp examples/ownership.cpp examples/coroutines.cpp examples/unicode.cpp examples/numerics.cpp examples/chrono.cpp examples/products.cpp examples/enum_flags.cpp examples/enum_values.cpp examples/nested.cpp examples/maps.cpp examples/variants.cpp examples/filesystem.cpp examples/containers.cpp examples/names.cpp examples/worklists.cpp examples/concurrency.cpp examples/members.cpp examples/native_boundary.cpp examples/native_forms.cpp examples/callables.cpp examples/class_callbacks.cpp examples/awaitables.cpp)
        target_compile_features(rosetta_lua_value_tests PRIVATE cxx_std_20)
        target_include_directories(rosetta_lua_value_tests PRIVATE "${LUA_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/examples")
        target_link_libraries(rosetta_lua_value_tests PRIVATE ${LUA_LIBRARIES} Threads::Threads rosetta_native_abi_static rosetta_module_native rosetta_pch_native rosetta_exchange)
        rosetta_warnings(rosetta_lua_value_tests)
        add_test(NAME lua_value_cleanup COMMAND rosetta_lua_value_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_objects.lua" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_callbacks.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hierarchy.lua" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_protocols.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_async.lua" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_advanced_values.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compile_time.lua" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_buffers.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_calls.lua" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_errors.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ownership_transfer.lua" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_coroutines.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_unicode.lua" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_numerics.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_chrono.lua" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_products.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_enum_flags.lua" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_enum_values.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_nested.lua" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_maps.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_variants.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_filesystem.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_containers.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_names.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_worklists.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_concurrency.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_members.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_boundary.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_build.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_forms.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_callables.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_class_callbacks.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_awaitables.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_interop.lua")
        # Lua receives an explicit module path, so this works with multi-config
        # generators and with .so/.dylib platform suffixes without shell quoting.
        add_test(NAME lua_integration COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo.lua" "$<TARGET_FILE:rosetta_demo>" "$<TARGET_FILE:rosetta_interop_a>" "$<TARGET_FILE:rosetta_interop_b>")
        add_test(NAME lua_objects COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_objects.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_callbacks COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_callbacks.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_hierarchy COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hierarchy.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_protocols COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_protocols.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_async COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_async.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_advanced_values COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_advanced_values.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_compile_time COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compile_time.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_buffers COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_buffers.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_calls COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_calls.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_errors COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_errors.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_ownership_transfer COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ownership_transfer.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_coroutines COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_coroutines.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_unicode COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_unicode.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_numerics COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_numerics.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_chrono COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_chrono.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_products COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_products.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_enum_flags COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_enum_flags.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_enum_values COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_enum_values.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_nested COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_nested.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_maps COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_maps.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_variants COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_variants.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_filesystem COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_filesystem.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_containers COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_containers.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_names COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_names.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_worklists COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_worklists.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_concurrency COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_concurrency.lua" "$<TARGET_FILE:rosetta_demo>")
        add_executable(rosetta_native_abi_tests tests/test_native_abi.cpp)
        target_compile_features(rosetta_native_abi_tests PRIVATE cxx_std_17)
        target_include_directories(rosetta_native_abi_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples")
        target_link_libraries(rosetta_native_abi_tests PRIVATE rosetta_native_abi_static ${CMAKE_DL_LIBS})
        add_dependencies(rosetta_native_abi_tests rosetta_native_abi_shared)
        rosetta_warnings(rosetta_native_abi_tests)
        add_test(NAME native_abi_c_header COMMAND "${ROSETTA_CLANG_EXECUTABLE}" -x c -std=c11
            -fsyntax-only "${CMAKE_CURRENT_SOURCE_DIR}/examples/native_abi.h")
        add_test(NAME native_abi COMMAND rosetta_native_abi_tests "$<TARGET_FILE:rosetta_native_abi_shared>")
        foreach(first a b)
            add_test(NAME lua_interop_${first}_first COMMAND "${ROSETTA_LUA_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_interop.lua" "$<TARGET_FILE:rosetta_interop_a>" "$<TARGET_FILE:rosetta_interop_b>" "${first}")
        endforeach()
        add_test(NAME lua_awaitables COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_awaitables.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_class_callbacks COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_class_callbacks.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_callables COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_callables.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_native_forms COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_forms.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_native_build COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_build.lua" "$<TARGET_FILE:rosetta_demo>")
        if(ROSETTA_BUILD_MODULE_EXAMPLE AND ROSETTA_NINJA_EXECUTABLE)
            add_executable(rosetta_native_build_tests tests/test_native_build.cpp)
            target_link_libraries(rosetta_native_build_tests PRIVATE rosetta_core)
            target_include_directories(rosetta_native_build_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
            rosetta_warnings(rosetta_native_build_tests)
            add_test(NAME native_build_graph COMMAND rosetta_native_build_tests "${CMAKE_CXX_COMPILER}"
                "${CMAKE_COMMAND}" "${ROSETTA_NINJA_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
        add_test(NAME lua_native_boundary COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_boundary.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_members COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_members.lua" "$<TARGET_FILE:rosetta_demo>")
        add_test(NAME lua_repl_startup COMMAND "${ROSETTA_LUA_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_repl.lua"
            "${lua_demo_search_path}" "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.lua")
    endif()
endif()
