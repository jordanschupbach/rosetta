# Example-specific Clang module build graph. Rosetta parses demo.hpp, never BMIs.
# Keep BMI flags in sync with the native target, including sanitizer/build flags.
set(module_example_default OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(module_example_default ON)
endif()
option(ROSETTA_BUILD_MODULE_EXAMPLE "Build the Clang named-module/header-unit demo" ${module_example_default})
add_library(rosetta_pch_native STATIC "${PROJECT_SOURCE_DIR}/examples/pch.cpp")
target_compile_features(rosetta_pch_native PRIVATE cxx_std_20)
set_target_properties(rosetta_pch_native PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_precompile_headers(rosetta_pch_native PRIVATE "${PROJECT_SOURCE_DIR}/examples/pch_config.hpp")
rosetta_warnings(rosetta_pch_native)
if(NOT ROSETTA_BUILD_MODULE_EXAMPLE)
    add_library(rosetta_module_native STATIC "${PROJECT_SOURCE_DIR}/examples/modules/unavailable.cpp")
    target_compile_features(rosetta_module_native PRIVATE cxx_std_20)
    set_target_properties(rosetta_module_native PROPERTIES POSITION_INDEPENDENT_CODE ON)
    rosetta_warnings(rosetta_module_native)
    return()
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "ROSETTA_BUILD_MODULE_EXAMPLE currently requires Clang; disable it for other compilers")
endif()
set(module_sources "${PROJECT_SOURCE_DIR}/examples/modules")
set(module_output "${CMAKE_CURRENT_BINARY_DIR}/native-modules")
file(MAKE_DIRECTORY "${module_output}")
separate_arguments(module_flags NATIVE_COMMAND "${CMAKE_CXX_FLAGS}")
set(module_configurations ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
list(REMOVE_DUPLICATES module_configurations)
foreach(configuration IN LISTS module_configurations)
    string(TOUPPER "${configuration}" configuration_upper)
    separate_arguments(configuration_flags NATIVE_COMMAND "${CMAKE_CXX_FLAGS_${configuration_upper}}")
    foreach(flag IN LISTS configuration_flags)
        list(APPEND module_flags "$<$<CONFIG:${configuration}>:${flag}>")
    endforeach()
endforeach()
list(APPEND module_flags -std=c++20 -fPIC -Wno-unused-command-line-argument)
# Configurations must have separate BMIs: compiler options form part of their identity.
set(module_output "${module_output}/$<CONFIG>")
set(partition "${module_output}/operations.pcm")
set(primary "${module_output}/library.pcm")
set(header_unit "${module_output}/header.pcm")
set(partition_flag "-fmodule-file=rosetta_demo_native:operations=${partition}")
set(primary_flag "-fmodule-file=rosetta_demo_native=${primary}")
add_custom_command(OUTPUT "${partition}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${module_output}"
    COMMAND "${CMAKE_CXX_COMPILER}" ${module_flags} --precompile "${module_sources}/operations.cppm" -o "${partition}"
    DEPENDS "${module_sources}/operations.cppm" "${CMAKE_CXX_COMPILER}"
    COMMAND_EXPAND_LISTS VERBATIM)
add_custom_command(OUTPUT "${primary}"
    COMMAND "${CMAKE_CXX_COMPILER}" ${module_flags} ${partition_flag} --precompile "${module_sources}/library.cppm" -o "${primary}"
    DEPENDS "${module_sources}/library.cppm" "${partition}"
    COMMAND_EXPAND_LISTS VERBATIM)
add_custom_command(OUTPUT "${header_unit}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${module_output}"
    COMMAND "${CMAKE_CXX_COMPILER}" ${module_flags} -fmodule-header=user "${module_sources}/header_unit.hpp" -o "${header_unit}"
    DEPENDS "${module_sources}/header_unit.hpp" "${CMAKE_CXX_COMPILER}"
    COMMAND_EXPAND_LISTS VERBATIM)
foreach(unit operations library)
    add_custom_command(OUTPUT "${module_output}/${unit}.o"
        COMMAND "${CMAKE_CXX_COMPILER}" ${module_flags} ${partition_flag} -c "${module_output}/${unit}.pcm" -o "${module_output}/${unit}.o"
        DEPENDS "${module_output}/${unit}.pcm" "${partition}"
        COMMAND_EXPAND_LISTS VERBATIM)
endforeach()
foreach(unit implementation facade)
    add_custom_command(OUTPUT "${module_output}/${unit}.o"
        COMMAND "${CMAKE_CXX_COMPILER}" ${module_flags} "${partition_flag}" "${primary_flag}"
            "-fmodule-file=${header_unit}" -Wno-experimental-header-units -Wall -Wextra -Wpedantic
            "$<$<BOOL:${ROSETTA_WARNINGS_AS_ERRORS}>:-Werror>"
            -c "${module_sources}/${unit}.cpp" -o "${module_output}/${unit}.o"
        DEPENDS "${module_sources}/${unit}.cpp" "${PROJECT_SOURCE_DIR}/examples/demo.hpp"
            "${primary}" "${partition}" "${header_unit}"
        COMMAND_EXPAND_LISTS VERBATIM)
endforeach()
add_library(rosetta_module_native STATIC "${module_output}/implementation.o" "${module_output}/facade.o"
    "${module_output}/operations.o" "${module_output}/library.o")
set_target_properties(rosetta_module_native PROPERTIES LINKER_LANGUAGE CXX)
