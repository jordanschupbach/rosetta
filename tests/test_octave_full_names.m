% Names Octave reserves or already uses, and Octave protocols (--octave-api full) on
% tests/octave_full_names.hpp. Argument: the directory holding +names and __names__.oct.
1;

function check (ok, what)
  global checks
  checks = checks + 1;
  if ! (islogical (ok) || isnumeric (ok)) || isempty (ok) || ! all (ok(:))
    error ('check failed: %s', what);
  end
end
function fails (f, pattern, identifier)
  global checks
  checks = checks + 1;
  try
    f ();
  catch err
    if isempty (strfind (err.message, pattern))
      error ('expected an error containing "%s", got: %s', pattern, err.message);
    end
    if nargin > 2 && ! strcmp (err.identifier, identifier)
      error ('expected error identifier %s, got %s', identifier, err.identifier);
    end
    return;
  end
  error ('expected an error containing "%s", got none', pattern);
end

args = argv ();
addpath (args{1});
global checks
checks = 0;

% Classes named like Octave classes live in the package.
check (names.Map ().count () == 1 && isa (names.Map (), 'names.Map'), 'class named Map');

% Methods named like keywords and classdef protocol functions get a trailing underscore.
w = names.Words ();
check (w.end_ () == 1 && w.function_ () == 2 && w.classdef_ () == 3 && w.methods_ () == 4, 'keyword methods');
check (w.properties_ () == 5 && w.global_ () == 6, 'more keyword methods');
check (w.disp_ () == 7 && w.size_ () == 8 && w.numel_ () == 9 && w.length_ () == 10 && w.isempty_ () == 11, 'protocol methods');
check (w.isclosed_ () == 12 && w.eq_ (3) == 3 && w.rosetta_h_ () == 13, 'runtime names');
check (w.camelCase () == 14 && w.CamelCase () == 15, 'names differing in case');
check (w.label () == 16 && names.Words.create () == 17, 'plain and static methods');
check (isequal (size (w), [1 1]) && numel (w) == 1 && ! isempty (w), 'the object protocol still works');
check (! isempty (strfind (evalc ('disp (w)'), 'names.Words')), 'disp still works');

% Fields named like keywords and protocol functions.
s = names.Span ();
check (s.end_ == 2 && s.begin == 1 && s.properties_ == 3 && s.size_ == 4 && s.disp_ == 5, 'keyword fields');
s.end_ = 7;
check (s.end_ == 7, 'keyword field set');

% Names differing only in case, and a class and a function differing in case.
check (names.value () == 1 && names.Value () == 2, 'functions differing in case');
check (names.Item ().id == 1 && names.item () == 3, 'class and function differing in case');

% Free functions keep Octave function names inside the package; keywords get an underscore.
check (names.disp (1) == 2 && names.size ([1 2 3]) == 3, 'functions named like Octave functions');
check (names.end_ (1) == 3 && names.global_ (1) == 4, 'functions named like keywords');

% Enumerators named like keywords.
check (names.Color.red == 0 && names.Color.green == 5 && names.Color.end_ == 6, 'enumerators');
check (names.color_value (names.Color.end_) == 6 && names.color_value ('green') == 5, 'enum arguments');
check (names.next_color (names.Color.red) == 5, 'enum result');

% Mutable reference parameters come back as extra outputs.
[lo, hi] = names.bounds ([3 1 2], 0, 0);
check (lo == 1 && hi == 3, 'two updated arguments');
[r, x] = names.increment (4);
check (r == 5 && x == 5, 'result and updated argument');
list = names.append ({'a'}, 'b');
check (iscellstr (list) && isequal (list, {'a', 'b'}), 'updated cell array');
fails (@() names.bounds ([], 0, 0), 'no values', 'names:invalid_argument');

% operator[] (obj(i) 1-based, obj{i} 0-based) and operator().
row = names.Row (3);
check (row(1) == 0 && row(2) == 1.5 && row{2} == 3 && row.size_ () == 3, 'indexing');
fails (@() row(4), 'Row index', 'names:out_of_range');
row.clear ();
check (row.size_ () == 0, 'void method through subsref');
p = names.Polynomial ();
p.coefficients = [1 2 3];
check (p(2) == 17 && isequal (p.coefficients, [1 2 3]), 'callable object');

% Comparison operators and derived partners.
a = names.Version (1, 2);
b = names.Version (1, 3);
check (a == names.Version (1, 2) && a ~= b && a < b && b > a && ! (b < a), 'comparisons');

% Integer widths, complex numbers and containers.
check (isa (names.big (), 'uint64') && names.big () == intmax ('uint64'), 'uint64 beyond int64');
check (isa (names.smallest (), 'int64') && names.smallest () == intmin ('int64'), 'int64 beyond double precision');
check (names.echo_u64 (intmax ('uint64')) == intmax ('uint64'), 'uint64 round trip');
check (names.echo_i8 (-5) == -5, 'int8 argument');
fails (@() names.echo_i8 (300), 'outside C++ type range');
fails (@() names.echo_u64 (-1), 'integer');
check (names.twice (1 + 2i) == 2 + 4i, 'complex');
check (isequal (names.unique ([3 1 3]), [1 3]), 'set result');
t = names.triple ();
check (iscell (t) && t{1} == 1 && strcmp (t{2}, 'two') && t{3} == true, 'tuple result');
check (names.maybe (3) == 3 && isempty (names.maybe ([])), 'optional');
check (strcmp (names.kind_of_value (3), 'int') && strcmp (names.kind_of_value ('x'), 'string'), 'variant');
m = names.numbered ();
check (isa (m, 'containers.Map') && strcmp (m(2), 'two'), 'map with numeric keys');
check (names.total (struct ('a', 1, 'b', 2)) == 3, 'struct as map');
check (names.total (containers.Map ({'a', 'b'}, {1, 2})) == 3, 'containers.Map as map');
check (names.negate (true) == false && islogical (names.negate (false)), 'logical');
check (isequal (names.flags (), [true false true]) && islogical (names.flags ()), 'logical vector');
check (isequal (names.words (), {'a', 'bb'}), 'cellstr result');
check (isequal (names.grid (), [1 2 3; 4 5 6]), 'integer matrix');
r2 = names.ragged ();
check (iscell (r2) && isequal (r2{2}, [2 3]), 'ragged rows as a cell');
check (names.sum_matrix ([1 2; 3 4]) == 10 && names.sum_matrix ({1, [2 3]}) == 6, 'matrix and cell of rows');
seen = [];
names.each ([1 2 3], @(v) evalin ('base', sprintf ('seen(end + 1) = %d;', v)));
check (isequal (seen, [1 2 3]), 'void callback');

% Errors map to identifiers.
fails (@() names.fail (-1), 'negative', 'names:invalid_argument');
fails (@() names.fail (0), 'zero', 'names:native');
fails (@() names.fail (1), 'one', 'names:arithmetic_error');
fails (@() names.fail (2), 'two', 'names:out_of_range');
check (names.fail (3) == 3, 'no error');

% Policy modules are subpackages.
check (names.stats.mean ([1 2 3]) == 2 && names.tools.depth () == 2, 'subpackages');

printf ('Octave full names passed (%d checks)\n', checks);
