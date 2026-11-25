% Prolog API (--language prolog) on the library-shaped example shared with the Python, R6, Lua,
% Common Lisp, Julia, C#, VB, Go, Rust, PHP and Perl backends. Run as
% `swipl test_prolog_full.pl -- features.pl`: the generated module is loaded, and read for the
% documentation checks. The native library is found through FEATURES_PROLOG_LIBRARY.
:- initialization(main, main).

% ---------------------------------------------------------------- checks
check(Goal, What) :-
    nb_getval(checks, N0), N is N0 + 1, nb_setval(checks, N),
    (   catch(Goal, Error, (print_message(error, Error), fail))
    ->  true
    ;   format(user_error, "check failed: ~w~n", [What]),
        halt(1)
    ).

% Goal raises error(native_error(Kind, Message), _) with Text in Message.
fails(Goal, Kind, Text) :-
    nb_getval(checks, N0), N is N0 + 1, nb_setval(checks, N),
    catch((Goal, Raised = none), Error, Raised = Error),
    (   Raised = error(native_error(Kind, Message), _),
        sub_string(Message, _, _, _, Text)
    ->  true
    ;   format(user_error, "expected native_error(~w, ...~w...) from ~q, got ~q~n", [Kind, Text, Goal, Raised]),
        halt(1)
    ).

% Goal raises Expected (unified).
raises(Goal, Expected) :-
    nb_getval(checks, N0), N is N0 + 1, nb_setval(checks, N),
    catch((Goal, Raised = none), Error, Raised = Error),
    (   Raised = Expected
    ->  true
    ;   format(user_error, "expected ~q from ~q, got ~q~n", [Expected, Goal, Raised]),
        halt(1)
    ).

near(A, B) :- abs(A - B) < 1.0e-12.

% The generated source contains Text.
documented(Text) :-
    nb_getval(source, Source),
    sub_string(Source, _, _, _, Text).

% The PlDoc comment of the predicate Name (its first mode line up to the first clause).
doc_of(Name, Doc) :-
    nb_getval(source, Source),
    string_concat("\n%!  ", Name, Start0),
    string_concat(Start0, "(", Start),
    sub_string(Source, Before, _, _, Start), !,
    sub_string(Source, Before, _, 0, Rest),
    string_concat("\n", Name, Head0),
    string_concat(Head0, "(", Head),
    sub_string(Rest, End, _, _, Head), !,
    sub_string(Rest, 0, End, _, Doc).

% ---------------------------------------------------------------- extended objects
% Modules whose override/1 clauses implement C++ virtual methods.
triangle:override(area(Self, Area)) :-
    object_data(Self, tri(Base, Height)),
    Area is Base * Height / 2.
triangle:override(name(_, "triangle")).

failing:override(area(_, _)) :-
    throw(sentinel(from_prolog)).

% Calling the method's predicate inside its own override runs the C++ implementation.
loud:override(name(Self, Name)) :-
    circle_name(Self, Name0),
    string_upper(Name0, Name).

louder:override(name(Self, Name)) :-
    loud:override(name(Self, Name0)),
    string_lower(Name0, Name1),
    string_concat(Name1, "!", Name).

unit_shape:override(area(_, 1.0)).

summer:override(visit(_, Value, Result)) :-
    Result is Value * 10.
summer:override(finished(Self, Values)) :-
    object_set_data(Self, Values).

twice(X, Y) :- Y is X * 2.

main :-
    nb_setval(checks, 0),
    current_prolog_flag(argv, Argv),
    last(Argv, File),
    use_module(File),
    read_file_to_string(File, Source, []),
    nb_setval(source, Source),
    classes,
    overloads,
    containers,
    callbacks,
    values,
    templates,
    closing,
    extended,
    documentation,
    submodule,
    nb_getval(checks, N),
    format("Prolog API passed (~d checks)~n", [N]).

