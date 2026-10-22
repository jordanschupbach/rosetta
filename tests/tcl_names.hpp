#pragma once

// These Tcl command names must bind inside ::tcl_names without replacing the
// global ::set or ::puts.
inline int set(int value = 42) { return value; }
inline int puts(int value) { return value; }
