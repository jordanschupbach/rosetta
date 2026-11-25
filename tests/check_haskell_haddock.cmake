# cmake -DHADDOCK=<haddock> -DMODULE=<Name> -DINTERFACE=<Name.hs> -DRUNTIME=<Name/Rosetta.hs> -DOUTPUT=<dir> -P check_haskell_haddock.cmake
# Runs Haddock on a generated Haskell module and its runtime: the module must be documented
# completely (100% coverage) and Haddock must not warn, except about links into packages whose
# documentation it was not given (base, containers, the hidden runtime module).
foreach(variable HADDOCK MODULE INTERFACE RUNTIME OUTPUT)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
execute_process(
    COMMAND "${HADDOCK}" --html "--odir=${OUTPUT}" --optghc=-outputdir "--optghc=${OUTPUT}/obj" "${INTERFACE}" "${RUNTIME}"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(log "${out}${err}")
if(NOT status EQUAL 0)
    message(FATAL_ERROR "haddock failed (${status}):\n${log}")
endif()
string(REGEX MATCH "100% \\([0-9]+ */ *[0-9]+\\) in '${MODULE}'" complete "${log}")
if(NOT complete)
    message(FATAL_ERROR "${MODULE} is not documented completely:\n${log}")
endif()
string(REPLACE "\n" ";" lines "${log}")
foreach(line IN LISTS lines)
    if(line MATCHES "[Ww]arning" AND NOT line MATCHES "could not find link destinations")
        message(FATAL_ERROR "haddock warned:\n${log}")
    endif()
endforeach()
message(STATUS "${complete}")