% ---------------------------------------------------------------- classes
classes :-
    circle_new(2, C1),
    check((circle_area(C1, A), A =:= 12, circle_name(C1, "circle"), shape_twice_area(C1, 24.0), circle_radius(C1, 2.0)), 'circle predicates'),
    check((shape_area(C1, A2), A2 =:= 12, shape_name(C1, "circle")), 'base class predicates take derived objects'),
    check((object_class(C1, circle), object_is_a(C1, shape), \+ object_is_a(C1, square)), 'class hierarchy'),
    fails(circle_new(-1, _), invalid_argument, "negative radius"),
    check((catch(circle_new(-1, _), E, true), E = error(Formal, _), phrase(prolog:error_message(Formal), [Format-Args]), format(string(Text), Format, Args), sub_string(Text, _, _, _, "negative radius (invalid_argument)")), 'error message'),
    square_new(S),
    check((square_side(S, 1.0), square_area(S, 1.0)), 'default constructor'),
    square_set_side(S, 3),
    check((square_area(S, 9.0), square_new(2, S2), square_area(S2, 4.0)), 'field setter and overloaded constructor'),
    check((area_of(S, 9.0), describe(C1, "circle:0")), 'functions on classes'),
    % Enumerators are atoms.
    check((circle_kind(C1, circle), square_kind(S, square)), 'enum results'),
    check((filled(solid, true), filled(none, false), filled(1, true)), 'enum arguments'),
    check((kind(custom, 7), kind(K, 1), K == square, fill(none, 0)), 'enum facts'),
    check((legacy(legacy_b, 2), features_legacy_mode(off, 1)), 'plain and qualified enums'),
    fails(filled(dotted, _), binding, ""),
    check((shape_sides_unknown(-1), golden(G), near(G, 1.618)), 'constants'),
    % Most-derived results, ownership transfer and shared ownership.
    make_shape(circle, 1.0, Made),
    check((object_class(Made, circle), circle_radius(Made, 1.0)), 'most-derived result'),
    canvas_new(Canvas),
    make_shape(square, 2.0, Sq), canvas_add(Canvas, Sq),
    shared_square(3.0, Sh), canvas_share(Canvas, Sh),
    circle_new(1, Transferred),
    canvas_add(Canvas, Transferred),
    fails(circle_area(Transferred, _), binding, "closed"),
    check((canvas_size(Canvas, 3), canvas_total(Canvas, T), T =:= 4 + 9 + 3), 'ownership transfer'),
    check((canvas_at(Canvas, 0, First), object_class(First, square), square_area(First, 4.0)), 'borrowed element'),
    shared_square(2.0, Shared),
    canvas_share(Canvas, Shared),
    check((square_area(Shared, 4.0), canvas_total(Canvas, 20.0)), 'shared ownership'),
    check((circle_new(1, One), total_area([One, Shared], 7.0)), 'list of objects').

% ---------------------------------------------------------------- overloads
overloads :-
    square_new(S),
    check((pick(1, "int"), pick(1.5, "double"), pick(2.0, "double"), pick("x", "string")), 'scalar overloads'),
    check((pick([1.0, 2.0], "vector"), pick(S, "shape")), 'container and class overloads'),
    check((pick(x, "string"), pick("7", "string")), 'atoms and strings as strings'),
    check((add(1, 111), add(1, 2, 103), add(1, 2, 3, 6)), 'default arguments'),
    check((defaults(R0), R0 == "x*1-1.500000"), 'all defaults'),
    check((defaults(circle, "y", true, R1), R1 == "y*0!-1.500000"), 'some defaults'),
    fails(pick(["a"], _), binding, "pick"),
    fails(pick(foo(1), _), binding, ""),
    raises(add(_, _), error(instantiation_error, _)),
    fails(add(99999999999999999999999, _), binding, ""),
    check((pick(9223372036854775807, "double"), add(1, 2, -3, 0)), 'beyond int picks double; negative integers').

