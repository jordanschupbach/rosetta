option(ROSETTA_BUILD_GO_EXAMPLES "Build and test Go packages (requires Go 1.24 and cgo)" OFF)
if(ROSETTA_BUILD_GO_EXAMPLES)
    find_program(ROSETTA_GO_EXECUTABLE go REQUIRED)
    get_filename_component(go_bin "${ROSETTA_GO_EXECUTABLE}" DIRECTORY)
    find_program(ROSETTA_GOFMT_EXECUTABLE gofmt HINTS "${go_bin}" REQUIRED)
    # Each package NAME is a module in go/NAME: NAME.go, go.mod and the
    # libNAME_stubs.a archive it links. go/go.work lists them, so programs
    # built with GOWORK set (even single files) can import them.
    set(go_dir "${CMAKE_CURRENT_BINARY_DIR}/go")
    set(go_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/go")
    set(go_modules demo verify)
    if(BUILD_TESTING)
        list(APPEND go_modules names)
    endif()
    set(go_uses)
    set(go_sources)
    foreach(example ${go_modules})
        set(package go_${example})
        if(example STREQUAL "demo")
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.hpp")
            set(policy POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.policy.json")
        elseif(example STREQUAL "verify")
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.hpp")
            set(policy POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.policy.json")
        else()
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/tests/go_names.hpp")
            set(policy POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/go_names.policy.json")
        endif()
        rosetta_generate_go(HEADER "${header}" MODULE ${package} OUTPUT "${go_generated}/${example}.cpp"
            GO_OUTPUT "${go_dir}/${package}/${package}.go" CLANG "${ROSETTA_CLANG_EXECUTABLE}" ${policy})
        add_library(rosetta_go_${example} STATIC "${go_generated}/${example}.cpp")
        set_target_properties(rosetta_go_${example} PROPERTIES OUTPUT_NAME ${package}_stubs PREFIX "lib" SUFFIX ".a"
            ARCHIVE_OUTPUT_DIRECTORY "${go_dir}/${package}" POSITION_INDEPENDENT_CODE ON)
        target_compile_features(rosetta_go_${example} PRIVATE cxx_std_17)
        rosetta_warnings(rosetta_go_${example})
        # The custom target makes the generated Go file part of the default build.
        add_custom_target(rosetta_go_${example}_package ALL DEPENDS "${go_dir}/${package}/${package}.go" rosetta_go_${example})
        file(WRITE "${go_dir}/${package}/go.mod" "module ${package}\n\ngo 1.24\n")
        list(APPEND go_uses "\t./${package}\n")
        list(APPEND go_sources "${go_dir}/${package}/${package}.go")
    endforeach()
    target_sources(rosetta_go_verify PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.cpp")
    target_compile_features(rosetta_go_verify PRIVATE cxx_std_20)
    if(BUILD_TESTING)
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tests/test_go.go" "${go_dir}/tests/rosetta_test.go" COPYONLY)
        file(WRITE "${go_dir}/tests/go.mod" "module rosetta_go_tests\n\ngo 1.24\n")
        list(APPEND go_uses "\t./tests\n")
    endif()
    string(REPLACE ";" "" go_uses "${go_uses}")
    file(WRITE "${go_dir}/go.work" "go 1.24\n\nuse (\n${go_uses})\n")
    set(go_env "${CMAKE_COMMAND}" -E env "GOWORK=${go_dir}/go.work" GOTOOLCHAIN=local GOFLAGS=-buildvcs=false CGO_ENABLED=1)
    if(BUILD_TESTING)
        # Test binaries, plain and with the race detector, built with the libraries.
        foreach(variant plain race)
            set(flags)
            if(variant STREQUAL "race")
                set(flags -race)
            endif()
            add_custom_command(OUTPUT "${go_dir}/tests/${variant}.test"
                COMMAND ${go_env} "${ROSETTA_GO_EXECUTABLE}" test -c ${flags} -o "${go_dir}/tests/${variant}.test" ./tests
                WORKING_DIRECTORY "${go_dir}"
                DEPENDS ${go_sources} "${go_dir}/tests/rosetta_test.go" rosetta_go_demo rosetta_go_verify rosetta_go_names
                COMMENT "Compiling Go tests (${variant})" VERBATIM)
            add_custom_target(rosetta_go_${variant}_tests ALL DEPENDS "${go_dir}/tests/${variant}.test")
        endforeach()
        foreach(family Scalars Names Objects Features)
            string(TOLOWER "${family}" name)
            add_test(NAME go_${name} COMMAND "${go_dir}/tests/plain.test" -test.run "^Test${family}$" -test.v)
            set_tests_properties(go_${name} PROPERTIES TIMEOUT 60)
        endforeach()
        add_test(NAME go_race COMMAND "${go_dir}/tests/race.test" -test.v)
        set_tests_properties(go_race PROPERTIES TIMEOUT 120)
        # Generated packages are gofmt-clean and pass go vet.
        add_test(NAME go_format COMMAND "${ROSETTA_GOFMT_EXECUTABLE}" -l ${go_sources})
        set_tests_properties(go_format PROPERTIES FAIL_REGULAR_EXPRESSION "\\.go")
        add_test(NAME go_vet COMMAND ${go_env} "${ROSETTA_GO_EXECUTABLE}" vet ./go_demo ./go_verify ./go_names ./tests
            WORKING_DIRECTORY "${go_dir}")
        set_tests_properties(go_vet PROPERTIES TIMEOUT 120)
    endif()
endif()

# The full Go API (--go-api full): each package is a cgo module (NAME.go, go.mod, internal/rosetta,
# policy subpackages) calling a shared library through the C ABI. Go builds run offline, with the
# module cache and build cache inside the build directory.
if(ROSETTA_BUILD_GO_EXAMPLES AND BUILD_TESTING)
    set(go_full_dir "${CMAKE_CURRENT_BINARY_DIR}/go-full")
    set(go_full_env "${CMAKE_COMMAND}" -E env GOWORK=off GOTOOLCHAIN=local GOFLAGS=-mod=mod GOPROXY=off
        "GOCACHE=${go_full_dir}/cache" "GOPATH=${go_full_dir}/gopath" CGO_ENABLED=1
        "CGO_LDFLAGS=-L${go_full_dir}/lib -Wl,-rpath,${go_full_dir}/lib")
    set(go_full_packages)
    foreach(example features names)
        if(example STREQUAL "features")
            set(module features)
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp")
            set(policy "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json")
            set(program "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_go_full.go")
            set(sources "${go_full_dir}/generated/${module}.cpp" "${go_full_dir}/generated/${module}_1.cpp")
            set(parts PARTS 2)
        else()
            set(module gonames)
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/tests/go_full_names.hpp")
            set(policy "${CMAKE_CURRENT_SOURCE_DIR}/tests/go_full_names.policy.json")
            set(program "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_go_full_names.go")
            set(sources "${go_full_dir}/generated/${module}.cpp")
            set(parts)
        endif()
        rosetta_generate_go(HEADER "${header}" MODULE ${module} OUTPUT "${go_full_dir}/generated/${module}.cpp"
            GO_API full GO_OUTPUT "${go_full_dir}/${module}/${module}.go" CLANG "${ROSETTA_CLANG_EXECUTABLE}"
            POLICY "${policy}" ${parts})
        add_library(rosetta_go_full_${example} SHARED ${sources})
        target_compile_features(rosetta_go_full_${example} PRIVATE cxx_std_17)
        get_filename_component(header_dir "${header}" DIRECTORY)
        target_include_directories(rosetta_go_full_${example} PRIVATE "${header_dir}")
        set_target_properties(rosetta_go_full_${example} PROPERTIES OUTPUT_NAME ${module}
            LIBRARY_OUTPUT_DIRECTORY "${go_full_dir}/lib")
        # A consumer module requiring the package through a replace directive.
        set(test_dir "${go_full_dir}/test_${example}")
        file(WRITE "${test_dir}/go.mod"
            "module ${module}_test\n\ngo 1.24\n\nrequire ${module} v0.0.0\n\nreplace ${module} => ../${module}\n")
        configure_file("${program}" "${test_dir}/main.go" COPYONLY)
        foreach(variant plain race)
            set(flags)
            if(variant STREQUAL "race")
                set(flags -race)
            endif()
            add_custom_command(OUTPUT "${test_dir}/${variant}"
                COMMAND ${go_full_env} "${ROSETTA_GO_EXECUTABLE}" build ${flags} -o "${test_dir}/${variant}" .
                WORKING_DIRECTORY "${test_dir}"
                DEPENDS "${go_full_dir}/${module}/${module}.go" "${test_dir}/main.go" rosetta_go_full_${example}
                COMMENT "Building the full Go ${example} test (${variant})" VERBATIM)
            add_custom_target(rosetta_go_full_${example}_${variant} ALL DEPENDS "${test_dir}/${variant}")
        endforeach()
        list(APPEND go_full_packages "${go_full_dir}/${module}")
    endforeach()
    set(features_go "${go_full_dir}/features/features.go" "${go_full_dir}/features/data/data.go")
    add_test(NAME go_full_features COMMAND "${go_full_dir}/test_features/plain" ${features_go})
    add_test(NAME go_full_features_race COMMAND "${go_full_dir}/test_features/race" ${features_go})
    add_test(NAME go_full_names COMMAND "${go_full_dir}/test_names/plain")
    add_test(NAME go_full_names_race COMMAND "${go_full_dir}/test_names/race")
    set_tests_properties(go_full_features go_full_names PROPERTIES TIMEOUT 60)
    set_tests_properties(go_full_features_race go_full_names_race PROPERTIES TIMEOUT 120)
    # gofmt-clean, go vet-clean, and godoc as `go doc` renders it.
    add_test(NAME go_full_format COMMAND "${ROSETTA_GOFMT_EXECUTABLE}" -l ${go_full_packages})
    set_tests_properties(go_full_format PROPERTIES FAIL_REGULAR_EXPRESSION "\\.go")
    foreach(module features gonames)
        add_test(NAME go_full_vet_${module} COMMAND ${go_full_env} "${ROSETTA_GO_EXECUTABLE}" vet ./...
            WORKING_DIRECTORY "${go_full_dir}/${module}")
        set_tests_properties(go_full_vet_${module} PROPERTIES TIMEOUT 120)
    endforeach()
    set(go_doc_checks
        "package|.|Package features is the Go interface to the C\\+\\+ library features.*# Building.*CGO_LDFLAGS.*replace features => ../path"
        "add|Add|func Add\\(a int\\) \\(int, error\\)\n    Add adds three integers.*# Example\n\n        int total.*Parameters:\n\n      - a: first term.*The error is an \\*Error of kind:.*AddWithB and AddWithC pass more"
        "class|Shape|type Shape struct.*Shape wraps the C\\+\\+ class features::Shape.*NewShapeFrom.*func \\(s \\*Shape\\) Area\\(\\) \\(float64, error\\)"
        "subpackage|./data.Scaled|package data // import \"features/data\".*Scaled calls C\\+\\+ features::data::scaled")
    foreach(check ${go_doc_checks})
        string(REPLACE "|" ";" check "${check}")
        list(GET check 0 name)
        list(GET check 1 symbol)
        list(GET check 2 pattern)
        add_test(NAME go_full_doc_${name} COMMAND ${go_full_env} "${ROSETTA_GO_EXECUTABLE}" doc ${symbol}
            WORKING_DIRECTORY "${go_full_dir}/features")
        set_tests_properties(go_full_doc_${name} PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "${pattern}")
    endforeach()
endif()
