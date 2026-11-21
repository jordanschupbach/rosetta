option(ROSETTA_BUILD_VB_EXAMPLES "Build and test Visual Basic bindings (requires .NET 10 SDK)" OFF)
if(ROSETTA_BUILD_VB_EXAMPLES AND BUILD_TESTING)
    find_program(ROSETTA_DOTNET_EXECUTABLE NAMES dotnet REQUIRED)
    set(vb_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/vb")
    set(vb_output "${CMAKE_CURRENT_BINARY_DIR}/vb")
    # Framework reference packs ship with the SDK; no NuGet packages are needed.
    file(MAKE_DIRECTORY "${vb_generated}")
    file(WRITE "${vb_generated}/NuGet.Config" "<configuration><packageSources><clear /></packageSources></configuration>\n")

    # rosetta_vb_test(NAME <test> HEADER <hpp> MODULE <module> INTERFACE <File.vb> POLICY <json>
    #                 SOURCE <test.vb> INCLUDE <dir> [PARTS n] [ARGS ...])
    # Generates the bindings, builds the native library and a VB program from the interface and the
    # test source, and adds a ctest running it with <MODULE>_VB_LIBRARY naming the library.
    function(rosetta_vb_test)
        cmake_parse_arguments(VB "" "NAME;HEADER;MODULE;INTERFACE;POLICY;SOURCE;INCLUDE;PARTS" "ARGS" ${ARGN})
        set(dir "${vb_generated}/${VB_NAME}")
        set(sources "${dir}/${VB_MODULE}.cpp")
        set(parts)
        if(DEFINED VB_PARTS)
            set(parts PARTS ${VB_PARTS})
            math(EXPR last "${VB_PARTS} - 1")
            foreach(part RANGE 1 ${last})
                list(APPEND sources "${dir}/${VB_MODULE}_${part}.cpp")
            endforeach()
        endif()
        rosetta_generate_visual_basic(HEADER "${VB_HEADER}" MODULE ${VB_MODULE} CLANG "${ROSETTA_CLANG_EXECUTABLE}"
            OUTPUT "${dir}/${VB_MODULE}.cpp" VB_OUTPUT "${dir}/${VB_INTERFACE}" VB_PROJECT "${dir}/${VB_MODULE}.vbproj"
            POLICY "${VB_POLICY}" ${parts})
        add_library(rosetta_vb_${VB_NAME} MODULE ${sources})
        target_compile_features(rosetta_vb_${VB_NAME} PRIVATE cxx_std_17)
        target_include_directories(rosetta_vb_${VB_NAME} PRIVATE "${VB_INCLUDE}")
        set_target_properties(rosetta_vb_${VB_NAME} PROPERTIES PREFIX "" OUTPUT_NAME ${VB_MODULE}
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vb-native/${VB_NAME}")
        get_filename_component(program "${VB_SOURCE}" NAME_WE)
        # The project's default RootNamespace (its name) checks that generated code ignores it.
        set(project "${dir}/${program}.vbproj")
        file(WRITE "${project}" "<Project Sdk=\"Microsoft.NET.Sdk\"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><EnableDefaultCompileItems>false</EnableDefaultCompileItems><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup><ItemGroup><Compile Include=\"${dir}/${VB_INTERFACE}\" /><Compile Include=\"${VB_SOURCE}\" /></ItemGroup></Project>\n")
        add_custom_command(OUTPUT "${vb_output}/${VB_NAME}/${program}.dll"
            COMMAND "${CMAKE_COMMAND}" -E env "DOTNET_CLI_HOME=${CMAKE_CURRENT_BINARY_DIR}/dotnet-home"
                DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
                "${ROSETTA_DOTNET_EXECUTABLE}" build "${project}" --configuration Release --output "${vb_output}/${VB_NAME}"
                --nologo --disable-build-servers "-p:BaseIntermediateOutputPath=${vb_generated}/obj/${VB_NAME}/"
                --configfile "${vb_generated}/NuGet.Config"
            DEPENDS "${project}" "${dir}/${VB_INTERFACE}" "${VB_SOURCE}" "${vb_generated}/NuGet.Config"
            VERBATIM)
        add_custom_target(rosetta_vb_${program} ALL DEPENDS "${vb_output}/${VB_NAME}/${program}.dll" rosetta_vb_${VB_NAME})
        string(TOUPPER "${VB_MODULE}" environment)
        set(arguments)
        foreach(argument IN LISTS VB_ARGS)
            string(REPLACE "@INTERFACE@" "${dir}/${VB_INTERFACE}" argument "${argument}")
            list(APPEND arguments "${argument}")
        endforeach()
        add_test(NAME vb_${VB_NAME} COMMAND "${ROSETTA_DOTNET_EXECUTABLE}" "${vb_output}/${VB_NAME}/${program}.dll" ${arguments})
        set_tests_properties(vb_${VB_NAME} PROPERTIES TIMEOUT 120
            ENVIRONMENT "${environment}_VB_LIBRARY=$<TARGET_FILE:rosetta_vb_${VB_NAME}>")
    endfunction()

    # The example shared with Python, Lua, Lisp, Julia and C#.
    rosetta_vb_test(NAME full_features HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.hpp" MODULE features
        INTERFACE Features.vb POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/python_features.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/TestVBFull.vb" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/examples"
        PARTS 2 ARGS @INTERFACE@)
    # .NET type names, VB keywords, names that differ only in case, and members bases declare.
    rosetta_vb_test(NAME full_names HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/vb_full_names.hpp" MODULE shadow
        INTERFACE Shadow.vb POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/vb_full_names.policy.json"
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/TestVBNames.vb" INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()