% ---------------------------------------------------------------- containers
containers :-
    check((count_words(["a", "b", "a"], W), W == ["a"-2, "b"-1]), 'map result'),
    check((count_words([a, b], W2), W2 == ["a"-1, "b"-1]), 'atoms as strings in lists'),
    check((min_max([3.0, 1.0, 2.0], MM), MM == 1.0-3.0), 'pair result'),
    fails(min_max([], _), out_of_range, "empty input"),
    check((find([5, 7], 7, 1), find([5, 7], 9, null)), 'optional result'),
    check((transpose([[1, 2, 3], [4, 5, 6]], Tr), Tr == [[1.0, 4.0], [2.0, 5.0], [3.0, 6.0]]), 'nested lists'),
    check((byte_sum([1, 2, 250], 253), bytes_of("AZ", B), B == [65, 90]), 'bytes'),
    check((circles(3, Cs), length(Cs, 3), last(Cs, L), circle_radius(L, 3.0)), 'list of class results'),
    check((repeat("z", 3, Z), Z == "zzz", repeat(z, 3, Z2), Z2 == "zzz", initial("hello", I), I == "h"), 'chars'),
    check((data_repeat("ét", U), U == "étét", data_repeat("☺", U2), string_length(U2, 2)), 'UTF-8 strings'),
    % A non-const reference parameter is an input and an output argument.
    check((scale([1.0, 2.0], 3.0, V), V == [3.0, 6.0]), 'writeback'),
    fails(repeat("z", -1, _), _, "negative count"),
    fails(min_max(notalist, _), binding, "").

% ---------------------------------------------------------------- callbacks
callbacks :-
    check(apply([X, Y]>>(Y is X * 2), 21.0, 42.0), 'lambda callback'),
    check((apply(twice, 21.0, R), R =:= 42), 'named predicate'),
    raises(apply([_, _]>>throw(sentinel(inside)), 1.0, _), sentinel(inside)),
    fails(apply([_, _]>>fail, 1.0, _), callback, ""),
    raises(apply([_, _]>>true, 1.0, _), error(instantiation_error, _)),
    adder(40, Plus),
    check((call(Plus, 2, P), P == 42), 'native function as a closure'),
    check((apply([A, B]>>(adder(1, F), call(F, A, B)), 3.0, R2), R2 =:= 4), 'native function inside a callback'),
    check((apply(Plus, 2.0, R3), R3 =:= 42), 'native function passed back'),
    nb_setval(count, 0),
    check((apply([A1, A1]>>(nb_getval(count, C0), C1 is C0 + 1, nb_setval(count, C1)), 5.0, 5.0), nb_getval(count, 1)), 'callback side effects').

% ---------------------------------------------------------------- values, operators and borrowed references
values :-
    vec2_new(1, 2, V),
    vec2_new(3, 4, V34),
    vec2_add(V, V34, W),
    check((vec2_x(W, 4.0), vec2_y(W, 6.0)), 'operator predicate'),
    check((vec2_multiply(V, 2, M), vec2_y(M, 4.0), vec2_neg(V, N), vec2_x(N, -1.0), vec2_subtract(V, W, D), vec2_x(D, -3.0)), 'operators'),
    check((vec2_new(1, 2, V12), vec2_equals(V, V12, true), vec2_equals(V, W, false)), 'equality operator'),
    check((vec2_at(V, 1, 2.0), vec2_at(V, 0, 1.0), vec2_new(1, 1, V11), vec2_inner(V, V11, 3.0)), 'operator[] and rename'),
    fails(vec2_at(V, 2, _), out_of_range, "Vec2 index"),
    vec2_set_x(V, 10, Chained0),
    vec2_set_y(Chained0, 20, Chained),
    check((vec2_x(V, 10.0), vec2_y(V, 20.0), Chained == V), 'chaining returns the same object'),
    vec2_add_assign(V, W, Same),
    check((vec2_x(V, 14.0), Same == V), 'compound assignment'),
    vec2_set_x(V, 5),
    check(vec2_x(V, 5.0), 'field setter next to the set_x method'),
    segment_new(Seg),
    vec2_new(3, 4, End),
    segment_set_end(Seg, End),
    check(segment_length2(Seg, 25.0), 'object field set'),
    segment_first(Seg, FirstRef),
    vec2_set_x(FirstRef, 3),
    check((segment_start(Seg, Start), vec2_x(Start, 3.0), segment_length2(Seg, 16.0)), 'borrowed reference writes through'),
    segment_start(Seg, Start2),
    vec2_set_y(Start2, 4),
    check(segment_length2(Seg, 0.0), 'field object borrowed'),
    check((vec2_clone(V12, Clone), vec2_y(Clone, 2.0)), 'C++ clone'),
    check((sum([V12, V34], Sum), Sum =:= 10), 'list of values'),
    check((segment_new(Seg2), segment_first(Seg2, Held), garbage_collect_atoms, vec2_x(Held, 0.0)), 'borrowed reference keeps its owner alive'),
    check(\+ current_predicate(features:target_set_point/2), 'read-only field has no setter').

