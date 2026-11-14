#!/usr/bin/env apl --script
⍝ The generated GNU APL script of the full API loads cleanly (CTest fails on any
⍝ ERROR in the output) and documents every function: the ⍝ lines right after
⍝ each header (⎕CR row 2). CTest runs
⍝   apl --script -f features.apl -f tests/test_apl_full_docs.apl
⍝ and expects "ROSETTA PASS apl_full_docs".

∇Z←DOCUMENTED NAME;R
 R←⎕CR NAME
 Z←(1<↑⍴R)∧'⍝'=↑R[2;]
∇

∇CHECKDOCS;N;NAMES;OK;BAD
 N←⎕NL 3
 NAMES←{⍵~' '}¨⊂[2]N
 NAMES←(∨/¨(⊂'ft∆')⍷¨NAMES)/NAMES
 OK←DOCUMENTED¨NAMES
 BAD←(~OK)/NAMES
 'Functions: ',⍕⍴NAMES
 →(0=⍴BAD)/PASS
 'FAIL undocumented: ',⍕BAD
 →0
PASS:→(100>⍴NAMES)/TOOFEW
 →(3≠⎕NC 'ft⍙native')/NATIVE
 'ROSETTA PASS apl_full_docs'
 →0
TOOFEW:'FAIL too few functions'
 →0
NATIVE:'FAIL native function not loaded'
∇

CHECKDOCS
)OFF
