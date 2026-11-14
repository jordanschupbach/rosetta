#!/usr/bin/env apl --script
⍝ APL names and values of the full GNU APL bindings of tests/apl_full_names.hpp
⍝ (module names, default prefix names∆). CTest runs
⍝   apl --script -f names.apl -f tests/test_apl_full_names.apl
⍝ and expects "ROSETTA PASS apl_full_names" (and no FAIL).

FAILURES←0
CHECKS←0

∇CHECK B;LABEL;GOT;WANT
 (LABEL GOT WANT)←B
 CHECKS←CHECKS+1
 →(GOT≡WANT)/0
 FAILURES←FAILURES+1
 'FAIL ',LABEL,': got ',(⍕GOT),', want ',⍕WANT
∇

∇FAILS B;LABEL;EXPR;KIND;FRAGMENT;EVENT;R;E;OK
 (LABEL EXPR KIND FRAGMENT EVENT)←B
 CHECKS←CHECKS+1
 R←⎕EC EXPR
 →(0=↑R)/CAUGHT
 FAILURES←FAILURES+1
 'FAIL ',LABEL,': no error'
 →0
CAUGHT:E←names∆last_error
 OK←((2⊃R)≡EVENT)∧(KIND≡↑E)∧(∨/FRAGMENT⍷2⊃E)∧∨/'names: '⍷2⊃E
 →OK/0
 FAILURES←FAILURES+1
 'FAIL ',LABEL,': got event ',(⍕2⊃R),' ',(↑E),' "',(2⊃E),'"'
∇

⍝ Defined functions: the names a C++ name becomes are APL names (3=⎕NC).
∇Z←DEFINED NAME
 Z←3=⎕NC NAME
∇

COLLECTED←⍬
∇COLLECT X
 COLLECTED←COLLECTED,X
∇
∇Z←GLUE B
 Z←(1⊃B),'+',2⊃B
∇
∇Z←Loud∆speak H
 Z←80
∇

