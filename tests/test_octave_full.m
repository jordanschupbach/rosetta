% Full Octave API (--octave-api full) on the library-shaped example shared with the Python, R6,
% Lua, Common Lisp, Julia, C# and Ruby backends (a port of tests/TestCSharpFull.cs).
% Arguments: MODE (features or help), the directory holding +features and __features__.oct,
% and the directory of the Octave subclasses used by the director checks (tests/octave_full).
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
  message = '';
  id = '';
  try
    f ();
    raised = false;
  catch err
    raised = true;
    message = err.message;
    id = err.identifier;
  end
  if ! raised
    error ('expected an error containing "%s", got none', pattern);
  end
  if isempty (strfind (message, pattern))
    error ('expected an error containing "%s", got: %s', pattern, message);
  end
  if nargin > 2 && ! strcmp (id, identifier)
    error ('expected error identifier %s, got %s (%s)', identifier, id, message);
  end
end
function set_field (object, name, value)
  object.(name) = value;
end
function r = has (text, part)
  r = ! isempty (strfind (text, part));
end

args = argv ();
mode = args{1};
addpath (args{2});
addpath (args{3});
global checks
checks = 0;

if strcmp (mode, 'help')
  % Help text: Doxygen comments as plain-text Octave help in the generated .m files.
  [text, format] = get_help_text ('features.add');
  check (strcmp (format, 'plain text'), 'plain text help');
  check (has (text, 'add  Adds three integers, Socha & Dorigo style.'), 'H1 line');
  check (has (text, 'r = features.add (a, b, c)'), 'call forms');
  check (has (text, 'b  integer, default 10: second term'), 'parameter with default');
  check (has (text, 'r  integer: the sum'), 'output');
  check (has (text, 'features:out_of_range  never, in practice'), 'errors');
  check (has (text, 'Note: Defaults keep the result small.'), 'notes');
  check (has (text, 'int total = add(1);  // 111'), 'code example');
  check (has (text, 'C++: int features::add(int a, int b = 10, int c = 100)'), 'C++ signature');
  check (has (text, 'See also: features.pick, features.sum'), 'see also');
  text = get_help_text ('features.defaults');
  check (has (text, 'label  char row, default ''x'''), 'string default');
  check (has (text, 'kind  features.Kind value, default features.Kind.square'), 'enum default');
  text = get_help_text ('features.Circle');
  check (has (text, 'Circle  A circle with area 3r'), 'class help');
  check (has (text, 'obj = features.Circle (radius)'), 'constructor form');
  check (has (text, 'Inherits from features.Shape.'), 'base class');
  check (has (text, 'override its virtual'), 'director note');
  text = get_help_text ('features.Circle.Circle');
  check (has (text, 'features:invalid_argument  when `radius` < 0'), 'constructor errors');
  check (has (text, 'radius  double: distance from the centre'), 'constructor parameter');
  text = get_help_text ('features.Shape.area');
  check (has (text, 'area  Area of the shape.'), 'method help');
  check (has (text, 'r = obj.area ()'), 'method form');
  text = get_help_text ('features.Vec2');
  check (has (text, 'x  double: Horizontal component.'), 'property documentation');
  check (has (text, 'Operators: +'), 'operators listed');
  text = get_help_text ('features.Kind');
  check (has (text, 'features.Kind.custom = 7'), 'enum constants');
  text = get_help_text ('features.data.unit');
  check (has (text, 'Mapped by the policy''s modules'), 'subpackage help');
  text = get_help_text ('features.scale');
  check (has (text, 'values = features.scale (values, factor)'), 'updated argument as output');
  text = get_help_text ('features.Contents');
  check (has (text, 'features.Circle  A circle'), 'package contents');
  out = evalc ('help features.Circle');
  check (has (out, 'A circle with area'), 'help command output');
  out = evalc ('help features.Shape.twice_area');
  check (has (out, 'twice_area'), 'help of an inherited method');
  out = evalc ('help features.pick');
  check (has (out, 'Overloads are chosen at run time'), 'overload note');
  printf ('Octave full API help passed (%d checks)\n', checks);
  exit (0);
