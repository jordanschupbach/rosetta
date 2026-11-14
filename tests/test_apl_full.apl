#!/usr/bin/env apl --script
⍝ Runtime tests of the full GNU APL bindings (--language apl --apl-api full) of
⍝ examples/python_features.hpp, generated with --apl-prefix ft. CTest runs
⍝   apl --script -f features.apl -f tests/test_apl_full.apl
⍝ and expects "ROSETTA PASS apl_full_features" (and no FAIL).

FAILURES←0
CHECKS←0

∇CHECK B;LABEL;GOT;WANT
 (LABEL GOT WANT)←B
 CHECKS←CHECKS+1
 →(GOT≡WANT)/0
 FAILURES←FAILURES+1
 'FAIL ',LABEL,': got ',(⍕GOT),', want ',⍕WANT
∇

⍝ Executes EXPR, which must signal EVENT with KIND and FRAGMENT in ft∆last_error.
∇FAILS B;LABEL;EXPR;KIND;FRAGMENT;EVENT;R;E;OK
 (LABEL EXPR KIND FRAGMENT EVENT)←B
 CHECKS←CHECKS+1
 R←⎕EC EXPR
 →(0=↑R)/CAUGHT
 FAILURES←FAILURES+1
 'FAIL ',LABEL,': no error'
 →0
CAUGHT:E←ft∆last_error
 OK←((2⊃R)≡EVENT)∧(KIND≡↑E)∧(∨/FRAGMENT⍷2⊃E)∧∨/'ft: '⍷2⊃E
 →OK/0
 FAILURES←FAILURES+1
 'FAIL ',LABEL,': got event ',(⍕2⊃R),' ',(↑E),' "',(2⊃E),'"'
∇

DOMAIN←5 4
INDEX←5 5

⍝ Callbacks and APL implementations of C++ classes (directors).
∇Z←DBL X
 Z←2×X
∇
∇Z←BAD X
 Z←X÷0
∇
∇Z←H TenTimes∆visit B
 Z←B×10
∇
SEEN←⍬
∇H TenTimes∆finished B
 SEEN←B
∇
∇Z←Unit∆area H
 Z←42
∇
∇Z←Unit∆name H
 Z←'unit'
∇
∇Z←H Boom∆visit B
 Z←B÷0
∇

