#pragma once

// These Julia Base names must bind locally rather than extending Base methods.
inline int sum(int value = 42) { return value; }
inline int identity(int value) { return value; }