end

% Classes, inheritance and virtual dispatch.
c1 = features.Circle (2);
check (c1.area () == 12 && strcmp (c1.name (), 'circle') && c1.twice_area () == 24 && c1.radius () == 2, 'circle methods');
check (has (evalc ('disp (c1)'), 'features.Circle'), 'disp names the class');
check (isa (c1, 'features.Shape') && isa (c1, 'features.NativeObject') && isa (c1, 'handle'), 'class hierarchy');
fails (@() features.Circle (-1), 'negative radius', 'features:invalid_argument');
s = features.Square ();
check (s.side == 1 && s.area () == 1, 'default constructor');
s.side = 3;
check (s.area () == 9 && features.Square (2).area () == 4, 'field set');
check (features.area_of (s) == 9 && strcmp (features.describe (c1), 'circle:0'), 'functions on classes');
check (has (evalc ('disp (s)'), 'side: 3'), 'disp shows fields');
% Enums.
check (c1.kind () == features.Kind.circle && s.kind () == features.Kind.square, 'enum results');
check (features.filled (features.Fill.solid) && ! features.filled (features.Fill.None), 'enum arguments');
check (features.filled ('solid') && features.Kind.custom == 7, 'enum names and values');
check (any (strcmp (properties ('features.Kind'), 'custom')), 'enum members');
check (features.Legacy.legacy_b == 2, 'unscoped enum');
check (features.Shape.sides_unknown == -1 && abs (features.golden () - 1.618) < 1e-12 && c1.sides_unknown == -1, 'constants');

% Most-derived results, ownership transfer and shared ownership.
made = features.make_shape (features.Kind.circle, 1);
check (isa (made, 'features.Circle') && made.radius () == 1, 'most-derived result');
canvas = features.Canvas ();
canvas.add (features.make_shape (features.Kind.square, 2));
canvas.share (features.shared_square (3));
transferred = features.Circle (1);
canvas.add (transferred);
fails (@() transferred.area (), 'closed', 'features:binding');
check (canvas.size_ () == 3 && canvas.total () == 4 + 9 + 3, 'ownership transfer');
first = canvas.at (0);
check (isa (first, 'features.Square') && first.area () == 4, 'borrowed element');
shared = features.shared_square (2);
canvas.share (shared);
check (shared.area () == 4 && canvas.total () == 20, 'shared ownership');
check (features.total_area ({features.Circle(1), shared}) == 7, 'vector of objects');
clear shared first

% Overload resolution by Octave type, checked again in C++.
check (strcmp (features.pick (1.5), 'double') && strcmp (features.pick (2), 'double'), 'doubles pick double');
check (strcmp (features.pick (int32 (1)), 'int') && strcmp (features.pick (int8 (1)), 'int'), 'integer types pick int');
check (strcmp (features.pick ('x'), 'string'), 'char picks string');
check (strcmp (features.pick ([1 2]), 'vector') && strcmp (features.pick (s), 'shape'), 'container and class overloads');
check (features.add (1) == 111 && features.add (1, 2) == 103 && features.add (1, 2, 3) == 6, 'default arguments');
check (features.add (int32 (1)) == 111, 'integer arguments');
fails (@() features.add (1.5), 'argument ''a''', 'features:binding');
fails (@() features.add (1, 2, 3, 4), 'incompatible arguments');