∇TEST;C;S;S2;M;Q;K;A;P;P2;R;G;F;N;V;V0;MS;L;X;T;CS;BEFORE
 BEFORE←ft∆live
 ⍝ ---------------------------------------------------------------- enums
 CHECK 'enum circle' ft∆Kind∆circle 0
 CHECK 'enum custom' ft∆Kind∆custom 7
 CHECK 'enum table' ft∆Kind (3 2⍴(,'circle') 0 (,'square') 1 (,'custom') 7)
 CHECK 'unscoped enum' ft∆Legacy∆legacy_b 2
 CHECK 'enum argument' (ft∆filled ft∆Fill∆solid) 1
 CHECK 'enum by name' (ft∆filled 'solid') 1
 CHECK 'enum None' (ft∆filled ft∆Fill∆None) 0
 ⍝ ---------------------------------------------------------------- classes
 C←ft∆Circle∆new 2
 CHECK 'handle' (1⊃C) 'Circle'
 CHECK 'area' (ft∆Circle∆area C) 12
 CHECK 'radius' (ft∆Circle∆radius C) 2
 CHECK 'virtual name' (ft∆Circle∆name C) 'circle'
 CHECK 'enum result' (ft∆Circle∆kind C) 0
 CHECK 'base method' (ft∆Shape∆twice_area C) 24
 CHECK 'base class argument' (ft∆area_of C) 12
 CHECK 'describe' (ft∆describe C) 'circle:0'
 CHECK 'static constant' ft∆Shape∆sides_unknown ¯1
 FAILS 'constructor error' 'ft∆Circle∆new ¯1' 'invalid_argument' 'negative radius' DOMAIN
 FAILS 'abstract' 'ft∆Shape∆new ⍬' 'binding' 'abstract' DOMAIN
 S←ft∆Square∆new ⍬
 CHECK 'default constructor' (ft∆Square∆side S) 1
 S2←ft∆Square∆new 3
 CHECK 'overloaded constructor' (ft∆Square∆area S2) 9
 S2 ft∆Square∆side∆set 4
 CHECK 'field set' (ft∆Square∆area S2) 16
 FAILS 'field type' 'S2 ft∆Square∆side∆set ''x''' 'binding' 'field side' DOMAIN
 M←ft∆make_shape ft∆Kind∆circle 1
 CHECK 'most-derived result' (1⊃M) 'Circle'
 CHECK 'most-derived area' (ft∆Circle∆area M) 3
 Q←ft∆shared_square 2
 CHECK 'shared_ptr result' (1⊃Q) 'Square'
 CHECK 'vector of handles' (ft∆total_area C S2) 28
 CHECK 'one handle as a vector' (ft∆total_area C) 12
 ⍝ Ownership: the canvas takes M (unique_ptr) and shares Q.
 K←ft∆Canvas∆new ⍬
 K ft∆Canvas∆add M
 K ft∆Canvas∆share Q
 CHECK 'canvas size' (ft∆Canvas∆size K) 2
 CHECK 'canvas total' (ft∆Canvas∆total K) 7
 FAILS 'moved handle' 'ft∆Circle∆area M' 'binding' 'closed' DOMAIN
 A←K ft∆Canvas∆at 0
 CHECK 'borrowed reference' (ft∆Circle∆area A) 3
 FAILS 'out_of_range' 'K ft∆Canvas∆at 5' 'out_of_range' '' INDEX
 ft∆Delete A M Q
 ⍝ ---------------------------------------------------------------- overloads, defaults
 CHECK 'pick int' (ft∆pick 1) 'int'
 CHECK 'pick double' (ft∆pick 1.5) 'double'
 CHECK 'pick string' (ft∆pick 'abc') 'string'
 CHECK 'pick vector' (ft∆pick 1 2.5) 'vector'
 CHECK 'pick shape' (ft∆pick C) 'shape'
 CHECK 'add 1' (ft∆add 1) 111
 CHECK 'add 2' (ft∆add 1 2) 103
 CHECK 'add 3' (ft∆add 1 2 3) 6
 CHECK 'defaults' (ft∆defaults ⍬) 'x*1-1.500000'
 CHECK 'defaults kind' (ft∆defaults ft∆Kind∆circle) 'x*0-1.500000'
 CHECK 'defaults three' (ft∆defaults 0 'yy' 1) 'yy*0!-1.500000'
 FAILS 'no overload' 'ft∆add ''a''' 'binding' '' DOMAIN
 CHECK 'template function' (ft∆clamp_int 15) 10
 CHECK 'template function real' (ft∆clamp_real 2.5) 2.5
 ⍝ ---------------------------------------------------------------- containers
 CHECK 'updated argument' (ft∆scale (1 2 3) 2) (2 4 6)
 CHECK 'map result' (ft∆count_words 'a' 'bb' 'a') (2 2⍴(,'a') 2 'bb' 1)
 CHECK 'pair result' (ft∆min_max 3 1 2) (1 3)
 FAILS 'empty input' 'ft∆min_max ⍬' 'out_of_range' 'empty input' INDEX
 CHECK 'optional' (ft∆find (1 2 3) 2) 1
 CHECK 'optional none' (ft∆find (1 2 3) 9) ⍬
 CHECK 'matrix' (ft∆transpose 2 3⍴⍳6) (⍉2 3⍴⍳6)
 CHECK 'char result' (ft∆initial 'hello') (,'h')
 CHECK 'char argument' (ft∆repeat 'x' 3) 'xxx'
 FAILS 'invalid_argument' 'ft∆repeat ''x'' ¯1' 'invalid_argument' 'negative count' DOMAIN
 CS←ft∆circles 3
 CHECK 'vector of objects' (≢CS) 3
 CHECK 'vector of objects area' (ft∆Circle∆area 2⊃CS) 12
 ft∆Delete CS
 CHECK 'bytes' (ft∆byte_sum 1 2 3) 6
 CHECK 'bytes result' (ft∆bytes_of 'AB') (65 66)
 CHECK 'unicode' (ft∆data∆repeat '世界') '世界世界'
 ⍝ ---------------------------------------------------------------- value class, operators
 P←ft∆Vec2∆new 1 2
 CHECK 'field' (ft∆Vec2∆x P) 1
 P ft∆Vec2∆x∆set 5
 CHECK 'field set' (ft∆Vec2∆x P) 5
 P2←ft∆Vec2∆new 1 1
 R←P ft∆Vec2∆plus P2
 CHECK 'operator plus' ((ft∆Vec2∆x R),ft∆Vec2∆y R) (6 3)
 ft∆Delete R
 R←ft∆Vec2∆negate P
 CHECK 'unary operator' (ft∆Vec2∆x R) ¯5
 ft∆Delete R
 R←P ft∆Vec2∆times 2
 CHECK 'operator times' (ft∆Vec2∆y R) 4
 ft∆Delete R
 CHECK 'operator eq' (P ft∆Vec2∆eq P) 1
 CHECK 'operator eq false' (P ft∆Vec2∆eq P2) 0
 CHECK 'operator at' (P ft∆Vec2∆at 1) 2
 FAILS 'operator at range' 'P ft∆Vec2∆at 5' 'out_of_range' 'Vec2 index' INDEX
 R←P ft∆Vec2∆set_x 7
 CHECK 'chaining returns self' R P
 CHECK 'chained value' (ft∆Vec2∆x P) 7
 CHECK 'renamed method' (P ft∆Vec2∆inner P2) 9
 CHECK 'vector of values' (ft∆sum P P2) 11
 ⍝ A borrowed member keeps its segment alive.
 G←ft∆Segment∆new ⍬
 F←ft∆Segment∆first G
 F ft∆Vec2∆x∆set 3
 X←ft∆Segment∆start G
 CHECK 'borrowed member' (ft∆Vec2∆x X) 3
 ft∆Delete G X
 CHECK 'borrowed outlives owner' (ft∆Vec2∆x F) 3
 ft∆Delete F
 FAILS 'deleted handle' 'ft∆Vec2∆x F' 'binding' 'closed' DOMAIN
 ⍝ ---------------------------------------------------------------- templates, policy extensions, modules, constants
 T←ft∆IntStack∆new ⍬
 T ft∆IntStack∆push 1
 T ft∆IntStack∆push 2
 CHECK 'template class' (ft∆IntStack∆pop T) 2
 CHECK 'template items' (ft∆IntStack∆items T) (,1)
 R←ft∆IntStack∆pop T
 FAILS 'empty stack' 'ft∆IntStack∆pop T' 'out_of_range' 'empty stack' INDEX
 CHECK 'extension default' (C ft∆Shape∆label ⍬) '#circle'
 CHECK 'extension' (C ft∆Shape∆label '>') '>circle'
 CHECK 'extension self' (ft∆Circle∆diameter C) 4
 R←ft∆Circle∆unit
 CHECK 'static extension' (ft∆Circle∆radius R) 1
 ft∆Delete R
 CHECK 'constant' ft∆golden 1.618
 CHECK 'qualified' ft∆features__legacy__version 2
 R←ft∆data∆unit
 CHECK 'submodule' (ft∆Vec2∆x R) 1
 ft∆Delete R
 CHECK 'submodule overload int' (ft∆data∆scaled 2) 4
 CHECK 'submodule overload real' (ft∆data∆scaled 2.5) 6.25
 L←ft∆Ledger∆new ⍬
 R←ft∆Ledger∆latest L
 CHECK 'copy type' (ft∆Stamp∆value R) 1
 ft∆Delete R L
 ⍝ ---------------------------------------------------------------- callbacks
 CHECK 'callback' (ft∆apply (ft∆fn 'DBL') 3) 6
 FAILS 'callback error' 'ft∆apply (ft∆fn ''BAD'') 1' 'callback' 'DOMAIN ERROR' DOMAIN
 FAILS 'callback name' 'ft∆apply (ft∆fn ''NO∆SUCH'') 1' 'callback' 'VALUE ERROR in NO∆SUCH' (3 1)
 F←ft∆adder 5
 CHECK 'returned function' (1⊃F) 'NativeFunction'
 CHECK 'call returned function' (F ft∆call 3) 8
 ft∆Delete F
 ⍝ ---------------------------------------------------------------- APL implementations (directors)
 V←'TenTimes' ft∆Visitor∆subclass ⍬
 CHECK 'override' (ft∆run_visitor V (1 2 3)) 60
 CHECK 'override void' SEEN (1 2 3)
 CHECK 'direct call runs C++' (V ft∆Visitor∆visit 4) 4
 V0←ft∆Visitor∆new ⍬
 CHECK 'base implementation' (ft∆run_visitor V0 (1 2)) 3
 ft∆Delete V V0
 V←'NoOverrides' ft∆Visitor∆subclass ⍬
 CHECK 'not overridden' (ft∆run_visitor V (1 2)) 3
 ft∆Delete V
 MS←'Unit' ft∆Shape∆subclass ⍬
 CHECK 'abstract implemented' (ft∆area_of MS) 42
 CHECK 'abstract describe' (ft∆describe MS) 'unit:7'
 CHECK 'base method on implementation' (ft∆Shape∆twice_area MS) 84
 K ft∆Canvas∆add MS
 CHECK 'C++ owns the implementation' (ft∆Canvas∆total K) 49
 V←'Boom' ft∆Visitor∆subclass ⍬
 FAILS 'override error' 'ft∆run_visitor V (1 2)' 'callback' 'DOMAIN ERROR' DOMAIN
 ft∆Delete V
 V←'Nothing' ft∆Shape∆subclass ⍬
 FAILS 'pure virtual' 'ft∆area_of V' 'binding' 'pure virtual method area' DOMAIN
 ft∆Delete V
 ⍝ ---------------------------------------------------------------- errors and lifetime
 FAILS 'wrong argument' 'ft∆Circle∆area 5' 'binding' 'expected Circle' DOMAIN
 FAILS 'wrong count' 'ft∆add 1 2 3 4' 'binding' '' DOMAIN
 ft∆Delete C
 FAILS 'closed' 'ft∆Circle∆area C' 'binding' 'closed' DOMAIN
 ft∆Delete S S2 K P P2 T MS
 CHECK 'every handle released' ft∆live BEFORE
∇

∇FINISH
 'Checks: ',⍕CHECKS
 →(0≠FAILURES)/0
 'ROSETTA PASS apl_full_features'
∇

TEST
FINISH
)OFF
