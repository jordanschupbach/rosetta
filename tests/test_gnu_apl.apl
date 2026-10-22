#!/usr/bin/env apl --script
⍝ Runtime tests for the generated GNU APL bindings. CTest runs one family,
⍝ named by ROSETTA_APL_FAMILY, with APL_LIB_ROOT set to build/apl.
)COPY 1 apl_demo
)COPY 1 apl_verify
)COPY 1 apl_names

FAILURES←0
KINDS←'allocation' 'range' 'index' 'argument' 'native' 'closed'

∇CHECK B;LABEL;GOT;WANT
 (LABEL GOT WANT)←B
 →(GOT≡WANT)/0
 FAILURES←FAILURES+1
 'FAIL ',LABEL,': got ',(⍕GOT),', want ',⍕WANT
∇

⍝ Executes EXPR, which must signal KIND from MODULE with FRAGMENT in its message.
∇FAILS B;LABEL;MODULE;EXPR;KIND;FRAGMENT;R;E;OK
 (LABEL MODULE EXPR KIND FRAGMENT)←B
 R←⎕EC EXPR
 →(0=↑R)/CAUGHT
 FAILURES←FAILURES+1
 'FAIL ',LABEL,': no error'
 →0
CAUGHT:E←⍎MODULE,'∆last_error'
 OK←((2⊃R)≡(500+KINDS⍳⊂KIND),0)∧(KIND≡↑E)∧(∨/FRAGMENT⍷2⊃E)∧∨/(MODULE,': ')⍷2⊃E
 →OK/0
 FAILURES←FAILURES+1
 'FAIL ',LABEL,': got event ',(⍕2⊃R),' ',(↑E),' "',(2⊃E),'"'
∇

∇Z←ENV NAME;E
 E←⎕ENV NAME
 E←(E[;1]∊⊂NAME)⌿E
 Z←⊃E[1;2]
∇

∇TEST∆scalars;I;TEXT
 CHECK 'schar' (apl_verify∆echo_schar ¯128) ¯128
 CHECK 'schar' (apl_verify∆echo_schar 127) 127
 CHECK 'uchar' (apl_verify∆echo_uchar 255) 255
 CHECK 'short' (apl_verify∆echo_short ¯32768) ¯32768
 CHECK 'ushort' (apl_verify∆echo_ushort 65535) 65535
 CHECK 'int' (apl_verify∆echo_int ¯2147483648) ¯2147483648
 CHECK 'int' (apl_verify∆echo_int 2147483647) 2147483647
 CHECK 'uint' (apl_verify∆echo_uint 4294967295) 4294967295
 ⍝ The literal ¯9223372036854775808 is a float; integral floats are accepted.
 CHECK 'long' (apl_verify∆echo_long ¯9223372036854775808) ¯9223372036854775808
 CHECK 'll' (apl_verify∆echo_ll 9223372036854775807) 9223372036854775807
 CHECK 'too large' (apl_demo∆too_large) 9007199254740993
 ⍝ Unsigned results above the integer range are floats when exact.
 CHECK 'ull' (apl_verify∆echo_ull 2*63) (2*63)
 CHECK 'ulong' (apl_verify∆echo_ulong 18446744073709549568) 18446744073709549568
 FAILS 'ull range' 'apl_verify' 'apl_verify∆echo_ull 2*64' 'range' 'integer outside C++ type range'
 FAILS 'int range' 'apl_verify' 'apl_verify∆echo_int 2147483648' 'range' 'integer outside C++ type range'
 FAILS 'schar range' 'apl_verify' 'apl_verify∆echo_schar 128' 'range' 'integer outside'
 FAILS 'uint range' 'apl_verify' 'apl_verify∆echo_uint ¯1' 'range' 'integer outside'
 FAILS 'fraction' 'apl_verify' 'apl_verify∆echo_int 1.5' 'argument' 'expected an integer'
 FAILS 'text for int' 'apl_verify' 'apl_verify∆echo_int ''a''' 'argument' 'expected an integer'
 FAILS 'vector for int' 'apl_verify' 'apl_verify∆echo_int 1 2' 'argument' 'expected an integer'
 CHECK 'add' (apl_demo∆add 20 22) 42
 CHECK 'float' (apl_verify∆echo_float 1.25) 1.25
 CHECK 'double' (apl_verify∆echo_double 1.7976931348623157E308) 1.7976931348623157E308
 CHECK 'double from int' (apl_verify∆echo_double 3) 3
 CHECK 'half' (apl_demo∆half 3) 1.5
 FAILS 'nonfinite' 'apl_verify' 'apl_verify∆nonfinite_result' 'range' 'non-finite floating-point result'
 CHECK 'bool' (apl_verify∆echo_bool 1) 1
 CHECK 'negate' (apl_demo∆negate 1) 0
 FAILS 'bool' 'apl_verify' 'apl_verify∆echo_bool 2' 'argument' 'expected a boolean (0 or 1)'
 ⍝ char is a character of code point 0-255.
 CHECK 'char' (apl_verify∆echo_char 'a') 'a'
 CHECK 'char' (apl_verify∆echo_char ⎕UCS 255) (⎕UCS 255)
 FAILS 'char range' 'apl_verify' 'apl_verify∆echo_char ⎕UCS 256' 'range' 'character outside the C++ char range'
 FAILS 'char number' 'apl_verify' 'apl_verify∆echo_char 97' 'argument' 'expected a character'
 ⍝ Strings are character vectors, passed as UTF-8.
 TEXT←'' 'world' '世界😀' ('a',(⎕UCS 0),'b') (40000⍴'long')
 I←0