% Containers.
counts = features.count_words ({'a', 'b', 'a'});
check (isa (counts, 'containers.Map') && counts ('a') == 2 && counts ('b') == 1, 'map result');
mm = features.min_max ([3 1 2]);
check (iscell (mm) && mm{1} == 1 && mm{2} == 3, 'pair result');
check (isequal (features.min_max ([3; 1; 2]), {1, 3}), 'column vectors are vectors');
fails (@() features.min_max ([]), 'empty input', 'features:out_of_range');
check (features.find ([5 7], 7) == 1 && isempty (features.find ([5 7], 9)), 'optional result');
check (features.find (int32 ([5 7]), 7) == 1, 'integer vector argument');
tr = features.transpose ([1 2 3; 4 5 6]);
check (isequal (tr, [1 4; 2 5; 3 6]), 'matrix in and out');
check (isequal (features.transpose ({[1 2 3], [4 5 6]}), [1 4; 2 5; 3 6]), 'cell of rows');
check (features.byte_sum (uint8 ([1 2 250])) == 253 && features.byte_sum ([1 2 250]) == 253, 'bytes');
check (isequal (features.bytes_of ('AZ'), [65 90]), 'bytes result');
cs = features.circles (3);
check (iscell (cs) && numel (cs) == 3 && cs{3}.radius () == 3, 'vector of class results');
check (strcmp (features.repeat ('z', 3), 'zzz') && features.initial ('hello') == 'h', 'chars');
check (ischar (features.initial ('hello')), 'char result');
% Mutable reference parameters: the updated value is an output.
values = features.scale ([1 2], 3);
check (isequal (values, [3 6]), 'updated argument returned');
fails (@() features.repeat ('z', -1), 'negative count', 'features:invalid_argument');
check (strcmp (features.defaults (), 'x*1-1.500000'), 'all defaults');
check (strcmp (features.defaults (features.Kind.circle, 'y', true), 'y*0!-1.500000'), 'some defaults');
check (isempty (features.transpose ([])), 'empty matrix');

% Callbacks both ways.
check (features.apply (@(x) x * 2, 21) == 42, 'callback');
fails (@() features.apply (@(x) error ('my:id', 'inside callback'), 1), 'inside callback', 'my:id');
fails (@() features.apply (@(x) undefined_function_xyz (x), 1), 'undefined_function_xyz');
plus40 = features.adder (40);
check (isa (plus40, 'function_handle') && plus40 (2) == 42, 'native function as function handle');
check (features.apply (@(x) feval (features.adder (1), x), 3) == 4, 'native function inside a callback');
check (features.apply (plus40, 2) == 42, 'native function passed back');
check (isequal (cellfun (plus40, {1, 2}), [41 42]), 'native function in cellfun');

% Value type operators, chaining and borrowed references.
v = features.Vec2 (1, 2);
w = v + features.Vec2 (3, 4);
check (w.x == 4 && w.y == 6 && (v * 2).y == 4 && (-v).x == -1 && (v - w).x == -3, 'operators');
check (v == features.Vec2 (1, 2) && ! (v == w) && v ~= w, 'equality operators');
check (v(1) == 1 && v(2) == 2 && v{1} == 2 && v.inner (features.Vec2 (1, 1)) == 3, 'indexing');
fails (@() v(3), 'Vec2 index', 'features:out_of_range');
check ((v .* 2).x == 2, 'times alias');
fails (@() 2 * v, 'not defined');
chained = v.set_x (10).set_y (20);
check (v.x == 10 && v.y == 20 && chained.x == 10 && chained == v, 'chaining returns the same object');
v.x = 5;
check (v.x == 5 && chained.x == 5, 'property write');
seg = features.Segment ();
seg.end_ = features.Vec2 (3, 4);
check (seg.length2 () == 25, 'field object set (end is renamed end_)');
start = seg.first ();
start.x = 3;
check (seg.start.x == 3 && seg.length2 () == 16, 'borrowed reference writes through');
seg.start.y = 4;
check (seg.length2 () == 0, 'nested property write');
check (features.Vec2 (1, 2).clone ().y == 2, 'clone');
check (features.sum ({features.Vec2(1, 2), features.Vec2(3, 4)}) == 10, 'vector of values');
held = features.Segment ().first ();
check (held.x == 0, 'borrowed reference keeps its owner alive');
clear held start seg