∇TEST;W;R;BEFORE
 BEFORE←names∆live
 ⍝ ---------------------------------------------------------------- names
 CHECK 'case value' names∆value 1
 CHECK 'case Value' names∆Value 2
 CHECK 'case VALUE' names∆VALUE 3
 CHECK 'runtime Delete' (names∆Delete_ 1) 11
 CHECK 'runtime fn' (names∆fn_ 1) 21
 CHECK 'runtime live' names∆live_ 30
 CHECK 'runtime names stay' (DEFINED¨'names∆Delete' 'names∆fn' 'names∆live' 'names∆call' 'names∆last_error') (1 1 1 1 1)
 CHECK 'leading underscore' names∆_hidden 4
 CHECK 'double underscore' names∆two__parts 5
 CHECK 'trailing underscore' names∆trailing_ 6
 CHECK 'digit' names∆v2x 7
 CHECK 'module' (names∆stats∆mean 1 2 3) 2
 CHECK 'module tools' names∆tools∆depth 2
 W←names∆Words∆new ⍬
 CHECK 'method subclass renamed' (names∆Words∆subclass_ W) 1
 CHECK 'method set' (names∆Words∆set W) 2
 CHECK 'method plus' (W names∆Words∆plus 1) 101
 CHECK 'operator beside method' (W names∆Words∆op∆plus 1) 201
 CHECK 'camelCase' (names∆Words∆camelCase W) 3
 CHECK 'CamelCase' (names∆Words∆CamelCase W) 4
 CHECK 'static' names∆Words∆create 5
 CHECK 'field' (names∆Words∆count W) 6
 W names∆Words∆count∆set 9
 CHECK 'field set' (names∆Words∆count W) 9
 CHECK 'nested enum' names∆Words_Mode∆on 3
 CHECK 'nested enum argument' (W names∆Words∆mode names∆Words_Mode∆on) 3
 CHECK 'nested enum by name' (W names∆Words∆mode 'on') 3
 FAILS 'unknown enumerator' 'W names∆Words∆mode ''up''' 'binding' 'unknown enumerator up' (5 4)
 CHECK 'virtual' (names∆speak_of W) 8
 names∆Delete W
 W←'Loud' names∆Words∆subclass ⍬
 CHECK 'APL implementation' (names∆speak_of W) 80
 names∆Delete W
 ⍝ ---------------------------------------------------------------- values
 CHECK 'quote default' (names∆quoted ⍬) 'it''s!'
 CHECK 'quote given' (names∆quoted 'a') 'a!'
 CHECK 'updated arguments' (names∆bounds (3 1 2) 0 0) (1 3)
 CHECK 'result and updated argument' (names∆increment 4) (5 5)
 FAILS 'updated argument error' 'names∆bounds ⍬ 0 0' 'invalid_argument' 'no values' (5 4)
 CHECK 'complex' (names∆twice 1J2) 2J4
 CHECK 'complex from real' (names∆twice 3) 6
 CHECK 'set' (names∆unique 3 1 3 2) (1 2 3)
 CHECK 'tuple' names∆triple (1 'two' 1)
 CHECK 'int64 min' names∆smallest ¯9223372036854775808
 CHECK 'uint64' (names∆echo_u64 5) 5
 FAILS 'uint64 negative' 'names∆echo_u64 ¯1' 'binding' 'outside' (5 4)
 CHECK 'int8' (names∆echo_i8 ¯128) ¯128
 FAILS 'int8 range' 'names∆echo_i8 128' 'binding' 'outside C++ type range' (5 4)
 FAILS 'fraction' 'names∆echo_i8 1.5' 'binding' 'fraction' (5 4)
 CHECK 'optional' (names∆maybe 4) 4
 CHECK 'optional none' (names∆maybe ⍬) ⍬
 CHECK 'variant int' (names∆kind_of_value 1) 'int'
 CHECK 'variant string' (names∆kind_of_value 'x') 'string'
 CHECK 'map result' names∆numbered (2 2⍴1 'one' 2 'two')
 CHECK 'map from matrix' (names∆total 2 2⍴'a' 1.5 'b' 2) 3.5
 CHECK 'map from pairs' (names∆total ('a' 1) ('b' 2)) 3
 CHECK 'empty map' (names∆total ⍬) 0
 CHECK 'boolean' (names∆negate 0) 1
 FAILS 'boolean range' 'names∆negate 2' 'binding' 'boolean' (5 4)
 CHECK 'boolean vector' names∆flags (1 0 1)
 CHECK 'strings' names∆words ((,'a') 'bb')
 CHECK 'matrix result' names∆grid (2 3⍴⍳6)
 CHECK 'ragged result' names∆ragged ((,1) (2 3))
 CHECK 'matrix argument' (names∆sum_matrix 2 2⍴1 2 3 4) 10
 CHECK 'nested argument' (names∆sum_matrix (1 2) (,3)) 6
 CHECK 'float pair result' (names∆halves 2.5) (1.25 1.25)
 CHECK 'float pair argument' (names∆sum_pair 1.25 2.75) 4
 CHECK 'pair round trip' (names∆sum_pair names∆halves 3) 3
 CHECK 'integer pair argument' (names∆sum_pair 1 2) 3
 CHECK 'vector of float pairs' (names∆sum_pairs (0.5 1.5) (2.5 3.5)) 8
 CHECK 'matrix of float pairs' (names∆sum_pairs 2 2⍴0.5 1.5 2.5 3.5) 8
 CHECK 'mixed tuple' (names∆sum_triple 0.5 2 1.5) 4
 FAILS 'pair length' 'names∆sum_pair 1.5 2.5 3.5' 'binding' '2 elements' (5 4)
 ⍝ ---------------------------------------------------------------- callbacks
 CHECK 'void callback' (names∆each (1 2 3) (names∆fn 'COLLECT')) 3
 CHECK 'void callback effect' COLLECTED (1 2 3)
 CHECK 'two-argument callback' (names∆join ((,'a') 'bb' (,'c')) (names∆fn 'GLUE')) 'a+bb+c'
 FAILS 'callback name' 'names∆each (1 2) (names∆fn ''1x'')' 'binding' 'invalid APL function name' (5 4)
 ⍝ ---------------------------------------------------------------- error kinds and events
 FAILS 'invalid_argument' 'names∆fail ¯1' 'invalid_argument' 'negative' (5 4)
 FAILS 'runtime_error' 'names∆fail 0' 'native' 'zero' (5 4)
 FAILS 'overflow_error' 'names∆fail 1' 'arithmetic_error' 'one' (5 4)
 FAILS 'out_of_range' 'names∆fail 2' 'out_of_range' 'two' (5 5)
 FAILS 'length_error' 'names∆fail 3' 'length_error' 'three' (5 3)
 CHECK 'no error' (names∆fail 4) 4
 CHECK 'every handle released' names∆live BEFORE
∇

∇FINISH
 'Checks: ',⍕CHECKS
 →(0≠FAILURES)/0
 'ROSETTA PASS apl_full_names'
∇

TEST
FINISH
)OFF