LOOP:→(I≥⍴TEXT)/DONE
 I←I+1
 CHECK 'string' (apl_verify∆echo_string I⊃TEXT) (I⊃TEXT)
 →LOOP
DONE:CHECK 'one character' (apl_verify∆echo_string 'x') (,'x')
 FAILS 'string number' 'apl_verify' 'apl_verify∆echo_string 1 2' 'argument' 'expected a character vector'
 CHECK 'embedded nul' (apl_verify∆embedded_nul) ('a',(⎕UCS 0),'b')
 CHECK 'greet' (apl_demo∆greet 'APL') 'Hello, APL!'
 CHECK 'defaulted' (apl_verify∆defaulted ⍬) 42
 CHECK 'defaulted' (apl_verify∆defaulted 12) 12
 CHECK 'alias' (apl_verify∆alias_identity 42) 42
 apl_verify∆do_nothing
 I←0
REPEAT:→(I≥20)/REPEATED
 I←I+1
 FAILS 'fail' 'apl_demo' 'apl_demo∆fail' 'native' 'native failure'
 FAILS 'unknown' 'apl_verify' 'apl_verify∆unknown_error' 'native' 'unknown C++ exception'
 FAILS 'allocation' 'apl_verify' 'apl_verify∆allocation_error' 'allocation' 'allocation'
 →REPEAT
REPEATED:FAILS 'message' 'apl_demo' 'apl_demo∆fail' 'native' 'apl_demo: r_demo::fail: native failure (native)'
 CHECK 'last error' (↑apl_demo∆last_error) 'native'
∇

