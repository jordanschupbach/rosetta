option(ROSETTA_BUILD_RUBY_EXAMPLES "Build and test MRI Ruby native extensions (requires Ruby 3.1+ headers)" OFF)
if(ROSETTA_BUILD_RUBY_EXAMPLES)
    find_package(Ruby 3.1 REQUIRED)
    find_package(Threads REQUIRED)
    # Use this interpreter's bundled/default gems, avoiding incompatible user gems.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "RUBYOPT=" "RUBYLIB=" "${Ruby_EXECUTABLE}" --disable-gems -rrbconfig -e
        "puts RbConfig::CONFIG.fetch('DLEXT'); require 'rubygems'; puts Gem.default_dir"
        OUTPUT_VARIABLE ruby_config OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    string(REPLACE "\n" ";" ruby_config "${ruby_config}")
    list(GET ruby_config 0 ruby_extension)
    list(GET ruby_config 1 ruby_gem_dir)
    set(ruby_command "${CMAKE_COMMAND}" -E env "GEM_HOME=${ruby_gem_dir}" "GEM_PATH=${ruby_gem_dir}"
        "RUBYOPT=" "RUBYLIB=" "${Ruby_EXECUTABLE}")
    set(ruby_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/ruby")
    rosetta_generate_ruby(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.hpp"
        MODULE ruby_demo OUTPUT "${ruby_generated}/demo.cpp"
        CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.policy.json")
    add_library(rosetta_ruby_demo MODULE "${ruby_generated}/demo.cpp")
    rosetta_generate_ruby(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.hpp"
        MODULE ruby_verify OUTPUT "${ruby_generated}/verify.cpp"
        CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.policy.json")
    add_library(rosetta_ruby_verify MODULE "${ruby_generated}/verify.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.cpp")
    foreach(example demo verify)
        set_target_properties(rosetta_ruby_${example} PROPERTIES PREFIX "" OUTPUT_NAME ruby_${example} SUFFIX ".${ruby_extension}")
        target_include_directories(rosetta_ruby_${example} SYSTEM PRIVATE ${Ruby_INCLUDE_DIRS})
        target_link_libraries(rosetta_ruby_${example} PRIVATE ${Ruby_LIBRARIES})
        target_compile_features(rosetta_ruby_${example} PRIVATE cxx_std_17)
        rosetta_warnings(rosetta_ruby_${example})
    endforeach()
    target_compile_features(rosetta_ruby_verify PRIVATE cxx_std_20)
    target_link_libraries(rosetta_ruby_verify PRIVATE Threads::Threads)
    add_custom_target(ruby-repl
        COMMAND ${ruby_command} "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.rb"
            "$<TARGET_FILE:rosetta_ruby_demo>" "$<TARGET_FILE:rosetta_ruby_verify>"
        DEPENDS rosetta_ruby_demo rosetta_ruby_verify USES_TERMINAL VERBATIM)
    if(BUILD_TESTING)
        # Compile the exact emitted runtime into a fault/nonlocal-exit probe.
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ruby_runtime.hpp")
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/ruby_runtime.hpp" runtime_header)
        string(FIND "${runtime_header}" "R\"cpp(" runtime_begin)
        string(FIND "${runtime_header}" ")cpp\"" runtime_end)
        math(EXPR runtime_begin "${runtime_begin} + 6")
        math(EXPR runtime_length "${runtime_end} - ${runtime_begin}")
        string(SUBSTRING "${runtime_header}" ${runtime_begin} ${runtime_length} ruby_test_runtime)
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tests/ruby_runtime_probe.cpp.in" "${ruby_generated}/runtime_probe.cpp" @ONLY)
        add_library(rosetta_ruby_runtime_probe MODULE "${ruby_generated}/runtime_probe.cpp")
        set_target_properties(rosetta_ruby_runtime_probe PROPERTIES PREFIX "" OUTPUT_NAME ruby_runtime_probe SUFFIX ".${ruby_extension}")
        target_include_directories(rosetta_ruby_runtime_probe SYSTEM PRIVATE ${Ruby_INCLUDE_DIRS})
        target_link_libraries(rosetta_ruby_runtime_probe PRIVATE ${Ruby_LIBRARIES})
        target_compile_features(rosetta_ruby_runtime_probe PRIVATE cxx_std_17)
        rosetta_warnings(rosetta_ruby_runtime_probe)
        add_test(NAME ruby_runtime_boundary COMMAND ${ruby_command}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ruby.rb" boundary
            "$<TARGET_FILE:rosetta_ruby_demo>" "$<TARGET_FILE:rosetta_ruby_verify>"
            "$<TARGET_FILE:rosetta_ruby_runtime_probe>")
        foreach(family scalars objects features repl)
            add_test(NAME ruby_${family} COMMAND ${ruby_command}
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ruby.rb" ${family}
                "$<TARGET_FILE:rosetta_ruby_demo>" "$<TARGET_FILE:rosetta_ruby_verify>"
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/repl.rb")
        endforeach()
        set_tests_properties(ruby_features PROPERTIES TIMEOUT 20)
    endif()
endif()
