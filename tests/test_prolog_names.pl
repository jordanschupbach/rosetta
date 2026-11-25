% Names and values that are hard for Prolog (tests/prolog_full_names.hpp). Run as
% `swipl test_prolog_names.pl -- names.pl`; the library is found through NAMES_PROLOG_LIBRARY.
:- initialization(main, main).

check(Goal, What) :-
    nb_getval(checks, N0), N is N0 + 1, nb_setval(checks, N),
    (   catch(Goal, Error, (print_message(error, Error), fail))
    ->  true
    ;   format(user_error, "check failed: ~w~n", [What]),
        halt(1)
    ).
fails(Goal, Kind) :-
    nb_getval(checks, N0), N is N0 + 1, nb_setval(checks, N),
    catch((Goal, Raised = none), Error, Raised = Error),
    (   Raised = error(native_error(Kind, _), _)
    ->  true
    ;   format(user_error, "expected native_error(~w, _) from ~q, got ~q~n", [Kind, Goal, Raised]),
        halt(1)
    ).

% The renumbered virtual method pipe_line_count_2 is overridden as count.
counter:override(count(Self, Count)) :-
    pipe_line_count_2(Self, Native),
    Count is Native + 40.
labeled:override(label(_, "prolog thing")).
described:override(describe(Self, Text)) :-
    leaf_describe(Self, Native),
    string_concat("prolog ", Native, Text).

main :-
    nb_setval(checks, 0),
    current_prolog_flag(argv, Argv),
    last(Argv, File),
    use_module(File),
    % System predicates as function names take a number; the system ones still work.
    check((length_2("abc", 30), length([a, b], 2)), 'length'),
    check((print_2(5, 5), sort_2([1, 2, 3], [3, 2, 1]), msort([b, a], [a, b])), 'print and sort'),
    check((succ_2(1, 3), succ(1, 2), call_2(2, 6), count([1, 2], 2)), 'succ, call and a free name'),
    % Class prefixes.
    check((point_new(P), point_x(P, 1), point_2_new(P2), point_2_y(P2, 2), object_class(P2, point_2)), 'classes alike but for case'),
    check((object_new(O), object_data_2(O, 9), object_id(O, 4)), 'a class named like the runtime'),
    fails(object_data(O, _), binding),
    check((pipe_new(Pi), pipe_line_count(Pi, 1), pipe_line_new(L), pipe_line_count_2(L, 2), pipe_line_total(L, 200)), 'concatenated prefixes'),
    pipe_line_extend(counter, none, Counter),
    check((pipe_line_total(Counter, 4200), pipe_line_count_2(Counter, 42)), 'a renumbered virtual method overridden'),
    % Enumerators.
    check((mode(on, 0), mode(on_2, 1), mode(true_2, 2), mode(null_2, 3), mode_value(off, 4), mode_value(true_2, 2)), 'enumerator atoms'),
    fails(mode_value(true, _), binding),
    % Members.
    cursor_new(C),
    check((cursor_count(C, 20), cursor_count_2(C, 2)), 'method before field'),
    cursor_set_count_2(C, 3),
    check(cursor_count(C, 30), 'field setter'),
    check((cursor_get_name(C, "camel"), cursor_get_name(C, 2, "snakesnake")), 'getName and get_name by arity'),
    check((cursor_move(C, 3, 3), cursor_move(C, 4, 7), cursor_position(C, 7), cursor_create(5)), 'methods and static methods'),
    % Multiple inheritance and hiding.
    thing_new(T),
    check((label_of(T, "thing"), counted_of(T, 3), counted_counted(T, 3), named_tag(T, "t"), thing_label(T, "thing")), 'multiple inheritance'),
    check((object_is_a(T, named), object_is_a(T, counted), as_named(T, N), named_label(N, "thing")), 'both bases'),
    thing_extend(labeled, none, LT),
    check((label_of(LT, "prolog thing"), named_label(LT, "prolog thing"), counted_of(LT, 3)), 'override through the second base'),
    leaf_new(Leaf),
    check((leaf_scale(Leaf, 2.0, 3.0, 6.0), base_scale(Leaf, 2.0, 2.0), leaf_size(Leaf, "big"), base_size(Leaf, 1)), 'hiding'),
    check((describe_base(Leaf, "leaf"), base_describe(Leaf, "leaf")), 'virtual through the base'),
    leaf_extend(described, none, DL),
    check((describe_base(DL, "prolog leaf"), base_describe(DL, "prolog leaf")), 'override of a derived virtual method'),
    % Operators.
    money_new(250, M),
    check((money_new(5, M5), money_add(M, M5, Sum), money_cents(Sum, 255), money_multiply(M, 2, D), money_cents(D, 500)), 'operators'),
    check((money_less(M5, M, true), money_less(M, M5, false), money_call(M, 1, 251)), 'comparison and call'),
    % Values.
    check((biggest(B), B =:= 18446744073709551615, echo_u64(18446744073709551615, B)), 'unsigned 64-bit'),
    check((echo_i64(-9223372036854775808, I), I =:= -9223372036854775808), 'signed 64-bit'),
    fails(echo_i64(9223372036854775808, _), binding),
    fails(echo_u64(-1, _), binding),
    check((negate(true, false), negate(false, true)), 'booleans'),
    check((kind_of(true, "bool"), kind_of(1, "int"), kind_of("x", "string")), 'boolean, integer and string overloads'),
    check((bump(1, "a", V, Text), V == 2, Text == "a!"), 'writebacks'),
    check(nothing, 'no arguments and no result'),
    check((call_twice([X, Y]>>(Y is X * 3), 2, 18), call_twice(succ, 1, 3)), 'callbacks'),
    nb_setval(seen, []),
    check((each([1, 2, 3], [X1]>>(nb_getval(seen, S0), nb_setval(seen, [X1|S0]))), nb_getval(seen, [3, 2, 1])), 'void callbacks'),
    fails(call_twice([_, _]>>fail, 1, _), callback),
    check((catch(kind_of(_, _), error(instantiation_error, _), true)), 'instantiation error'),
    check(data_twice(4, 8), 'submodule'),
    nb_getval(checks, Count),
    format("Prolog names passed (~d checks)~n", [Count]).
