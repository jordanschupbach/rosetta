option(ROSETTA_BUILD_RUST_EXAMPLES "Build and test Rust crates (requires cargo and rustc)" OFF)
if(ROSETTA_BUILD_RUST_EXAMPLES AND BUILD_TESTING)
    find_program(ROSETTA_CARGO_EXECUTABLE NAMES cargo REQUIRED)
    set(rust_dir "${CMAKE_CURRENT_BINARY_DIR}/rust")
    # Cargo runs offline (the crates have no dependencies) with its home and target directory in the
    # build tree. Warnings are errors in the generated crates and the test programs alike.
    set(rust_env "${CMAKE_COMMAND}" -E env "CARGO_HOME=${rust_dir}/cargo-home" "CARGO_TARGET_DIR=${rust_dir}/target"
        CARGO_TERM_COLOR=never)

    # rosetta_rust_test(NAME <test> HEADER <hpp> MODULE <module> POLICY <json> SOURCE <test.rs>
    #                   INCLUDE <dir> [PARTS n])
    # Generates the crate, builds the native library and a program depending on the crate (with the
    # test source as its main.rs), and adds a ctest running it with the generated src/lib.rs.
    function(rosetta_rust_test)
        cmake_parse_arguments(RS "" "NAME;HEADER;MODULE;POLICY;SOURCE;INCLUDE;PARTS" "" ${ARGN})
        set(generated "${rust_dir}/generated/${RS_NAME}")
        set(crate "${rust_dir}/${RS_NAME}/${RS_MODULE}")
        set(sources "${generated}/${RS_MODULE}.cpp")
        set(parts)
        if(DEFINED RS_PARTS)
            set(parts PARTS ${RS_PARTS})
            math(EXPR last "${RS_PARTS} - 1")
            foreach(part RANGE 1 ${last})
                list(APPEND sources "${generated}/${RS_MODULE}_${part}.cpp")
            endforeach()
        endif()
        rosetta_generate_rust(HEADER "${RS_HEADER}" MODULE ${RS_MODULE} CLANG "${ROSETTA_CLANG_EXECUTABLE}"
            OUTPUT "${generated}/${RS_MODULE}.cpp" RUST_OUTPUT "${crate}" POLICY "${RS_POLICY}" ${parts})
        set(native "${rust_dir}/native/${RS_NAME}")
        add_library(rosetta_rust_${RS_NAME} SHARED ${sources})
        target_compile_features(rosetta_rust_${RS_NAME} PRIVATE cxx_std_17)
        target_include_directories(rosetta_rust_${RS_NAME} PRIVATE "${RS_INCLUDE}")
        set_target_properties(rosetta_rust_${RS_NAME} PROPERTIES OUTPUT_NAME ${RS_MODULE} LIBRARY_OUTPUT_DIRECTORY "${native}")
        # The test program: a crate requiring the generated one by path.
        set(program "${rust_dir}/${RS_NAME}/test_${RS_MODULE}")
        file(WRITE "${program}/Cargo.toml"
            "[package]\nname = \"test_${RS_MODULE}\"\nversion = \"0.1.0\"\nedition = \"2021\"\npublish = false\n\n"
            "[dependencies]\n${RS_MODULE} = { path = \"../${RS_MODULE}\" }\n")
        configure_file("${RS_SOURCE}" "${program}/src/main.rs" COPYONLY)
        string(TOUPPER "${RS_MODULE}" environment)
        set(binary "${rust_dir}/target/debug/test_${RS_MODULE}")
        add_custom_command(OUTPUT "${binary}"
            COMMAND ${rust_env} "${environment}_RUST_LIB_DIR=${native}" "RUSTFLAGS=-D warnings -C link-arg=-Wl,-rpath,${native}"
                "${ROSETTA_CARGO_EXECUTABLE}" build --offline --quiet --manifest-path "${program}/Cargo.toml"
            DEPENDS "${crate}/src/lib.rs" "${crate}/src/rosetta.rs" "${crate}/Cargo.toml" "${crate}/build.rs"
                "${program}/src/main.rs" "${program}/Cargo.toml" rosetta_rust_${RS_NAME}
            COMMENT "Building the Rust ${RS_NAME} test" VERBATIM)
        add_custom_target(rosetta_rust_${RS_NAME}_program ALL DEPENDS "${binary}")
        add_test(NAME rust_${RS_NAME} COMMAND "${binary}" "${crate}/src/lib.rs")
        set_tests_properties(rust_${RS_NAME} PROPERTIES TIMEOUT 120)
        # rustdoc renders the documentation without broken links or warnings.
        add_test(NAME rust_${RS_NAME}_doc COMMAND ${rust_env} "RUSTDOCFLAGS=-D warnings"
            "${ROSETTA_CARGO_EXECUTABLE}" doc --offline --quiet --no-deps --manifest-path "${crate}/Cargo.toml")
        set_tests_properties(rust_${RS_NAME}_doc PROPERTIES TIMEOUT 300)
    endfunction()

    # The example shared with Python, Lua, Lisp, Julia, C#, VB and Go.
    rosetta_rust_test(NAME full_features HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp" MODULE features
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rust_full.rs" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/examples" PARTS 2)
    # Prelude and runtime type names, Rust keywords, generated member names and multiple inheritance.
    rosetta_rust_test(NAME full_names HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/rust_full_names.hpp" MODULE names
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/rust_full_names.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rust_names.rs" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()