% ---------------------------------------------------------------- templates, extensions, qualified names, copy types
templates :-
    int_stack_new(St),
    int_stack_push(St, 1),
    int_stack_push(St, 2),
    check((int_stack_size(St, 2), int_stack_items(St, [1, 2]), int_stack_pop(St, 2)), 'template instance'),
    check((clamp_int(42, 10), clamp_int(3, 5, 3), clamp_real(2.5, 2.5)), 'function templates'),
    circle_new(2, C),
    check((shape_label(C, "#circle"), shape_label(C, "@", "@circle"), circle_unit(U), circle_radius(U, 1.0)), 'extensions'),
    check(circle_diameter(C, 4.0), 'extension method'),
    check((features_legacy_version(2), features_legacy_tag_new(T), features_legacy_tag_id(T, 7)), 'qualified names'),
    ledger_new(L),
    ledger_latest(L, Copy),
    stamp_set_value(Copy, 5),
    check((ledger_stamp(L, Stamp), stamp_value(Stamp, 1)), 'copy type'),
    aim_new(Aim),
    aim_origin(Aim, Origin),
    vec2_set_x(Origin, 2),
    check((aim_target(Aim, Target), target_point(Target, Point), vec2_x(Point, 2.0)), 'nested borrowed fields').

% ---------------------------------------------------------------- closing and releasing
closing :-
    circle_new(1, X),
    check((circle_area(X, 3.0), \+ object_closed(X)), 'before close'),
    close_object(X),
    check(object_closed(X), 'closed'),
    fails(circle_area(X, _), binding, "closed"),
    close_object(X),
    forall(between(1, 200, I),
           (   make_shape(square, I, Tmp),
               Area is float(I * I),
               check(square_area(Tmp, Area), 'many objects'),
               (I mod 2 =:= 0 -> close_object(Tmp) ; true)
           )),
    garbage_collect_atoms,
    fails(circle_area(not_an_object, _), binding, "").