% Templates, extensions, qualified names and copy types.
stack = features.IntStack ();
stack.push (1);
stack.push (2);
check (stack.size_ () == 2 && isequal (stack.items (), [1 2]) && stack.pop () == 2, 'template instance');
fails (@() features.IntStack ().pop (), 'empty stack', 'features:out_of_range');
check (features.clamp_int (42) == 10 && features.clamp_int (3, 5) == 3 && features.clamp_real (2.5) == 2.5, 'function templates');
check (strcmp (c1.label (), '#circle') && strcmp (c1.label ('@'), '@circle') && features.Circle.unit ().radius () == 1, 'extensions');
check (features.features__legacy__version () == 2 && features.features__legacy__Tag ().id == 7, 'qualified names');
check (features.features__legacy__Mode.off == 1, 'qualified enum');
check (features.Circle (2).diameter () == 4, 'extension method');
ledger = features.Ledger ();
copy = ledger.latest ();
copy.value = 5;
check (ledger.stamp.value == 1, 'copy type');
aim = features.Aim ();
aim.origin.x = 2;
check (aim.target.point.x == 2, 'nested borrowed fields');
fails (@() set_field (aim.target, 'point', features.Vec2 ()), 'point');
check (aim.target.scale == 1, 'reference member class fields');

% Errors, delete and lifetime.
try
  features.Circle (-1);
catch err
  check (strcmp (err.identifier, 'features:invalid_argument'), 'error identifier');
end
fails (@() features.area_of ([]), 'argument ''shape''', 'features:binding');
fails (@() features.area_of ('x'), 'argument ''shape''', 'features:binding');
x = features.Circle (1);
check (x.area () == 3 && ! x.isclosed (), 'before delete');
delete (x);
check (x.isclosed (), 'closed by delete');
fails (@() x.area (), 'closed', 'features:binding');
delete (x);
check (has (evalc ('disp (x)'), 'closed'), 'disp of a closed object');
for i = 1:200
  tmp = features.make_shape (features.Kind.square, i);
  check (tmp.area () == i * i, 'many objects');
end
clear tmp x

% Directors: Octave subclasses implement C++ virtual methods.
t = Triangle (4, 3);
check (t.area () == 6 && features.area_of (t) == 6 && t.twice_area () == 12 && strcmp (features.describe (t), 'triangle:7'), 'Octave override called from C++');
check (features.total_area ({t, features.Circle(1), features.Square(2)}) == 13 && strcmp (t.label ('@'), '@triangle'), 'mixed overrides');
check (isa (t, 'features.Shape'), 'subclass is a Shape');
fails (@() features.area_of (Incomplete ()), 'pure virtual method');
fails (@() features.area_of (Failing ()), 'from Octave', 'Failing:area');
check (strcmp (features.describe (Loud (1)), 'CIRCLE:0') && Loud (2).area () == 12, 'base call runs the C++ method');
check (strcmp (features.describe (Louder (1)), 'circle!:0'), 'deeper subclasses');
fails (@() features.Shape (), 'abstract', 'features:binding');
canvas = features.Canvas ();
u = UnitShape ();
u.tag = 'kept';
canvas.add (u);
check (u.area () == 1 && ! u.isclosed (), 'subclass object owned by C++ stays usable');
clear u
canvas.share (features.Square (2));
check (canvas.total () == 5 && canvas.size_ () == 2, 'C++ keeps the Octave object alive');
back = canvas.at (0);
check (isa (back, 'UnitShape') && strcmp (back.tag, 'kept') && back == canvas.at (0), 'the same Octave object comes back');
clear back
canvas.clear ();
canvas.share (UnitShape ());
check (canvas.total () == 1, 'shared subclass object');
canvas.clear ();
summer = Summer ();
check (features.run_visitor (summer, [1 2]) == 30 && isequal (summer.seen, [1 2]), 'callback interface');
check (features.run_visitor (features.Visitor (), [1 2]) == 3, 'native default implementation');

% Policy modules: features::data functions live in the subpackage features.data.
u = features.data.unit ();
check (u.x == 1 && u.y == 0, 'subpackage result');
check (features.data.scaled (3) == 7.5 && features.data.scaled (int32 (3)) == 6, 'subpackage overloads');
check (strcmp (features.data.repeat ('ab'), 'abab') && strcmp (features.repeat ('x', 3), 'xxx'), 'subpackage names are separate');
clear u

printf ('Octave full API passed (%d checks)\n', checks);
