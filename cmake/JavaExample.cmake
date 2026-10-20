option(ROSETTA_BUILD_JAVA_EXAMPLES "Build and test Java JNI bindings (requires JDK 17+)" OFF)
if(ROSETTA_BUILD_JAVA_EXAMPLES)
    find_package(Java 17 REQUIRED COMPONENTS Development)
    # Find JNI headers from the exact javac used to compile the generated API.
    get_filename_component(java_compiler "${Java_JAVAC_EXECUTABLE}" REALPATH)
    get_filename_component(java_bin "${java_compiler}" DIRECTORY)
    get_filename_component(JAVA_HOME "${java_bin}" DIRECTORY)
    find_package(JNI REQUIRED COMPONENTS JVM)
    find_package(Threads REQUIRED)
    set(java_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/java")
    set(java_classes "${CMAKE_CURRENT_BINARY_DIR}/java-classes")
    foreach(example demo verify)
        if(example STREQUAL "demo")
            set(native r_demo)
            set(java_name JavaDemo)
        else()
            set(native r_verification)
            set(java_name JavaVerify)
        endif()
        rosetta_generate_java(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/${native}.hpp"
            MODULE ${java_name} OUTPUT "${java_generated}/${example}.cpp"
            JAVA_OUTPUT "${java_generated}/${java_name}.java"
            CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/${native}.policy.json")
        add_library(rosetta_java_${example} SHARED "${java_generated}/${example}.cpp")
        set_target_properties(rosetta_java_${example} PROPERTIES OUTPUT_NAME ${java_name})
        target_include_directories(rosetta_java_${example} SYSTEM PRIVATE ${JNI_INCLUDE_DIRS})
        target_compile_features(rosetta_java_${example} PRIVATE cxx_std_17)
        target_link_libraries(rosetta_java_${example} PRIVATE Threads::Threads)
        rosetta_warnings(rosetta_java_${example})
    endforeach()
    target_sources(rosetta_java_verify PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.cpp")
    target_compile_features(rosetta_java_verify PRIVATE cxx_std_20)
    rosetta_generate_java(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.hpp"
        MODULE Packaged_Api OUTPUT "${java_generated}/packaged.cpp" JAVA_PACKAGE org.rosetta.bindings
        CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.policy.json")
    add_library(rosetta_java_packaged SHARED "${java_generated}/packaged.cpp")
    set_target_properties(rosetta_java_packaged PROPERTIES OUTPUT_NAME Packaged_Api)
    target_include_directories(rosetta_java_packaged SYSTEM PRIVATE ${JNI_INCLUDE_DIRS})
    target_compile_features(rosetta_java_packaged PRIVATE cxx_std_17)
    target_link_libraries(rosetta_java_packaged PRIVATE Threads::Threads)
    rosetta_warnings(rosetta_java_packaged)
    # A JAR tracks the complete javac output, including owner/Cleaner classes.
    set(java_api_jar "${CMAKE_CURRENT_BINARY_DIR}/RosettaExamples.jar")
    add_custom_command(OUTPUT "${java_api_jar}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${java_classes}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${java_classes}"
        COMMAND "${Java_JAVAC_EXECUTABLE}" --release 17 -Xlint:all -Werror -d "${java_classes}"
            "${java_generated}/JavaDemo.java" "${java_generated}/JavaVerify.java" "${java_generated}/Packaged_Api.java"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/JavaExample.java"
        COMMAND "${Java_JAR_EXECUTABLE}" --create --file "${java_api_jar}" -C "${java_classes}" .
        DEPENDS "${java_generated}/JavaDemo.java" "${java_generated}/JavaVerify.java" "${java_generated}/Packaged_Api.java"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/JavaExample.java" VERBATIM)
    add_custom_target(rosetta_java_classes ALL DEPENDS "${java_api_jar}")
    add_dependencies(rosetta_java_classes rosetta_java_demo rosetta_java_verify rosetta_java_packaged)
    add_custom_target(java-demo
        COMMAND "${Java_JAVA_EXECUTABLE}" "-Djava.library.path=$<TARGET_FILE_DIR:rosetta_java_demo>"
            -cp "${java_api_jar}" JavaExample
        DEPENDS rosetta_java_classes USES_TERMINAL VERBATIM)
    if(BUILD_TESTING)
        set(java_test_classes "${CMAKE_CURRENT_BINARY_DIR}/java-test-classes")
        set(java_test_jar "${CMAKE_CURRENT_BINARY_DIR}/RosettaTests.jar")
        add_custom_command(OUTPUT "${java_test_jar}"
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${java_test_classes}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${java_test_classes}"
            COMMAND "${Java_JAVAC_EXECUTABLE}" --release 17 -Xlint:all -Werror -cp "${java_api_jar}" -d "${java_test_classes}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/TestJava.java"
            COMMAND "${Java_JAR_EXECUTABLE}" --create --file "${java_test_jar}" -C "${java_test_classes}" .
            DEPENDS "${java_api_jar}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/TestJava.java"
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/JavaExample.java" VERBATIM)
        add_custom_target(rosetta_java_tests ALL DEPENDS "${java_test_jar}")
        add_dependencies(rosetta_java_tests rosetta_java_classes)
        foreach(family scalars objects features)
            add_test(NAME java_${family} COMMAND "${Java_JAVA_EXECUTABLE}" -Xcheck:jni
                "-Djava.library.path=$<TARGET_FILE_DIR:rosetta_java_demo>" -cp "${java_api_jar}:${java_test_jar}" TestJava ${family})
            set_tests_properties(java_${family} PROPERTIES TIMEOUT 30
                FAIL_REGULAR_EXPRESSION "WARNING in native method;FATAL ERROR in native method")
        endforeach()
        add_test(NAME java_example COMMAND "${Java_JAVA_EXECUTABLE}" -Xcheck:jni
            "-Djava.library.path=$<TARGET_FILE_DIR:rosetta_java_demo>" -cp "${java_api_jar}" JavaExample)
    endif()
endif()