∇TEST∆names;P
 CHECK 'type' (apl_names∆type ⍬) 42
 CHECK 'type' (apl_names∆type 7) 7
 CHECK 'underscore' (apl_names∆_private_name) 7
 CHECK 'labels' (apl_names∆labels ⍬) 12
 CHECK 'labels' (apl_names∆labels 3) 32
 CHECK 'labels' (apl_names∆labels 3 4) 34
 FAILS 'labels' 'apl_names' 'apl_names∆labels 1 2 3' 'argument' 'expected 0 to 2 arguments'
 FAILS 'labels' 'apl_names' 'apl_names∆labels 2 2⍴1' 'argument' 'scalar or vector'
 ⍝ A plain string is one argument; more strings are nested.
 CHECK 'join' (apl_names∆join 'a') 'a-!'
 CHECK 'join' (apl_names∆join 'a+') 'a+-!'
 CHECK 'join' (apl_names∆join (,'a') (,'+')) 'a+!'
 CHECK 'join' (apl_names∆join 'ab' 'cd' 'ef') 'abcdef'
 CHECK 'join' (apl_names∆join ⊂'abc') 'abc-!'
 CHECK 'repeat' (apl_names∆repeat 3 'ab') 'ababab'
 FAILS 'repeat' 'apl_names' 'apl_names∆repeat 3' 'argument' 'expected 2 arguments'
 CHECK 'mixed' (apl_names∆mixed 1) 4.5
 CHECK 'mixed' (apl_names∆mixed 1 2.5) 5.5
 CHECK 'mixed' (apl_names∆mixed 1 2.5 3) 6.5
 CHECK 'shout' (apl_names∆shout 'a') 'A'
 CHECK 'length' (apl_names∆length '世界😀') 10
 CHECK 'seven' (apl_names∆seven 1 2 3 4 5 6 7) 28
 CHECK 'depth' (apl_names∆depth) 1
 P←apl_names∆point∆new 1 2
 CHECK 'sum' (P apl_names∆point∆sum ⍬) 3
 CHECK 'sum' (P apl_names∆point∆sum 2) 6
 CHECK 'origin' (apl_names∆point∆origin) 0
 CHECK 'x' (apl_names∆point∆x P) 1
 P apl_names∆point∆set_x 5
 CHECK 'set x' (P apl_names∆point∆sum ⍬) 7
 CHECK 'tag' (apl_names∆point∆tag P) 'p'
 P apl_names∆point∆set_tag 'z'
 CHECK 'tag' (apl_names∆point∆tag P) 'z'
 CHECK 'weight' (apl_names∆point∆weight P) 1.5
 P apl_names∆point∆set_weight 2
 CHECK 'weight' (apl_names∆point∆weight P) 2
 apl_names∆point∆close P
 FAILS 'closed point' 'apl_names' 'P apl_names∆point∆sum ⍬' 'closed' 'native object is closed'
 apl_names∆point∆close P
 P←apl_names∆point∆new ⍬
 CHECK 'default point' (P apl_names∆point∆sum ⍬) 0
 ⍝ Handles are checked: unknown, forged and wrongly typed ones fail.
 FAILS 'forged' 'apl_names' '999999 apl_names∆point∆sum ⍬' 'closed' 'native object is closed'
 FAILS 'not a handle' 'apl_names' '''p'' apl_names∆point∆sum ⍬' 'argument' 'expected one object'
 FAILS 'two handles' 'apl_names' '(P P) apl_names∆point∆sum ⍬' 'argument' 'expected one object'
 ⍝ Each library has its own handles.
 FAILS 'other library' 'apl_demo' 'apl_demo∆Counter∆value P' 'closed' 'native object is closed'
 apl_names∆point∆close P
∇

∇TEST∆objects;C;S;W;T
 CHECK 'none live' (apl_demo∆Counter∆live) 0
 FAILS 'negative' 'apl_demo' 'apl_demo∆Counter∆new ¯1' 'native' 'negative initial'
 CHECK 'failed constructor' (apl_demo∆Counter∆live) 0
 C←apl_demo∆Counter∆new 20
 CHECK 'live' (apl_demo∆Counter∆live) 1
 CHECK 'add' (C apl_demo∆Counter∆add 22) 42
 CHECK 'value' (apl_demo∆Counter∆value C) 42
 CHECK 'count' (apl_demo∆Counter∆count C) 42
 CHECK 'initial' (apl_demo∆Counter∆initial C) 20
 CHECK 'label' (apl_demo∆Counter∆label C) 'counter'
 CHECK 'answer' (apl_demo∆Counter∆answer) 42
 C apl_demo∆Counter∆set_count 7
 CHECK 'value' (apl_demo∆Counter∆value C) 7
 C apl_demo∆Counter∆set_label '世界😀',⎕UCS 0
 CHECK 'label' (apl_demo∆Counter∆label C) ('世界😀',⎕UCS 0)
 ⍝ Const fields have no setter.
 CHECK 'const field' (⎕NC 'apl_demo∆Counter∆set_initial') 0
 apl_demo∆Counter∆close C
 CHECK 'closed' (apl_demo∆Counter∆live) 0
 FAILS 'value closed' 'apl_demo' 'apl_demo∆Counter∆value C' 'closed' 'native object is closed'
 FAILS 'set closed' 'apl_demo' 'C apl_demo∆Counter∆set_count 1' 'closed' 'native object is closed'
 apl_demo∆Counter∆close C
 S←apl_verify∆Series∆new ⍬
 W←apl_verify∆Worklist∆new 2
 T←apl_verify∆AsyncTask∆new 1 60000 0
 CHECK 'series live' (apl_verify∆Series∆live) 1
 CHECK 'task live' (apl_verify∆AsyncTask∆live) 1
 apl_verify∆Series∆close S
 apl_verify∆Worklist∆close W
 apl_verify∆AsyncTask∆close T
 CHECK 'series closed' (apl_verify∆Series∆live) 0
 CHECK 'workers' (apl_verify∆AsyncTask∆workers) 0
 ⍝ Handles are checked against the object's class.
 S←apl_verify∆Series∆new ⍬
 FAILS 'wrong class' 'apl_verify' 'apl_verify∆Worklist∆size S' 'argument' 'object is not a r_verify::Worklist'
 FAILS 'wrong close' 'apl_verify' 'apl_verify∆Worklist∆close S' 'argument' 'object is not a r_verify::Worklist'
 CHECK 'still open' (apl_verify∆Series∆size S) 2
 apl_verify∆Series∆close S
 C←apl_demo∆Counter∆new¨⍳1000
 CHECK 'many live' (apl_demo∆Counter∆live) 1000
 CHECK 'distinct handles' (⍴∪C) (,1000)
∇

∇TEST∆features;S;Q;ORDER;EXPECTED;GOT;T
 CHECK 'maximum' (apl_verify∆maximum 20 42) 42
 CHECK 'template' (apl_verify∆template_box 42) 42
 CHECK 'variadic' (apl_verify∆variadic_sum 10 20 12) 42
 CHECK 'constant' (apl_verify∆constant_answer) 42
 CHECK 'constrained' (apl_verify∆constrained_sum 20 22) 42
 CHECK 'hierarchy' (apl_verify∆hierarchy_value 42) 42
 CHECK 'crosscast' (apl_verify∆hierarchy_crosscast) 1
 CHECK 'C identity' (apl_verify∆rosetta_r_c_identity 42) 42
 FAILS 'overflow' 'apl_verify' 'apl_verify∆constrained_sum 2147483647 1' 'range' 'overflow'
 FAILS 'series count' 'apl_verify' 'apl_verify∆Series∆new 0 ¯1' 'index' 'series count'
 S←apl_verify∆Series∆new ⍬
 CHECK 'size' (apl_verify∆Series∆size S) 2
 CHECK 'get' (S apl_verify∆Series∆get ⍬) 20
 CHECK 'scaled' (S apl_verify∆Series∆scaled_sum 2) 82
 CHECK 'text' (apl_verify∆Series∆text S) '[20, 21]'
 S apl_verify∆Series∆set 1 22
 CHECK 'scaled' (S apl_verify∆Series∆scaled_sum 1) 42
 CHECK 'get' (S apl_verify∆Series∆get 1) 22
 FAILS 'get range' 'apl_verify' 'S apl_verify∆Series∆get 2' 'index' 'out of range'
 FAILS 'set range' 'apl_verify' 'S apl_verify∆Series∆set ¯1 1' 'index' 'out of range'
 apl_verify∆Series∆close S
 S←apl_verify∆Series∆new 1 3
 CHECK 'text' (apl_verify∆Series∆text S) '[1, 2, 3]'
 apl_verify∆Series∆close S
 ORDER←0
 EXPECTED←'mza' 'azm' 'zma' 'amz'
NEXT:→(ORDER≥4)/ORDERED
 ORDER←ORDER+1
 Q←apl_verify∆Worklist∆new ORDER
 Q apl_verify∆Worklist∆push 'a'
 Q apl_verify∆Worklist∆push 'z'
 Q apl_verify∆Worklist∆push 'm'
 ⍝ APL evaluates right to left: the rightmost pop runs first.
 CHECK 'size' (apl_verify∆Worklist∆size Q) 3
 GOT←(apl_verify∆Worklist∆pop Q),(apl_verify∆Worklist∆pop Q),apl_verify∆Worklist∆pop Q
 CHECK ('order ',⍕ORDER) (⌽GOT) (ORDER⊃EXPECTED)
 FAILS 'empty' 'apl_verify' 'apl_verify∆Worklist∆pop Q' 'index' 'empty'
 apl_verify∆Worklist∆close Q
 →NEXT
ORDERED:FAILS 'worklist order' 'apl_verify' 'apl_verify∆Worklist∆new 0' 'argument' 'unknown worklist order'
 T←apl_verify∆AsyncTask∆new 21 0 0
 CHECK 'wait' (T apl_verify∆AsyncTask∆wait 2000) 1
 CHECK 'result' (apl_verify∆AsyncTask∆result T) 42
 apl_verify∆AsyncTask∆close T
 T←apl_verify∆AsyncTask∆new 1 0 1
 CHECK 'wait' (T apl_verify∆AsyncTask∆wait 2000) 1
 FAILS 'task failure' 'apl_verify' 'apl_verify∆AsyncTask∆result T' 'native' 'async worker failure'
 apl_verify∆AsyncTask∆close T
 T←apl_verify∆AsyncTask∆new 1 60000 0
 CHECK 'ready' (apl_verify∆AsyncTask∆ready T) 0
 CHECK 'wait' (T apl_verify∆AsyncTask∆wait 0) 0
 CHECK 'cancel' (apl_verify∆AsyncTask∆cancel T) 1
 CHECK 'wait' (T apl_verify∆AsyncTask∆wait 2000) 1
 FAILS 'cancelled' 'apl_verify' 'apl_verify∆AsyncTask∆result T' 'native' 'cancelled'
 apl_verify∆AsyncTask∆close T
 FAILS 'delay' 'apl_verify' 'apl_verify∆AsyncTask∆new 1 ¯1 0' 'index' 'delay'
 CHECK 'workers' (apl_verify∆AsyncTask∆workers) 0
∇

⍝ Leaves a 60s worker running; the exit cleanup must stop it quickly.
∇TEST∆exit;T
 T←apl_verify∆AsyncTask∆new 1 60000 0
 CHECK 'task live' (apl_verify∆AsyncTask∆live) 1
∇

∇RUN FAMILY
 ⍎'TEST∆',FAMILY
 →(FAILURES≠0)/0
 'ROSETTA PASS ',FAMILY
∇
RUN ENV 'ROSETTA_APL_FAMILY'
)OFF
