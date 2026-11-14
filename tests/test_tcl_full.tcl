# Full Tcl API (--tcl-api full) on the library-shaped example shared with the Python, C#, Ruby,
# Julia, Common Lisp and Fortran backends. Argument: the package directory (features/, holding
# pkgIndex.tcl, features.tcl and the native extension).
set package_dir [file normalize [lindex $argv 0]]
set auto_path [linsert $auto_path 0 [file dirname $package_dir]]
package require features
namespace import ::features::*

set checks 0
proc check {ok what} {
    incr ::checks
    if {![uplevel 1 [list expr $ok]]} { error "check failed: $what" }
}
# fails pattern ?errorcode-prefix? script: the script raises an error whose message contains
# pattern and whose -errorcode starts with the given words.
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
proc near {a b} { expr {abs($a - $b) < 1e-9} }

set doc_file [file join $package_dir features.tcl]
set fd [open $doc_file]; fconfigure $fd -encoding utf-8
set source_lines [split [read $fd] \n]
close $fd
# The comment block right above the first non-comment line containing declaration (searching
# from the line containing after).
proc doc {declaration {after {}}} {
    set from 0
    if {$after ne {}} { set from [lsearch -glob $::source_lines "*$after*"] }
    for {set at $from} {$at < [llength $::source_lines]} {incr at} {
        set line [lindex $::source_lines $at]
        if {[string first $declaration $line] >= 0 && ![string match "#*" [string trimleft $line]]} break
    }
    if {$at >= [llength $::source_lines]} { error "not declared: $declaration" }
    set block {}
    for {set i [expr {$at - 1}]} {$i >= 0} {incr i -1} {
        set line [string trim [lindex $::source_lines $i]]
        if {![string match "#*" $line]} break
        set block [linsert $block 0 [regsub {^# ?} $line {}]]
    }
    join $block \n
}
proc has {text part} { expr {[string first $part $text] >= 0} }

# Directors: Tcl subclasses implement C++ virtual methods.
oo::class create Triangle {
    superclass ::features::Shape
    variable base height
    constructor {b h} { next; set base $b; set height $h }
    method area {} { expr {$base * $height / 2.0} }
    method name {} { return triangle }
}
oo::class create Incomplete { superclass ::features::Shape }
oo::class create Failing {
    superclass ::features::Shape
    method area {} { error "from Tcl" {} {MINE failing} }
}
# next runs the native implementation rather than dispatching back to the override.
oo::class create Loud {
    superclass ::features::Circle
    method name {} { string toupper [next] }
}
oo::class create Noisy {
    superclass ::features::Circle
    method name {} { return "[string tolower [next]]!" }
}
oo::class create Louder { superclass Noisy }
oo::class create UnitShape {
    superclass ::features::Shape
    variable tag
    method area {} { return 1.0 }
    method tag {args} { if {[llength $args]} { set tag [lindex $args 0] }; return $tag }
}
oo::class create Summer {
    superclass ::features::Visitor
    variable seen
    method visit {value} { expr {$value * 10} }
    method finished {values} { set seen $values; return }
    method seen {} { return $seen }
}
oo::class create NoNext {
    superclass ::features::Shape
    constructor {} { set forgot next } ;# (an empty body would delete the constructor)
}

# --- Classes, inheritance and virtual dispatch ----------------------------------------------------
set c1 [Circle new 2]
check {[$c1 area] == 12 && [$c1 name] eq "circle" && [$c1 twice_area] == 24 && [$c1 radius] == 2} "circle methods"
check {[info object class $c1] eq "::features::Circle"} "object class"
check {[info object isa typeof $c1 ::features::Shape] && [info object isa typeof $c1 ::features::NativeObject]} "class hierarchy"
check {[info class superclasses ::features::Circle] eq "::features::Shape"} "superclass"
check {[info class superclasses ::features::Shape] eq "::features::NativeObject"} "NativeObject root"
set o [fails "negative radius" {FEATURES invalid_argument std::invalid_argument} { Circle new -1 }]
check {[lindex [dict get $o -errorcode] end] eq "negative radius"} "errorcode message"
set s [Square new]
check {[$s side] == 1 && [$s area] == 1} "default constructor"
$s side 3
check {[$s area] == 9 && [[Square new 2] area] == 4} "field set"
check {[area_of $s] == 9 && [describe $c1] eq "circle:0"} "functions on classes"
check {[::features::area_of $s] == 9} "qualified command"
check {[[Circle new 2.5] radius] == 2.5 && [[Circle new 3] radius] == 3.0} "integers convert to double"
fails "expected a number" {FEATURES binding} { Circle new two }
fails "wrong # args" {FEATURES binding} { Circle new }
fails "cannot be created from Tcl" {FEATURES binding} { Target new }
check {[$s cget -side] == 3} "cget"
$s configure -side 4
check {[$s side] == 4 && [$s configure] eq {-side 4.0}} "configure"
check {[$s configure -side] == 4} "configure one option"
fails "unknown option \"-bogus\"" { $s cget -bogus }
fails "unknown method \"bogus\"" { $s bogus }
fails "wrong # args" { $s side 1 2 }

# --- Enums --------------------------------------------------------------------------------------
check {[$c1 kind] eq "circle" && [$s kind] eq "square"} "enum results are names"
check {$::features::Kind(circle) == 0 && $::features::Kind(custom) == 7} "enum arrays"
check {[filled solid] && ![filled None] && [filled $::features::Fill(solid)]} "enum arguments (name or value)"
check {[lsort [array names ::features::Kind]] eq {circle custom square}} "enum names"
fails "unknown enumerator \"hexagon\"" { make_shape hexagon 1 }
fails "unknown enumerator" { make_shape {} 1 }
check {$::features::Legacy(legacy_a) == 1} "unscoped enum"
check {$::features::Shape::sides_unknown == -1 && [near $::features::golden 1.618]} "constants"
fails "is a constant" { set ::features::golden 2 }
check {[near $::features::golden 1.618]} "constant unchanged"
unset ::features::golden
check {[near $::features::golden 1.618]} "constant survives unset"

# --- Most-derived results, ownership transfer and shared ownership -------------------------------
set made [make_shape circle 1]
check {[info object class $made] eq "::features::Circle" && [$made radius] == 1} "most-derived result"
check {[info object class [make_shape square 2]] eq "::features::Square"} "most-derived square"
check {[$made owned] && [$made alive]} "owned result"
set canvas [Canvas new]
$canvas add [make_shape square 2]
$canvas share [shared_square 3]
set transferred [Circle new 1]
$canvas add $transferred
fails "closed" { $transferred area }
check {![$transferred alive]} "transferred object is closed"
check {[$canvas size] == 3 && [$canvas total] == 4 + 9 + 3} "ownership transfer"
set first [$canvas at 0]
check {[info object class $first] eq "::features::Square" && [$first area] == 4 && ![$first owned]} "borrowed element"
check {[$canvas at 0] eq $first} "the same borrowed object comes back"
fails "cannot transfer ownership" { $canvas add $first }
set shared [shared_square 2]
$canvas share $shared
check {[$shared area] == 4 && [$canvas total] == 20} "shared ownership"
$shared destroy
check {[$canvas total] == 20} "C++ keeps a shared object after destroy"
check {[total_area [list [Circle new 1] [Square new 2]]] == 7} "list of objects"
fails "::features::Shape object" { total_area notanobject }
$first destroy
check {[$canvas total] == 20 && [[$canvas at 0] area] == 4} "destroying a borrowed object leaves the C++ object"

# --- Overload resolution --------------------------------------------------------------------------
check {[pick 1] eq "int" && [pick 1.5] eq "double" && [pick 2.0] eq "double" && [pick x] eq "string"} "scalar overloads"
check {[pick [list 1.0 2.0]] eq "vector" && [pick $s] eq "shape"} "container and class overloads"
check {[pick {1 2}] eq "vector"} "a list of several elements"
check {[pick 0x10] eq "int" && [pick 1e3] eq "double"} "number spellings"
fails "wrong # args: should be one of" { pick a b c }
fails "no overload accepts" { pick [Vec2 new] }
check {[add 1] == 111 && [add 1 2] == 103 && [add 1 2 3] == 6} "default arguments"
fails "wrong # args" { add }
fails "wrong # args" { add 1 2 3 4 }
check {[[Square new 2] area] == 4 && [[Square new] area] == 1} "constructor overloads"

# --- Integer range checks and bigints -------------------------------------------------------------
fails "outside the range" { add [expr {2**40}] }
fails "outside the range" { add [expr {2**80}] }
fails "expected an integer" { add 1.5 }
check {[add [expr {-(2**31)}] 0 0] == -(2**31)} "int minimum"
check {[add 0x7fffffff 0 0] == 2147483647} "int maximum"
fails "outside the range" { add 0x80000000 0 0 }

# --- Containers -----------------------------------------------------------------------------------
set counts [count_words {a b a}]
check {[dict get $counts a] == 2 && [dict get $counts b] == 1 && [dict size $counts] == 2} "dict result"
check {[min_max {3.0 1.0 2.0}] eq {1.0 3.0}} "pair as list"
check {[min_max {3 1 2}] eq {1.0 3.0}} "integer elements convert"
fails "empty input" {FEATURES out_of_range} { min_max {} }
check {[find {5 7} 7] == 1 && [find {5 7} 9] eq ""} "optional result is the empty string"
set tr [transpose {{1.0 2.0 3.0} {4.0 5.0 6.0}}]
check {[llength $tr] == 3 && [lindex $tr 0] eq {1.0 4.0}} "nested lists"
check {[byte_sum {1 2 250}] == 253} "bytes from a list"
check {[byte_sum [binary format c2 {1 2}]] == 3} "bytes from a byte array"
check {[bytes_of AZ] eq {65 90}} "bytes result"
fails "outside the range" { byte_sum {256} }
set cs [circles 3]
check {[llength $cs] == 3 && [[lindex $cs 2] radius] == 3} "list of class results"
check {[info object class [lindex $cs 0]] eq "::features::Circle"} "list elements are objects"
check {[repeat z 3] eq "zzz" && [initial hello] eq "h"} "chars"
check {[initial "héllo"] eq "h" && [dict get [count_words {ü ü}] ü] == 2} "UTF-8 round trip"
fails "expected a single ASCII character" { repeat zz 3 }
set values {1.0 2.0}
scale values 3
check {$values eq {3.0 6.0}} "mutable reference argument written back to the variable"
fails "no such variable" { scale nosuchvariable 2 }
set o [fails "negative count" {FEATURES RuntimeError} { repeat z -1 }]
check {[lindex [dict get $o -errorcode] 2] eq "std::invalid_argument"} "policy exception kind keeps the C++ type"
check {[defaults] eq "x*1-1.500000"} "all defaults"
check {[defaults circle q] eq "q*0-1.500000"} "positional defaults"
check {[defaults custom x 0 1.0] eq "x*7-1.000000"} "more positional defaults"
check {[has [join $source_lines \n] "proc defaults \{\{kind square\} \{label x\} \{loud false\} \{weight 0.5\}"]} "defaults in the stub"
check {[has [doc "proc defaults "] "weight - double, default 0.5"]} "defaults documented"
check {[defaults square {a $b [c] {d}}] eq {a $b [c] {d}*1-1.500000}} "strings with Tcl specials"

# --- Callbacks both ways --------------------------------------------------------------------------
check {[features::apply {::tcl::mathop::* 2} 21] == 42} "command prefix callback"
check {[features::apply [list ::apply {x {expr {$x + 1}}}] 1] == 2} "lambda callback"
proc twice {x} { expr {$x * 2} }
check {[features::apply twice 5] == 10} "proc callback"
fails "inside callback" {MINE inside} { features::apply [list ::apply {x {error "inside callback" {} {MINE inside}}}] 1 }
set o [fails "divide by zero" {ARITH DIVZERO} { features::apply [list ::apply {x {expr {int($x) / 0}}}] 1 }]
check {[string match "*expr*" [dict get $o -errorinfo]]} "errorinfo of the callback survives"
fails "does not convert" { features::apply [list ::apply {x {return nope}}] 1 }
check {[catch {features::apply [list ::apply {x {return -code break}}] 1}] == 3} "break through C++"
set plus [adder 40]
check {[{*}$plus 2] == 42} "returned function is a command"
check {[string match "::features::*" $plus]} "returned command lives in the namespace"
fails "expected an integer, got \"2.0\"" { features::apply $plus 2 } ;# doubles do not truncate
check {[features::apply [list ::apply {x {{*}[::features::adder 1] [expr {int($x)}]}}] 3] == 4} "native function inside a callback"
fails "wrong # args" { {*}$plus }
rename $plus {}
check {[info commands $plus] eq ""} "returned function deleted"
fails "expected a command prefix, got \"nosuchcommand\"" { features::apply nosuchcommand 1 }
fails "empty native function" { features::apply {} 1 } ;# the empty string is an empty std::function

# --- Operators, chaining and borrowed references --------------------------------------------------
set v [Vec2 new 1 2]
set w [$v add [Vec2 new 3 4]]
check {[$w x] == 4 && [$w y] == 6 && [[$v mul 2] y] == 4 && [[$v neg] x] == -1 && [[$v sub $w] x] == -3} "operators"
check {[$v eq [Vec2 new 1 2]] && ![$v eq $w]} "equality"
check {![$v eq 5] && ![$v eq x]} "equality with other values is false"
check {[$v index 1] == 2 && [$v inner [Vec2 new 1 1]] == 3} "index"
fails "Vec2 index" {FEATURES out_of_range} { $v index 2 }
set chained [[$v set_x 10] set_y 20]
check {[$v x] == 10 && [$v y] == 20 && [$chained x] == 10} "chaining"
check {$chained eq $v} "a returned *this is the same object"
$chained x 11
check {[$v x] == 11} "chained result borrows the same object"
check {[lsearch [info object methods $v -all] iadd] < 0} "compound assignment stays C++-only"
set seg [Segment new]
$seg end [Vec2 new 3 4]
check {[$seg length2] == 25} "field object set"
set start [$seg first]
$start x 3
check {[[$seg start] x] == 3 && [$seg length2] == 16} "borrowed reference writes through"
[$seg start] y 4
check {[$seg length2] == 0} "field object borrowed"
check {[[[Vec2 new 1 2] clone] y] == 2} "clone"
set orig [Vec2 new 5 6]
set copy [oo::copy $orig]
$copy x 9
check {[$orig x] == 5 && [$copy x] == 9 && [$copy owned]} "oo::copy copies the C++ object"
check {[sum [list [Vec2 new 1 2] [Vec2 new 3 4]]] == 10} "list of values"
set held [[Segment new] first]
check {[$held x] == 0} "borrowed reference keeps its owner alive"

# --- Templates, extensions, qualified names, copy types -------------------------------------------
set stack [IntStack new]
$stack push 1
$stack push 2
check {[$stack size] == 2 && [$stack items] eq {1 2} && [$stack pop] == 2} "template instance"
check {[clamp_int 42] == 10 && [clamp_int 3 5] == 3 && [clamp_real 2.5] == 2.5} "function templates"
check {[$c1 label] eq "#circle" && [$c1 label @] eq "@circle" && [[Circle unit] radius] == 1} "extensions"
check {[features__legacy__version] == 2 && [[features__legacy__Tag new] id] == 7} "qualified names"
check {$::features::features__legacy__Mode(off) == 1} "qualified enum"
check {[[Circle new 2] diameter] == 4} "extension method"
check {[features__legacy__area_of [Circle new 1]] == 3} "re-exported function"
set ledger [Ledger new]
set latest [$ledger latest]
$latest value 5
check {[[$ledger stamp] value] == 1 && [$latest owned]} "copy type"
set aim [Aim new]
[$aim origin] x 2
check {[[[$aim target] point] x] == 2} "nested borrowed fields"
set target [$aim target]
fails "read-only" { $target point [Vec2 new] }
fails "read-only" { $target configure -point [Vec2 new] }
$target scale 2.5
check {[$target scale] == 2.5} "value field has a setter"

# --- Errors, destroying and lifetime ---------------------------------------------------------------
fails "::features::Shape object" {FEATURES binding} { area_of {} }
fails "::features::Shape object" { area_of [Vec2 new 1 2] }
fails "::features::Shape object" { area_of nosuchobject }
set x [Circle new 1]
check {[$x area] == 3} "before destroy"
$x destroy
check {[info commands $x] eq ""} "destroy deletes the command"
fails "invalid command name" { $x area }
set y [Circle new 1]
rename $y {}
check {[info commands $y] eq ""} "rename {} deletes"
for {set i 1} {$i <= 200} {incr i} {
    set tmp [make_shape square $i]
    check {[$tmp area] == $i * $i} "many objects"
    $tmp destroy
}
set errorcode_kinds {}
foreach script {{Circle new -1} {min_max {}} {$v index 9}} {
    catch $script m o
    lappend errorcode_kinds [lindex [dict get $o -errorcode] 1]
}
check {$errorcode_kinds eq {invalid_argument out_of_range out_of_range}} "errorcode kinds"

# --- Directors --------------------------------------------------------------------------------------
set t [Triangle new 4 3]
check {[$t area] == 6 && [area_of $t] == 6 && [$t twice_area] == 12 && [describe $t] eq "triangle:7"} "Tcl override called from C++"
check {[total_area [list $t [Circle new 1] [Square new 2]]] == 13 && [$t label @] eq "@triangle"} "mixed overrides"
check {[$t kind] eq "custom"} "native default implementation of a virtual"
fails "pure virtual" {FEATURES not_implemented} { area_of [Incomplete new] }
fails "pure virtual" { [Incomplete new] area }
set o [fails "from Tcl" {MINE failing} { area_of [Failing new] }]
check {[string match "*from Tcl*" [dict get $o -errorinfo]]} "override errorinfo"
fails "abstract" { Shape new }
check {[describe [Loud new 1]] eq "CIRCLE:0" && [[Loud new 2] area] == 12} "next runs the C++ method"
check {[describe [Louder new 1]] eq "circle!:0"} "deeper subclasses"
fails "next" { [NoNext new] area }
set canvas [Canvas new]
set u [UnitShape new]
$u tag kept
$canvas add $u
check {[$u area] == 1 && ![$u owned]} "subclass object owned by C++"
$canvas share [Square new 2]
check {[$canvas total] == 5 && [$canvas size] == 2} "C++ calls the Tcl object it owns"
set back [$canvas at 0]
check {$back eq $u && [$back tag] eq "kept"} "the same Tcl object comes back"
$canvas clear
check {[info commands $u] eq ""} "C++ deleting a subclass object deletes its command"
set u2 [UnitShape new]
$canvas share $u2
$u2 destroy
fails "pure virtual" { $canvas total }
$canvas clear
set summer [Summer new]
check {[run_visitor $summer {1.0 2.0}] == 30 && [$summer seen] eq {1.0 2.0}} "callback interface"
check {[run_visitor [Visitor new] {1.0 2.0}] == 3} "native default implementation"

# --- Documentation ----------------------------------------------------------------------------------
set d [doc "proc add "]
check {[has $d "add -- Adds three integers, Socha & Dorigo style."]} "summary"
check {[has $d "a - int: first term"]} "parameter"
check {[has $d "b - int, default 10: second term"]} "parameter default"
check {[has $d "result: int - the sum"]} "result"
check {[has $d "{FEATURES out_of_range}"]} "errors"
check {[has $d "example:\n    int total = add(1);"]} "example"
check {[has $d "see: pick"]} "see"
check {[has $d "note: Defaults keep the result small."]} "note"
check {[has $d "usage: features::add a ?b? ?c?"]} "usage"
check {[has [doc "constructor {radius} {}"] "{FEATURES invalid_argument}"]} "constructor errors"
check {[has [doc "method x {{value {}}} {}"] "Horizontal component."]} "field documentation"
check {[has [doc "oo::class create Shape "] "Subclass it in Tcl"]} "class remarks"
check {[has [doc "proc pick "] "features::pick arg0:list of double -> string"]} "overloads"
check {[has [doc "proc count_words "] "dict of string -> int"]} "dict type"
check {[has [doc "proc find "] "int or {}"]} "optional type"
check {[has [doc "proc min_max "] "list {double double}"]} "pair type"
check {[has [doc "proc apply "] "command prefix (called with double, returns double)"]} "callback type"
check {[has [doc "proc transpose "] "list of list of double"]} "nested type"
check {[has [doc "proc filled "] "one of None, solid"]} "enum type"
check {[has [doc "variable Kind"] "enumerator names"]} "enum representation documented"
check {[has [doc "method area {} {}" "oo::class create Circle"] "area --"]} "method docs"
check {[has [doc "oo::objdefine Circle method unit"] "usage: features::Circle unit"]} "static method docs"
check {[lsearch -exact $source_lines "if \{0\} \{"] > 0} "docs are not executed"
check {[lsearch -glob $source_lines "load *"] > 0 && [lsearch -glob $source_lines "package provide features 1.0"] > 0} "loads and provides"
# Layout: every stub (proc, constructor, method, class) directly follows its comment block.
set stubs 0
set inside [lsearch -exact $source_lines "if \{0\} \{"]
for {set i $inside} {$i < [llength $source_lines]} {incr i} {
    set line [string trim [lindex $source_lines $i]]
    if {[regexp {^(proc|method|constructor|oo::class create|oo::objdefine \S+ method) } $line]} {
        incr stubs
        set above [string trim [lindex $source_lines $i-1]]
        if {![string match "#*" $above] && ![string match "superclass *" $above]} { error "no comment block above: $line" }
    }
}
check {$stubs > 100} "every stub has a comment block ($stubs stubs)"
check {[info complete [join $source_lines \n]]} "the package file is a complete script"
check {[info procs ::features::add] eq "" && [info commands ::features::add] ne ""} "stubs do not replace native commands"
set interp [interp create]
$interp eval [list set auto_path $auto_path]
check {[$interp eval {package require features}] eq "1.0"} "a second interpreter loads the package"
check {[$interp eval {[features::Circle new 2] area}] == 12} "a second interpreter works"
interp delete $interp
check {[[Circle new 1] area] == 3} "the first interpreter survives deleting the second"

# --- Policy modules ---------------------------------------------------------------------------------
set pt [::features::data::unit]
check {[$pt x] == 1 && [$pt y] == 0} "submodule result"
check {[::features::data::scaled 3] == 6 && [::features::data::scaled 2.5] == 6.25} "submodule overloads"
check {[::features::data::repeat ab] eq "abab" && [repeat x 3] eq "xxx"} "submodule names are separate"
check {[has [doc "proc unit " "namespace eval data"] "Mapped by the policy's modules"]} "submodule docs"

puts "Tcl full API passed ($checks checks)"
