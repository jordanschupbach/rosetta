# Tcl names and values (--tcl-api full) on tests/tcl_full_names.hpp: commands named like Tcl's
# own (set, list, string, info, rename, unknown, namespace, destroy, create), classes named like
# TclOO's, methods named like TclOO's methods, strings full of Tcl specials, flags, integer limits,
# dicts, tuples, variants and policy modules. Argument: the package directory (names/).
set package_dir [file normalize [lindex $argv 0]]
set auto_path [linsert $auto_path 0 [file dirname $package_dir]]
package require names

set checks 0
proc check {ok what} {
    incr ::checks
    if {![uplevel 1 [list expr $ok]]} { error "check failed: $what" }
}
proc fails {pattern args} {
    incr ::checks
    set script [lindex $args end]
    set code [expr {[llength $args] > 1 ? [lindex $args 0] : {}}]
    if {![catch {uplevel 1 $script} message options]} { error "expected an error containing \"$pattern\", got none ($message)" }
    if {[string first $pattern $message] < 0} { error "expected an error containing \"$pattern\", got: $message" }
    set got [dict get $options -errorcode]
    if {[llength $code] && [lrange $got 0 [llength $code]-1] ne $code} { error "expected -errorcode $code..., got $got ($message)" }
    return $options
}

# --- Commands named like Tcl's ----------------------------------------------------------------------
check {[names::set 1] == 2} "set"
check {[names::list 2] eq {7 7}} "list"
check {[names::string ab] eq "<ab>"} "string"
check {[names::info] == 42} "info"
check {[names::rename a b] eq "a->b"} "rename"
check {[names::unknown] == 13} "unknown"
check {[names::namespace x] eq "ns:x"} "namespace (renamed from namespace_)"
check {[names::destroy] == 1 && [names::create] == 2 && [names::next] == 3} "destroy, create, next"
# The built-ins are untouched, and `namespace import` skips names that would replace them.
check {[set x 5] == 5 && [llength [list a b]] == 2 && [string length abc] == 3} "built-ins unchanged"
namespace import ::names::*
check {[info commands ::set] eq "::set" && [set y 1] == 1 && [info exists y]} "import does not replace set"
check {[echo hi] eq "hi" && [quoted] eq {a {b} "c"}} "plain names are imported"
check {[lsort [namespace eval ::names {::namespace export}]] eq [lsort {Clash Factory Point Words all_permissions biggest call_point call_text call_void class create destroy echo either fail_custom fail_logic fail_unknown group_count groups hypot2 kind_of lookup maybe next object permission_bits quoted rotate say triple truth tiny unique uwide wide}]} "export list"

# --- Tcl specials in strings ------------------------------------------------------------------------
foreach text [list {$x} {[exit]} "\{" "\}" {a {b} c} "back\\slash" "\"quoted\"" {} " lead" "trail " "tab\there" "new\nline" ";" "#hash" "é中" "nul\x00byte"] {
    check {[echo $text] eq $text} "echo [string range $text 0 8]"
}
set want "a\$\[\{b\$\[\{c"
check {[names::join {a b c}] eq $want} "default separator with specials"
check {[names::join [list x y] { }] eq "x y"} "separator argument"
check {[quoted] eq "a {b} \"c\""} "default with braces and quotes"
check {[call_text [list ::apply {s {return "<$s>"}}]] eq {<$x [y] {z}>}} "callback receives specials unevaluated"
check {[call_text [list ::apply {s {return {[not evaluated]}}}]] eq {[not evaluated]}} "callback result not evaluated"

# --- Classes named like TclOO's ---------------------------------------------------------------------
set o [names::object new]
check {[$o value] == 1 && [info object class $o] eq "::names::object"} "class named object"
check {[[names::class new] value] == 2} "class named class (renamed from class_)"
check {[[names::dict new] size] == 0 && [dict size {a 1}] == 1} "class named dict leaves ::dict alone"
check {[info class superclasses ::names::object] eq "::names::NativeObject"} "root"

# --- Methods named like TclOO's ---------------------------------------------------------------------
set w [names::Words new]
check {[$w destroy_] == 1 && [$w eval_ 2] == 2 && [$w variable_] == 3 && [$w varname_] == 4} "reserved methods get a trailing _"
check {[$w unknown_] == 5 && [$w cget_] == 6 && [$w configure_] == 7 && [$w alive_] == 8 && [$w owned_] == 9} "runtime methods get a trailing _"
check {[$w next] == 10 && [$w self] == 11 && [$w my] == 12 && [$w set 4] == 8 && [$w create] == 14 && [$w new] == 15} "other names stay"
check {[$w alive] && [$w owned]} "runtime methods still work"
check {[names::Words new_count] == 16 && [names::Words create_count] == 17} "static methods"
check {[names::Factory create_ 1] == 101 && [names::Factory destroy_] == -1 && [names::Factory eval_] == -2} "static methods named like class methods"
set f [names::Factory create named_factory]
check {$f eq "::named_factory" && [$f made] == 0} "create still creates objects"
$f destroy
check {[info commands ::named_factory] eq ""} "destroy still destroys"
check {[info commands ::names::Factory] ne ""} "the class survives"
$w destroy
check {[info commands $w] eq ""} "destroy on an object whose class has destroy_"

