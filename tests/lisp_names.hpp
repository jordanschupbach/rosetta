#pragma once

// These COMMON-LISP names must bind in the generated package without
// touching CL:LIST or CL:CAR.
inline int list(int value = 42) { return value; }
inline int car(int value) { return value; }
