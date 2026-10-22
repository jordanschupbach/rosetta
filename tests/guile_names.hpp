#pragma once

// These names match Guile core procedures; importing the module with a
// prefix keeps the core bindings untouched.
inline int list(int value = 42) { return value; }
inline int display(int value) { return value; }
