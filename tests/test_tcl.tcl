# Runtime tests for the generated Tcl extensions.
# Usage: tclsh test_tcl.tcl FAMILY REPL_SCRIPT, with the build directory on TCLLIBPATH.
lassign $argv family repl
if {$family eq "repl"} {
    source $repl
} else {
    package require tcl_demo
    package require tcl_verify
}
package require tcl_names

set passes 0
set failures 0
proc report {ok description} {
    if {$ok} { incr ::passes } else { incr ::failures; puts "FAIL $description" }
}
proc check {condition} {
    report [uplevel 1 [list expr $condition]] $condition
}
proc fails {script fragment {kind ""}} {
    if {![catch {uplevel 1 $script} message options]} {
        report 0 "no error ($fragment) from: $script"
        return
    }
    report [expr {[string first $fragment $message] >= 0}] "$script: expected \"$fragment\" in \"$message\""
    if {$kind ne ""} {
        set code [dict get $options -errorcode]
        report [expr {$code eq [list ROSETTA $kind]}] "$script: expected errorCode {ROSETTA $kind}, got {$code}"
    }
}
proc v {name args} { tailcall ::tcl_verify::r_verify__$name {*}$args }
proc Counter {args} { tailcall ::tcl_demo::r_demo__Counter {*}$args }
proc Series {args} { tailcall ::tcl_verify::r_verify__Series {*}$args }
proc Worklist {args} { tailcall ::tcl_verify::r_verify__Worklist {*}$args }
proc AsyncTask {args} { tailcall ::tcl_verify::r_verify__AsyncTask {*}$args }
proc live {} { Counter live }

