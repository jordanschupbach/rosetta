# Read by TclExample.cmake on stdin: version, shared-library suffix, install prefix.
puts [info patchlevel]
puts [info sharedlibextension]
puts [file dirname [file dirname [file normalize [info nameofexecutable]]]]
