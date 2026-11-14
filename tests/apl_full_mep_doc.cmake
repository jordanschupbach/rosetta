# mep's APL language server shows the generated documentation of the full GNU APL bindings
# (ctest apl_full_mep_doc): `mep-apl-lsp --doc NAME` reads the ⍝ lines after each tradfn header
# from the APL files under ROOT (the generated features.apl, prefix ft).
#   cmake -DMEP_APL_LSP=... -DROOT=DIR -P tests/apl_full_mep_doc.cmake
set(checks
    "ft∆add|Adds three integers, Socha & Dorigo style.|Z←ft∆add a b c|`b` integer, default 10: second term|C++: `int features::add(int a, int b = 10, int c = 100)`"
    "ft∆Circle∆new|A circle with area 3r² (a crude π).|obj←ft∆Circle∆new radius|DOMAIN ERROR `ft: invalid_argument: …`"
    "ft∆Circle∆area|Area of the shape.|Z←ft∆Circle∆area obj"
    "ft∆Vec2∆x|Horizontal component.|obj ft∆Vec2∆x∆set value"
    "ft∆Visitor∆subclass|Impl∆method|`visit`"
    "ft∆Kind|Shape kinds.|`ft∆Kind∆custom` = 7"
    "ft∆defaults|Default values of every literal kind.|default ft∆Kind∆square|default 'x'|default ¯2"
    "ft∆Delete|GNU APL has no finalizers"
    "ft∆data∆unit|exported into the `data` submodule")
set(failed 0)
foreach(check IN LISTS checks)
    string(REPLACE "|" ";" parts "${check}")
    list(GET parts 0 name)
    list(REMOVE_AT parts 0)
    execute_process(COMMAND "${MEP_APL_LSP}" --doc --root "${ROOT}" "${name}"
        OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(SEND_ERROR "FAIL ${name}: mep-apl-lsp exited ${status}: ${err}")
        math(EXPR failed "${failed} + 1")
        continue()
    endif()
    foreach(fragment IN LISTS parts)
        string(FIND "${out}" "${fragment}" at)
        if(at EQUAL -1)
            message(SEND_ERROR "FAIL ${name}: no \"${fragment}\" in:\n${out}")
            math(EXPR failed "${failed} + 1")
        endif()
    endforeach()
endforeach()
list(LENGTH checks count)
if(failed EQUAL 0)
    message("ROSETTA PASS apl_full_mep_doc (${count} names)")
endif()