% ---------------------------------------------------------------- extended objects
extended :-
    shape_extend(triangle, tri(4, 3), T),
    check((shape_area(T, A), A =:= 6, area_of(T, A2), A2 =:= 6, shape_twice_area(T, 12.0), describe(T, "triangle:7")),
          'override called from C++ and from Prolog'),
    check((object_data(T, tri(4, 3)), object_class(T, shape)), 'extended object data'),
    check((circle_new(1, C1), square_new(2, S2), total_area([T, C1, S2], 13.0), shape_label(T, "@", "@triangle")), 'mixed overrides'),
    shape_extend(incomplete, none, Incomplete),
    fails(area_of(Incomplete, _), _, "pure virtual"),
    shape_extend(failing, none, Failing),
    raises(area_of(Failing, _), sentinel(from_prolog)),
    fails(shape_new(_), binding, "abstract"),
    circle_extend(loud, none, 2, Loud),
    check((describe(Loud, "CIRCLE:0"), circle_name(Loud, "CIRCLE"), circle_area(Loud, 12.0)), 'the C++ method inside its override'),
    check((circle_extend(louder, none, 1, Louder), describe(Louder, "circle!:0")), 'override of an override'),
    canvas_new(Canvas),
    shape_extend(unit_shape, kept, U),
    canvas_add(Canvas, U),
    check(shape_area(U, 1.0), 'extended object owned by C++'),
    square_new(2, Sq),
    canvas_share(Canvas, Sq),
    garbage_collect_atoms,
    check((canvas_total(Canvas, 5.0), canvas_size(Canvas, 2)), 'C++ keeps the extended object alive'),
    check((canvas_at(Canvas, 0, Back), Back == U, object_data(Back, kept)), 'the same object comes back'),
    canvas_clear(Canvas),
    shape_extend(unit_shape, none, U2),
    canvas_share(Canvas, U2),
    check(canvas_total(Canvas, 1.0), 'shared extended object'),
    canvas_clear(Canvas),
    visitor_extend(summer, none, Summer),
    check((run_visitor(Summer, [1.0, 2.0], R), R =:= 30, object_data(Summer, [1.0, 2.0])), 'callback interface'),
    check((visitor_visit(Summer, 4.0, R2), R2 =:= 40), 'a direct call runs the override'),
    check((visitor_new(V), run_visitor(V, [1.0, 2.0], R3), R3 =:= 3), 'native default implementation'),
    fails(object_data(Sq, _), binding, "").

% ---------------------------------------------------------------- PlDoc
documentation :-
    check(documented("/** <module> "), 'module comment'),
    doc_of("add", Add),
    check(sub_string(Add, _, _, _, "%!  add(+A:integer, -Result:integer) is det.\n%!  add(+A:integer, +B:integer, -Result:integer) is det."), 'mode lines'),
    check(sub_string(Add, _, _, _, "Adds three integers, Socha & Dorigo style."), 'summary'),
    check(sub_string(Add, _, _, _, "@arg A first term"), 'parameters'),
    check(sub_string(Add, _, _, _, "@arg Result the sum"), 'result'),
    check(sub_string(Add, _, _, _, "%   ==\n%   int total = add(1);  // 111\n%   =="), 'code block'),
    check(sub_string(Add, _, _, _, "@see pick/2, sum/2"), 'see also'),
    check(sub_string(Add, _, _, _, "@throws error(native_error(out_of_range, Message), _)"), 'throws'),
    check(sub_string(Add, _, _, _, "`int` arithmetic and *no* overflow checks"), 'inline markup'),
    check(sub_string(Add, _, _, _, "B = 10, C = 100"), 'C++ defaults documented'),
    check((doc_of("defaults", Defaults), sub_string(Defaults, _, _, _, "Weight = 0.5")), 'float default'),
    check((doc_of("pick", Pick), sub_string(Pick, _, _, _, "Overloads")), 'overloads listed'),
    check((doc_of("vec2_x", X), sub_string(X, _, _, _, "Horizontal component. (float)")), 'field documentation'),
    check((doc_of("circle_extend", Extend), sub_string(Extend, _, _, _, "override/1")), 'extended objects documented'),
    check((doc_of("apply", Apply), sub_string(Apply, _, _, _, ":F:callable")), 'closure argument'),
    check(documented(":- meta_predicate apply(2, +, -)."), 'meta predicate'),
    check((doc_of("scale", Scale), sub_string(Scale, _, _, _, "scale(+Values0:list(float), +Factor:float, -Values:list(float))")), 'writeback mode').

% ---------------------------------------------------------------- policy modules
submodule :-
    check((data_unit(U), object_class(U, vec2), vec2_x(U, 1.0), vec2_y(U, 0.0)), 'submodule result'),
    check((data_scaled(3, 6), data_scaled(2.5, S), S =:= 6.25), 'submodule overloads'),
    check((data_repeat("ab", "abab"), repeat("x", 3, "xxx")), 'submodule names are separate'),
    check(documented("predicates prefixed data_"), 'submodule documented').
