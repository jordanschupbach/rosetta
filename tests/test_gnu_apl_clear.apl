#!/usr/bin/env apl --script
⍝ )CLEAR destroys the objects a library still owns; the library stays
⍝ loaded, so copying the bindings again finds none alive.
)COPY 1 apl_demo
C←apl_demo∆Counter∆new¨⍳100
apl_demo∆Counter∆live
)CLEAR
)COPY 1 apl_demo
∇CHECK
 →(0≠apl_demo∆Counter∆live)/0
 'ROSETTA PASS clear'
∇
CHECK
)OFF