# --- Fields colliding with methods ------------------------------------------------------------------
set c [names::Clash new]
check {[$c size] == 3 && [$c get_size] == 3} "field accessor"
check {[$c count] == 4 && [$c count_] == 40} "field and method side by side"
check {[$c cget -destroy] == 5} "a field named destroy is only reachable through cget"
$c configure -destroy 6 -size 7
check {[$c cget -destroy] == 6 && [$c size] == 7} "configure several fields"
check {[dict get [$c configure] -count] == 4} "configure lists fields"
$c destroy
check {[info commands $c] eq ""} "destroy is TclOO's"

# --- Flags, integers and booleans -------------------------------------------------------------------
check {[permission_bits read] == 1 && [permission_bits {read write}] == 3 && [permission_bits 5] == 5} "flags from names, lists and integers"
check {[all_permissions] == 7} "flag results are integers"
check {$::names::Permission(execute) == 4} "flag array"
check {[wide -9223372036854775808] == -9223372036854775808 && [wide 9223372036854775807] == 9223372036854775807} "int64 limits"
fails "outside the range" { wide 9223372036854775808 }
check {[uwide 18446744073709551615] == 18446744073709551615 && [biggest] == 18446744073709551615} "uint64 limits"
fails "outside the range" { uwide 18446744073709551616 }
fails "outside the range" { uwide -1 }
check {[tiny -128] == -128 && [tiny 127] == 127} "int8"
fails "outside the range" { tiny 128 }
check {[truth yes] == 0 && [truth 0] == 1 && [truth false] == 1} "booleans"
fails "expected a boolean" { truth maybe }

# --- Containers, variants, complex ------------------------------------------------------------------
set g [groups]
check {[dict get $g {a b}] eq {1 2} && [dict get $g {{c}}] eq {}} "dict with specials in keys"
check {[group_count $g] == 2 && [group_count {k {1 2 3}}] == 1} "dict argument"
fails "expected a dict" { group_count {a b c} }
check {[unique {b a b}] eq {a b}} "set as a sorted list"
check {[triple] eq {1 {two words} 3.5}} "tuple as list"
check {[either 1] == 5 && [either 0] eq "five"} "variant results"
check {[kind_of 5] eq "int" && [kind_of five] eq "string"} "variant arguments"
check {[rotate {1 0}] eq {0.0 1.0}} "complex as {re im}"
check {[maybe 1] eq "given" && [maybe 0] eq ""} "optional"
check {[hypot2 {3 4}] == 25} "pair argument"
fails "expected a list of two elements" { hypot2 {1 2 3} }

# --- Operators ---------------------------------------------------------------------------------------
set p [names::Point new]
set q [names::Point new]
$q x 5
check {[$p lt $q] && ![$q lt $p]} "lt"
check {[$p call 3] == 9} "operator() is call"
check {[call_point [list ::apply {pt {expr {[$pt x] * 10}}}]] == 10} "callback receives an object"

# --- Errors ------------------------------------------------------------------------------------------
check {[lookup one] == 1} "lookup"
fails "no key two" {NAMES KeyError std::out_of_range} { lookup two }
fails "bad logic" {NAMES logic_error std::logic_error} { fail_logic }
fails "custom failure" {NAMES runtime_error} { fail_custom }
fails "unknown C++ exception" {NAMES exception} { fail_unknown }
set count 0
call_void [list incr ::count] 3
check {$count == 3} "void callback"
fails "stop" {MINE} { call_void [list error stop {} MINE] 3 }
proc skip_it {} { return -code continue }
check {[catch {call_void skip_it 1}] == 4} "continue propagates"

# --- Policy modules ----------------------------------------------------------------------------------
check {[names::stats::mean {1 2 3}] == 2.0} "submodule"
check {[names::stats::set 2] == 200 && [names::set 2] == 3} "submodule keeps its own names"

# --- Documentation file -------------------------------------------------------------------------------
set fd [open [file join $package_dir names.tcl]]; fconfigure $fd -encoding utf-8
set text [read $fd]
close $fd
check {[info complete $text]} "the package file is a complete script"
check {[string first "proc set \{value\} \{\}" $text] > 0 && [string first "proc namespace \{name\} \{\}" $text] > 0} "stubs for commands named like Tcl's"
check {[string first "method destroy_ \{\} \{\}" $text] > 0} "stub for a renamed method"
set want "proc join \{parts \{separator \\\$\\\[\\\{\}\} \{\}"
check {[string first $want $text] > 0} "stub default with specials"
set doc_interp [interp create]
$doc_interp eval [list set argv0 [file join $package_dir names.tcl]]
set sourced [catch {$doc_interp eval [list source [file join $package_dir names.tcl]]} m]
check {$sourced == 0} "the package file sources cleanly ($m)"
check {[$doc_interp eval {names::set 9}] == 10} "sourcing loads the extension"
interp delete $doc_interp

# --- Native and Tcl output stay in order when stdout is a pipe ---------------------------------------
set child [file join [file dirname $package_dir] order_child.tcl]
set f [open $child w]
puts $f [list set auto_path [linsert $auto_path 0 [file dirname $package_dir]]]
puts $f {package require names; puts a; names::say "b\n"; puts c; names::say "d\n"}
close $f
set ordered [exec [info nameofexecutable] $child]
file delete $child
check {$ordered eq "a\nb\nc\nd"} "puts and std::cout output interleave in order ($ordered)"

puts "Tcl full names passed ($checks checks)"
