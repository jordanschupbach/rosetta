% Checks the PlDoc of a generated Prolog module: run as `swipl check_prolog_pldoc.pl -- MODULE.pl`
% (the native library found through MODULE_PROLOG_LIBRARY). PlDoc collects the comments while the
% module loads: the module comment and a structured comment for every exported predicate (PlDoc
% files it under the predicates its mode lines declare), with no PlDoc warnings.
:- initialization(main, main).
:- use_module(library(pldoc)).
:- use_module(library(pldoc/doc_process)).

:- dynamic warned/1.
user:message_hook(Message, warning, _) :-
    \+ warned(_),
    assertz(warned(Message)),
    fail.

main :-
    doc_collect(true),
    current_prolog_flag(argv, Argv),
    last(Argv, File),
    use_module(File, []),
    absolute_file_name(File, Path),
    module_property(Module, file(Path)),
    (   doc_comment(Module:module(_), _, _, _)
    ->  true
    ;   format(user_error, "no PlDoc module comment~n", []), halt(1)
    ),
    module_property(Module, exports(Exports)),
    forall(member(Name/Arity, Exports),
           (   doc_comment(Module:Name/Arity, _, _, Comment),
               sub_string(Comment, 0, _, _, "%!")
           ->  true
           ;   format(user_error, "no PlDoc comment with a mode line for ~w/~w~n", [Name, Arity]), halt(1)
           )),
    (   warned(Message)
    ->  format(user_error, "PlDoc warning: ~q~n", [Message]), halt(1)
    ;   true
    ),
    length(Exports, Count),
    format("PlDoc of ~w passed (~d predicates)~n", [Module, Count]).
