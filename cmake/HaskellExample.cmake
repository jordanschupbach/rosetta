option(ROSETTA_BUILD_HASKELL_EXAMPLES "Build and test Haskell APIs (requires GHC and Haddock)" OFF)

if(ROSETTA_BUILD_HASKELL_EXAMPLES AND BUILD_TESTING)
    find_program(ROSETTA_GHC_EXECUTABLE NAMES ghc REQUIRED)
    find_program(ROSETTA_HADDOCK_EXECUTABLE NAMES haddock REQUIRED)
    set(haskell_dir "${CMAKE_CURRENT_BINARY_DIR}/haskell")

    # rosetta_haskell_test(NAME <test> HEADER <hpp> MODULE <module> POLICY <json> SOURCE <test.hs>
    #                      INCLUDE <dir> [PARTS n] [ARGS ...])
    # Generates the Haskell module (MODULE in PascalCase), its runtime and the C++ library, builds
    # the library, compiles the test program with GHC against both, and adds ctests running it (with
    # ARGS) and checking the module's Haddock (tests/check_haskell_haddock.cmake).
    function(rosetta_haskell_test)
        cmake_parse_arguments(HS "" "NAME;HEADER;MODULE;POLICY;SOURCE;INCLUDE;PARTS" "ARGS" ${ARGN})
        set(generated "${haskell_dir}/${HS_NAME}")
        set(sources "${generated}/${HS_MODULE}.cpp")
        set(parts)
        if(DEFINED HS_PARTS)
            set(parts PARTS ${HS_PARTS})
            math(EXPR last "${HS_PARTS} - 1")
            foreach(part RANGE 1 ${last})
                list(APPEND sources "${generated}/${HS_MODULE}_${part}.cpp")
            endforeach()
        endif()
        string(SUBSTRING "${HS_MODULE}" 0 1 first)
        string(SUBSTRING "${HS_MODULE}" 1 -1 rest)
        string(TOUPPER "${first}" first)
        set(hs_module "${first}${rest}")
        set(interface "${generated}/${hs_module}.hs")
        set(runtime "${generated}/${hs_module}/Rosetta.hs")
        rosetta_generate_haskell(HEADER "${HS_HEADER}" MODULE ${HS_MODULE} CLANG "${ROSETTA_CLANG_EXECUTABLE}"
            OUTPUT "${generated}/${HS_MODULE}.cpp" HASKELL_OUTPUT "${interface}" HASKELL_MODULE ${hs_module}
            POLICY "${HS_POLICY}" ${parts})
        add_library(rosetta_haskell_${HS_NAME} SHARED ${sources})
        target_compile_features(rosetta_haskell_${HS_NAME} PRIVATE cxx_std_17)
        target_include_directories(rosetta_haskell_${HS_NAME} PRIVATE "${HS_INCLUDE}")
        set_target_properties(rosetta_haskell_${HS_NAME} PROPERTIES OUTPUT_NAME ${HS_MODULE} LIBRARY_OUTPUT_DIRECTORY "${generated}")
        set(program "${generated}/test_${HS_NAME}")
        add_custom_command(OUTPUT "${program}"
            COMMAND "${ROSETTA_GHC_EXECUTABLE}" -threaded -rtsopts -Wall -Werror -v0 "-i${generated}" -outputdir "${generated}/obj"
                -o "${program}" "${HS_SOURCE}" "-L${generated}" "-l${HS_MODULE}" "-optl-Wl,-rpath,${generated}"
            DEPENDS "${HS_SOURCE}" "${interface}" "${runtime}" rosetta_haskell_${HS_NAME}
            COMMENT "Compiling the Haskell test ${HS_NAME}" VERBATIM)
        add_custom_target(rosetta_haskell_${HS_NAME}_program ALL DEPENDS "${program}")
        add_test(NAME haskell_${HS_NAME} COMMAND "${program}" ${HS_ARGS})
        set_tests_properties(haskell_${HS_NAME} PROPERTIES TIMEOUT 120)
        add_test(NAME haskell_${HS_NAME}_haddock COMMAND "${CMAKE_COMMAND}" "-DHADDOCK=${ROSETTA_HADDOCK_EXECUTABLE}" "-DMODULE=${hs_module}"
            "-DINTERFACE=${interface}" "-DRUNTIME=${runtime}" "-DOUTPUT=${generated}/doc" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_haskell_haddock.cmake")
        set_tests_properties(haskell_${HS_NAME}_haddock PROPERTIES TIMEOUT 300)
    endfunction()

    # The example shared with Python, Lua, Lisp, Julia, C#, Rust, OCaml and Prolog; a small
    # allocation area makes the garbage collector run often.
    rosetta_haskell_test(NAME full_features HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp" MODULE features
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_haskell_full.hs" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/examples" PARTS 2
        ARGS "${haskell_dir}/full_features/Features.hs" +RTS -A64k -RTS)
    # Keywords, Prelude and runtime names, primes, enumerators, integers, conversions, policy
    # modules, exception kinds and interleaved standard output.
    rosetta_haskell_test(NAME full_names HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/haskell_full_names.hpp" MODULE names
        POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/haskell_full_names.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_haskell_names.hs" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()
