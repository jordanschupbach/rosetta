# Run with: just tcl-repl
package require tcl_demo
package require tcl_verify
puts "Rosetta Tcl examples loaded: namespaces ::tcl_demo and ::tcl_verify"
puts {set c [tcl_demo::r_demo__Counter new 20]; $c add 22; $c cget -count; $c close}
if {[info exists argv0] && [file normalize $argv0] eq [file normalize [info script]]} {
    # Started as a script: read commands interactively.
    set tcl_interactive 1
    while {1} {
        puts -nonewline "% "
        flush stdout
        if {[gets stdin line] < 0} break
        append command $line\n
        if {![info complete $command]} continue
        if {[catch {uplevel #0 $command} result]} { puts stderr $result } elseif {$result ne ""} { puts $result }
        set command ""
    }
}
