option(ROSETTA_BUILD_CSHARP_EXAMPLES "Build and test C# bindings (requires .NET 10 SDK)" OFF)
if(ROSETTA_BUILD_CSHARP_EXAMPLES)
    find_program(ROSETTA_DOTNET_EXECUTABLE NAMES dotnet REQUIRED)
    find_package(Threads REQUIRED)
    set(csharp_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/csharp")
    set(csharp_output "${CMAKE_CURRENT_BINARY_DIR}/csharp")
    foreach(example demo verify)
        if(example STREQUAL "demo")
            set(native r_demo)
            set(csharp_name CSharpDemo)
        else()
            set(native r_verification)
            set(csharp_name CSharpVerify)
        endif()
        rosetta_generate_csharp(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/${native}.hpp"
            MODULE ${csharp_name} OUTPUT "${csharp_generated}/${example}.cpp"
            CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/${native}.policy.json")
        add_library(rosetta_csharp_${example} SHARED "${csharp_generated}/${example}.cpp")
        set_target_properties(rosetta_csharp_${example} PROPERTIES OUTPUT_NAME ${csharp_name}
            LIBRARY_OUTPUT_DIRECTORY "${csharp_output}")
        target_compile_features(rosetta_csharp_${example} PRIVATE cxx_std_17)
        target_link_libraries(rosetta_csharp_${example} PRIVATE Threads::Threads)
        rosetta_warnings(rosetta_csharp_${example})
    endforeach()
    target_sources(rosetta_csharp_verify PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_verification.cpp")
    target_compile_features(rosetta_csharp_verify PRIVATE cxx_std_20)
    rosetta_generate_csharp(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.hpp"
        MODULE NamespacedApi OUTPUT "${csharp_generated}/namespaced.cpp" CSHARP_NAMESPACE Rosetta.Bindings
        CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/examples/r_demo.policy.json")
    add_library(rosetta_csharp_namespaced SHARED "${csharp_generated}/namespaced.cpp")
    set_target_properties(rosetta_csharp_namespaced PROPERTIES OUTPUT_NAME NamespacedApi LIBRARY_OUTPUT_DIRECTORY "${csharp_output}")
    target_compile_features(rosetta_csharp_namespaced PRIVATE cxx_std_17)
    target_link_libraries(rosetta_csharp_namespaced PRIVATE Threads::Threads)
    rosetta_warnings(rosetta_csharp_namespaced)
    if(BUILD_TESTING)
        rosetta_generate_csharp(HEADER "${CMAKE_CURRENT_SOURCE_DIR}/tests/csharp_enums.hpp"
            MODULE EnumApi OUTPUT "${csharp_generated}/enums.cpp"
            CLANG "${ROSETTA_CLANG_EXECUTABLE}" POLICY "${CMAKE_CURRENT_SOURCE_DIR}/tests/csharp_enums.policy.json")
        add_library(rosetta_csharp_enums SHARED "${csharp_generated}/enums.cpp")
        set_target_properties(rosetta_csharp_enums PROPERTIES OUTPUT_NAME EnumApi LIBRARY_OUTPUT_DIRECTORY "${csharp_output}")
        target_compile_features(rosetta_csharp_enums PRIVATE cxx_std_17)
        target_link_libraries(rosetta_csharp_enums PRIVATE Threads::Threads)
        rosetta_warnings(rosetta_csharp_enums)
    endif()
    # Framework reference packs ship with the SDK; no NuGet packages are needed.
    file(MAKE_DIRECTORY "${csharp_generated}")
    file(WRITE "${csharp_generated}/NuGet.Config" "<configuration><packageSources><clear /></packageSources></configuration>\n")
    set(csharp_sources "<Compile Include=\"${csharp_generated}/CSharpDemo.cs\" /><Compile Include=\"${csharp_generated}/CSharpVerify.cs\" /><Compile Include=\"${csharp_generated}/NamespacedApi.cs\" />")
    function(rosetta_csharp_program name source)
        set(project "${csharp_generated}/${name}.csproj")
        set(program_sources "${csharp_sources}")
        set(enum_dependency)
        if(name STREQUAL "TestCSharp")
            string(APPEND program_sources "<Compile Include=\"${csharp_generated}/EnumApi.cs\" />")
            set(enum_dependency "${csharp_generated}/EnumApi.cs")
        endif()
        file(WRITE "${project}" "<Project Sdk=\"Microsoft.NET.Sdk\"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><EnableDefaultCompileItems>false</EnableDefaultCompileItems><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup><ItemGroup>${program_sources}<Compile Include=\"${source}\" /></ItemGroup></Project>\n")
        add_custom_command(OUTPUT "${csharp_output}/${name}.dll"
            COMMAND "${CMAKE_COMMAND}" -E env "DOTNET_CLI_HOME=${CMAKE_CURRENT_BINARY_DIR}/dotnet-home"
                DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
                "${ROSETTA_DOTNET_EXECUTABLE}" build "${project}" --configuration Release --output "${csharp_output}"
                --nologo --disable-build-servers "-p:BaseIntermediateOutputPath=${csharp_generated}/obj/${name}/"
                --configfile "${csharp_generated}/NuGet.Config"
            DEPENDS "${project}" "${csharp_generated}/NuGet.Config" "${source}"
                "${csharp_generated}/CSharpDemo.cs" "${csharp_generated}/CSharpVerify.cs" "${csharp_generated}/NamespacedApi.cs" ${enum_dependency}
            VERBATIM)
        add_custom_target(rosetta_csharp_${name} ALL DEPENDS "${csharp_output}/${name}.dll")
        add_dependencies(rosetta_csharp_${name} rosetta_csharp_demo rosetta_csharp_verify rosetta_csharp_namespaced)
    endfunction()
    rosetta_csharp_program(CSharpExample "${CMAKE_CURRENT_SOURCE_DIR}/examples/CSharpExample.cs")
    add_custom_target(csharp-demo COMMAND "${ROSETTA_DOTNET_EXECUTABLE}" "${csharp_output}/CSharpExample.dll"
        DEPENDS rosetta_csharp_CSharpExample USES_TERMINAL VERBATIM)
    if(BUILD_TESTING)
        rosetta_csharp_program(TestCSharp "${CMAKE_CURRENT_SOURCE_DIR}/tests/TestCSharp.cs")
        add_dependencies(rosetta_csharp_TestCSharp rosetta_csharp_enums)
        foreach(family scalars objects features enums)
            add_test(NAME csharp_${family} COMMAND "${ROSETTA_DOTNET_EXECUTABLE}" "${csharp_output}/TestCSharp.dll" ${family})
            set_tests_properties(csharp_${family} PROPERTIES TIMEOUT 30)
        endforeach()
        add_test(NAME csharp_example COMMAND "${ROSETTA_DOTNET_EXECUTABLE}" "${csharp_output}/CSharpExample.dll")
    endif()
endif()