switch -- $family {
    scalars {
        check {[tcl_names::set] == 42}
        check {[tcl_names::puts 42] == 42}
        set untouched 42
        check {$untouched == 42 && [info commands ::tcl_names::set] eq "::tcl_names::set"}
        foreach name {char schar uchar short ushort int uint long ulong ll ull} {
            set name echo_$name
            check {[v $name 42] == 42}
            check {[v $name " 42 "] == 42}
            check {[v $name 0x2a] == 42}
            foreach bad {true 42.0 abc {} {1 2} 1e3} {
                fails {v $name $bad} "expected integer" conversion
            }
            fails {v $name [expr {2**64}]} "range" range
        }
        foreach {name bits} {schar 8 short 16 int 32 ll 64} {
            set low [expr {-2**($bits-1)}]
            set high [expr {2**($bits-1) - 1}]
            check {[v echo_$name $low] == $low}
            check {[v echo_$name $high] == $high}
            fails {v echo_$name [expr {$low - 1}]} "range" range
            fails {v echo_$name [expr {$high + 1}]} "range" range
        }
        foreach {name bits} {uchar 8 ushort 16 uint 32 ull 64} {
            set high [expr {2**$bits - 1}]
            check {[v echo_$name $high] == $high}
            fails {v echo_$name -1} "range" range
            fails {v echo_$name [expr {$high + 1}]} "range" range
        }
        set exact [expr {2**53 + 1}]
        check {[tcl_demo::r_demo__too_large] == $exact}
        check {[v echo_ull $exact] == $exact}
        check {[v echo_float 1.25] == 1.25}
        check {[v echo_float 2] == 2.0}
        check {[v echo_double 1.7976931348623157e308] == 1.7976931348623157e308}
        fails {v echo_float 1e39} "outside C++ type range" range
        fails {v echo_double Inf} "finite" range
        fails {v echo_double NaN} "Not a Number" conversion
        fails {v echo_double abc} "expected floating-point number" conversion
        fails {v nonfinite_result} "non-finite" range
        foreach {value expected} {true 1 false 0 yes 1 off 0 1 1 0 0} {
            check {[v echo_bool $value] == $expected}
        }
        fails {v echo_bool maybe} "expected boolean" conversion
        set long [string repeat long 10000]
        foreach text [list "" world "世界😀" "a\0b" $long [binary format c* {-1 -2}]] {
            check {[v echo_string $text] eq $text}
        }
        check {[string length [v echo_string "世界😀"]] == 3}
        check {[v embedded_nul] eq "a\0b" && [string length [v embedded_nul]] == 3}
        check {[v defaulted] == 42}
        check {[v defaulted 12] == 12}
        check {[v alias_identity 42] == 42}
        check {[v do_nothing] eq ""}
        fails {v echo_int} "wrong # args"
        fails {v echo_int 1 2} "wrong # args"
        catch {v echo_int} message options
        check {[dict get $options -errorcode] eq {TCL WRONGARGS}}
        for {set i 0} {$i < 20} {incr i} {
            fails {tcl_demo::r_demo__fail} "native failure" native
            fails {v unknown_error} "unknown C++ exception" native
            fails {v allocation_error} "allocation" allocation
            check {[tcl_demo::r_demo__add 20 22] == 42}
        }
        set trapped ""
        try { tcl_demo::r_demo__fail } trap {ROSETTA native} {message} { set trapped $message }
        check {$trapped eq "native failure"}
    }
    objects {
        check {[live] == 0}
        fails {Counter new -1} "negative initial" native
        check {[live] == 0}
        fails {Counter new} "wrong # args"
        fails {Counter nope} {bad subcommand "nope": must be new, answer, or live}
        set c [Counter new 20]
        set alias $c
        check {[string match {::tcl_demo::r_demo__Counter#*} $c]}
        check {[live] == 1}
        check {[$c add 22] == 42}
        check {[$c value] == 42}
        check {[$c cget -count] == 42 && [$c cget -initial] == 20}
        check {[Counter answer] == 42}
        $c configure -count 7
        check {[$alias value] == 7}
        set label "世界😀\0"
        $c configure -label $label
        check {[$alias cget -label] eq $label}
        check {[dict get [$c configure] -count] == 7 && [dict get [$c configure] -label] eq $label}
        fails {$c configure -initial 1} "readonly" conversion
        fails {$c configure -count true} "expected integer" conversion
        # Every value converts before any is assigned.
        fails {$c configure -count 99 -label x -count bad} "expected integer"
        check {[$c cget -count] == 7 && [$c cget -label] eq $label}
        fails {$c configure -count} "wrong # args"
        fails {$c configure -nope 1} {bad option "-nope": must be -count, -initial, or -label}
        fails {$c cget -nope} {bad option "-nope"}
        fails {$c nope} {bad method "nope": must be add, value, cget, configure, or close}
        fails {$c add 1 2} "wrong # args"
        fails {$c} "wrong # args"
        $c close
        check {[live] == 0}
        fails {$alias value} "invalid command name"
        fails {$c close} "invalid command name"
        # Deleting the command any other way also destroys the native object.
        set d [Counter new 1]
        rename $d ""
        check {[live] == 0}
        set e [Counter new 1]
        rename $e renamed_counter
        check {[renamed_counter add 1] == 2}
        renamed_counter close
        check {[live] == 0}
        # Deleting an interpreter destroys the objects it owns.
        interp create child
        child eval {package require tcl_demo; set kept [tcl_demo::r_demo__Counter new 1]}
        check {[live] == 1}
        check {[child eval {$kept add 41}] == 42}
        interp delete child
        check {[live] == 0}
        set objects [list [Series new] [Worklist new 2] [AsyncTask new 1 60000 false]]
        check {[Series live] == 1 && [AsyncTask live] == 1}
        foreach object $objects { $object close }
        check {[Series live] == 0 && [AsyncTask live] == 0 && [AsyncTask workers] == 0}
    }
    features {
        check {[v maximum 20 42] == 42}
        check {[v template_box 42] == 42}
        check {[v variadic_sum 10 20 12] == 42}
        check {[v constant_answer] == 42}
        check {[v constrained_sum 20 22] == 42}
        check {[v hierarchy_value 42] == 42}
        check {[v hierarchy_crosscast]}
        check {[v rosetta_r_c_identity 42] == 42}
        fails {v constrained_sum [expr {2**31 - 1}] 1} "overflow" range
        fails {Series new 0 -1} "series count" index
        set s [Series new]
        check {[$s size] == 2 && [$s get] == 20 && [$s scaled_sum 2] == 82}
        check {[$s text] eq {[20, 21]}}
        $s set 1 22
        check {[$s scaled_sum 1] == 42}
        fails {$s get 2} "out of range" index
        fails {$s set -1 1} "out of range"
        $s close
        foreach order {1 2 3 4} expected {{m z a} {a z m} {z m a} {a m z}} {
            set q [Worklist new $order]
            foreach x {a z m} { $q push $x }
            check {[$q size] == 3}
            check {[list [$q pop] [$q pop] [$q pop]] eq $expected}
            fails {$q pop} "empty" index
            $q close
        }
        fails {Worklist new 0} "unknown worklist order" argument
        set task [AsyncTask new 21 0 false]
        check {[$task wait 2000] && [$task result] == 42 && [$task result] == 42}
        $task close
        set task [AsyncTask new 1 0 true]
        check {[$task wait 2000]}
        fails {$task result} "async worker failure"
        $task close
        set task [AsyncTask new 1 60000 false]
        check {![$task ready] && ![$task wait 0] && [$task cancel] && [$task wait 2000]}
        fails {$task result} "cancelled"
        $task close
        fails {AsyncTask new 1 -1 false} "delay" index
        check {[AsyncTask workers] == 0}
        # Leave a 60s worker running at exit, bounded by the CTest timeout.
        set pending [AsyncTask new 1 60000 false]
    }
    repl {
        check {[tcl_demo::r_demo__add 20 22] == 42}
        set c [Counter new 20]
        check {[$c add 22] == 42}
        $c close
        set s [Series new]
        check {[$s scaled_sum 2] == 82}
        $s close
    }
    default { error "unknown family $family" }
}

puts "Tcl $family: $passes passed, $failures failed"
exit [expr {$failures == 0 ? 0 : 1}]
