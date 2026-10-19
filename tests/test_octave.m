1; # Script with local helpers; no package dependencies.
function fails(action, fragment, identifier = '')
  caught = false;
  try
    action();
  catch err
    caught = true;
    assert(! isempty(strfind(err.message, fragment)), ['unexpected error: ' err.message]);
    if (! isempty(identifier))
      assert(strcmp(err.identifier, identifier), ['unexpected identifier: ' err.identifier]);
    endif
  end_try_catch
  assert(caught, ['expected failure: ' fragment]);
endfunction
function request_output(action)
  value = action();
endfunction
function request_two(action)
  [a, b] = action();
endfunction
function set_property(owner, name, value)
  owner.(name) = value;
endfunction
function save_owner(owner, path, format = '-binary')
  save(format, path, 'owner');
endfunction
function temporary_owners(Counter, Series, Worklist, Task)
  c = Counter.new(1);
  aliases = {c, {c}, struct('owner', c)};
  s = Series.new();
  q = Worklist.new(2);
  task = Task.new(1, 60000, false);
endfunction
args = argv();
family = args{1};
addpath(args{2});
demo = octave_demo();
verify = octave_verify();
Counter = demo.r_demo__Counter;
Series = verify.r_verify__Series;
Worklist = verify.r_verify__Worklist;
Task = verify.r_verify__AsyncTask;
v = @(name, varargin) verify.(['r_verify__' name])(varargin{:});
switch family
  case 'scalars'
    names = {'char', 'schar', 'uchar', 'short', 'ushort', 'int', 'uint', 'long', 'ulong', 'll', 'ull'};
    for i = 1:numel(names)
      name = ['echo_' names{i}];
      assert(v(name, 42) == 42);
      assert(v(name, int64(42)) == 42);
      assert(v(name, uint64(42)) == 42);
      bads = {true, false, '42', [], [1, 2], {42}, struct('n', 42), 1 + 2i, sparse(1), NaN, Inf, 1.5};
      for j = 1:numel(bads)
        fails(@() v(name, bads{j}), 'expected', 'rosetta:conversion');
      endfor
      fails(@() v(name, 2^54), 'exact integer');
      fails(@() v(name, single(2^25)), 'exact integer');
    endfor
    signed = {'schar', 'short', 'int', 'll'};
    bits = [8, 16, 32, 64];
    for i = 1:numel(signed)
      b = bits(i);
      if (b == 64)
        lo = intmin('int64'); hi = intmax('int64');
      else
        lo = int64(-2^(b-1)); hi = int64(2^(b-1)-1);
        fails(@() v(['echo_' signed{i}], lo - int64(1)), 'range');
        fails(@() v(['echo_' signed{i}], hi + int64(1)), 'range');
      endif
      assert(isequal(v(['echo_' signed{i}], lo), lo));
      assert(isequal(v(['echo_' signed{i}], hi), hi));
      fails(@() v(['echo_' signed{i}], intmax('uint64')), 'range');
    endfor
    unsigned = {'uchar', 'ushort', 'uint', 'ull'};
    for i = 1:numel(unsigned)
      b = bits(i);
      hi = intmax(['uint' num2str(b)]);
      assert(isequal(v(['echo_' unsigned{i}], hi), uint64(hi)));
      fails(@() v(['echo_' unsigned{i}], int64(-1)), 'range');
      if (b < 64)
        fails(@() v(['echo_' unsigned{i}], uint64(hi) + uint64(1)), 'range');
      endif
    endfor
    exact = bitshift(uint64(1), 53) + uint64(1);
    assert(isequal(demo.r_demo__too_large(), exact));
    assert(isequal(v('echo_ull', exact), exact));
    assert(isequal(v('echo_ll', intmin('int64')), intmin('int64')));
    assert(isa(v('echo_int', 42), 'int64'));
    assert(isa(v('echo_uint', 42), 'uint64'));
    assert(isa(v('echo_float', 1.25), 'single'));
    assert(v('echo_float', 1.25) == 1.25);
    assert(v('echo_double', realmax()) == realmax());
    fails(@() v('echo_float', 1e39), 'finite');
    fails(@() v('nonfinite_result'), 'non-finite');
    for bad = {NaN, Inf, -Inf, true, [1, 2], 1 + 1i, '1'}
      fails(@() v('echo_double', bad{1}), 'expected');
    endfor
    assert(isequal(v('echo_bool', true), true));
    assert(isequal(v('echo_bool', false), false));
    for bad = {0, 1, 'true', [], [true, false]}
      fails(@() v('echo_bool', bad{1}), 'logical scalar');
    endfor
    for text = {'', 'world', '世界😀', char([97, 0, 98]), char([255, 254]), repmat('long', 1, 10000)}
      assert(isequal(v('echo_string', text{1}), text{1}));
    endfor
    assert(isequal(v('embedded_nul'), char([97, 0, 98])));
    fails(@() v('echo_string', ['ab'; 'cd']), 'row vector');
    fails(@() v('echo_string', ['a'; 'b']), 'row vector');
    fails(@() v('echo_string', uint8([97, 98])), 'row vector');
    assert(v('defaulted') == 42);
    assert(v('defaulted', 12) == 12);
    assert(v('alias_identity', 42) == 42);
    v('do_nothing');
    fails(@() request_output(@() v('do_nothing')), 'no output');
    fails(@() request_two(@() demo.r_demo__add(1, 2)), 'at most one');
    fails(@() v('echo_int'), 'number');
    fails(@() v('echo_int', 1, 2), 'number');
    fails(@() octave_demo('unknown'), 'unknown native operation');
    fails(@() octave_demo(1), 'row vector');
    for i = 1:20
      fails(@() demo.r_demo__fail(), 'native failure', 'rosetta:native');
      fails(@() v('unknown_error'), 'unknown C++ exception', 'rosetta:native');
      fails(@() v('allocation_error'), 'allocation', 'rosetta:allocation');
      assert(demo.r_demo__add(20, 22) == 42);
    endfor
  case 'objects'
    assert(Counter.live() == 0);
    fails(@() Counter.new(-1), 'negative initial');
    assert(Counter.live() == 0);
    fails(@() Counter.new(), 'number');
    c = Counter.new(20);
    alias_owner = c;
    assert(Counter.live() == 1);
    assert(c.add(22) == 42);
    assert(c.count == 42 && c.initial == 20 && Counter.answer() == 42);
    c.count = 7;
    assert(alias_owner.value() == 7);
    c.label = '世界😀';
    assert(isequal(alias_owner.label, '世界😀'));
    c.label = char([97, 0, 98]);
    assert(isequal(c.label, char([97, 0, 98])));
    fails(@() set_property(c, 'initial', 1), 'readonly');
    fails(@() set_property(c, 'count', true), 'numeric');
    fails(@() set_property(c, 'label', 1), 'row vector');
    fails(@() set_property(c, 'unknown', 1), 'unknown native property');
    fails(@() c.unknown(), 'unknown native method');
    fails(@() c.unknown, 'unknown native property');
    fails(@() c.add, 'parentheses');
    fails(@() c(1), 'named properties');
    fails(@() c.add(1, 2), 'number');
    foreign = Series.new();
    fails(@() c.add(foreign), 'numeric');
    foreign.close(); clear foreign;
    path = [tempname() '.mat'];
    unwind_protect
      fails(@() save_owner(c, path), 'cannot be serialized', 'rosetta:ownership');
      fails(@() save_owner(c, path, '-text'), 'cannot be serialized', 'rosetta:ownership');
      if (__octave_config_info__('build_features').HDF5)
        fails(@() save_owner(c, path, '-hdf5'), 'cannot be serialized', 'rosetta:ownership');
      endif
    unwind_protect_cleanup
      if (exist(path, 'file')), unlink(path); endif
    end_unwind_protect
    assert(mislocked('octave_demo'));
    clear octave_demo;
    assert(alias_owner.value() == 7);
    c.close(); c.close();
    assert(Counter.live() == 0);
    fails(@() alias_owner.value(), 'closed');
    fails(@() alias_owner.count, 'closed');
    fails(@() set_property(alias_owner, 'count', 1), 'closed');
    clear c alias_owner;
    c = Counter.new(1);
    aliases = {c, struct('owner', c)};
    clear c;
    assert(Counter.live() == 1);
    clear aliases;
    assert(Counter.live() == 0);
    c = Counter.new(20);
    cleanup = onCleanup(@() c.close());
    clear cleanup;
    assert(Counter.live() == 0);
    fails(@() c.value(), 'closed');
    clear c;
    temporary_owners(Counter, Series, Worklist, Task);
    assert(Counter.live() == 0 && Series.live() == 0 && Worklist.live() == 0 && Task.live() == 0 && Task.workers() == 0);
  case 'features'
    assert(v('maximum', 20, 42) == 42);
    assert(v('template_box', 42) == 42);
    assert(v('variadic_sum', 10, 20, 12) == 42);
    assert(v('constant_answer') == 42);
    assert(v('constrained_sum', 20, 22) == 42);
    assert(v('hierarchy_value', 42) == 42);
    assert(v('hierarchy_crosscast'));
    assert(v('rosetta_r_c_identity', 42) == 42);
    fails(@() v('constrained_sum', 2^31 - 1, 1), 'overflow', 'rosetta:range');
    fails(@() v('variadic_sum', 2^31 - 1, 1, 1), 'overflow');
    fails(@() Series.new(0, -1), 'series count', 'rosetta:index');
    s = Series.new();
    assert(s.size() == 2 && s.get() == 20 && s.scaled_sum(2) == 82);
    assert(strcmp(s.text(), '[20, 21]'));
    s.set(1, 22);
    assert(s.scaled_sum(1) == 42);
    fails(@() s.get(2), 'out of range', 'rosetta:index');
    fails(@() s.set(-1, 1), 'out of range');
    fails(@() s.scaled_sum(2^31 - 1), 'overflow');
    s.close(); clear s;
    expected = {{'m', 'z', 'a'}, {'a', 'z', 'm'}, {'z', 'm', 'a'}, {'a', 'm', 'z'}};
    for order = 1:4
      q = Worklist.new(order);
      q.push('a'); q.push('z'); q.push('m');
      assert(q.size() == 3);
      actual = {q.pop(), q.pop(), q.pop()};
      assert(isequal(actual, expected{order}));
      fails(@() q.pop(), 'empty', 'rosetta:index');
      q.close(); clear q;
    endfor
    fails(@() Worklist.new(0), 'unknown worklist order', 'rosetta:argument');
    task = Task.new(21, 0, false);
    assert(task.wait(2000) && task.result() == 42 && task.result() == 42);
    task.close(); clear task;
    task = Task.new(1, 0, true);
    assert(task.wait(2000));
    fails(@() task.result(), 'async worker failure');
    task.close(); clear task;
    task = Task.new(1, 60000, false);
    assert(! task.ready() && ! task.wait(0) && task.cancel() && task.wait(2000));
    fails(@() task.result(), 'cancelled');
    task.close(); clear task;
    fails(@() Task.new(1, -1, false), 'delay', 'rosetta:index');
    temporary_owners(Counter, Series, Worklist, Task);
    assert(Counter.live() == 0 && Series.live() == 0 && Worklist.live() == 0 && Task.live() == 0 && Task.workers() == 0);
    # Leave a pending worker for interpreter shutdown. CTest bounds this to 20s.
    pending = Task.new(1, 60000, false);
  case 'enums'
    values = octave_values();
    Mode = values.octave_values__Mode;
    Signed = values.octave_values__Signed;
    Wide = values.octave_values__Wide;
    Logical = values.octave_values__Logical;
    State = values.octave_values__Holder__State;
    assert(isa(Mode.add, 'uint64') && Mode.add == Mode.plus);
    assert(values.octave_values__echo() == Mode.add);
    assert(values.octave_values__echo(Mode.multiply) == Mode.multiply);
    assert(values.octave_values__echo_legacy(-42) == -42);
    fails(@() values.octave_values__echo_legacy(int64(2147483647)), 'unknown enum value');
    assert(isequal(Signed.low, intmin('int64')) && isequal(Signed.high, intmax('int64')));
    assert(isequal(values.octave_values__echo_signed(Signed.low), Signed.low));
    assert(isequal(values.octave_values__echo_signed(Signed.high), Signed.high));
    assert(isequal(Wide.exact, bitshift(uint64(1), 53) + uint64(1)));
    assert(isequal(Wide.high, intmax('uint64')));
    assert(isequal(values.octave_values__echo_wide(Wide.high), Wide.high));
    assert(isequal(values.octave_values__echo_wide(Wide.exact), Wide.exact));
    assert(isequal(values.octave_values__echo_logical(Logical.on), true));
    fails(@() values.octave_values__echo_logical(1), 'logical scalar');
    for bad = {0, 3, 255}
      fails(@() values.octave_values__echo(bad{1}), 'unknown enum value', 'rosetta:conversion');
    endfor
    for bad = {true, '1', [], [1, 2], 1 + 1i, sparse(1), NaN, 1.5}
      fails(@() values.octave_values__echo(bad{1}), 'expected', 'rosetta:conversion');
    endfor
    fails(@() values.octave_values__echo(256), 'range');
    fails(@() values.octave_values__echo(-1), 'range');
    fails(@() values.octave_values__echo_wide(2^54), 'exact integer');
    fails(@() values.octave_values__invalid_result(), 'unknown C++ enum result', 'rosetta:conversion');
    Holder = values.octave_values__Holder;
    owner = Holder.new(Mode.multiply);
    assert(owner.get() == Mode.multiply && owner.state == State.ready && Holder.status() == State.ready);
    owner.set(Mode.add);
    assert(owner.mode == Mode.add);
    owner.mode = Mode.multiply;
    fails(@() set_property(owner, 'mode', 3), 'unknown enum value');
    assert(owner.get() == Mode.multiply);
    fails(@() owner.set(3), 'unknown enum value');
    fails(@() Holder.new(3), 'unknown enum value');
    owner.close(); clear owner;
    assert(values.octave_values__echo(Mode.add) == Mode.add);
  case 'repl'
    source(args{3});
    assert(demo.r_demo__add(20, 22) == 42);
    c = Counter.new(20);
    assert(c.add(22) == 42);
    clear c;
    s = Series.new();
    assert(s.scaled_sum(2) == 82);
    clear s;
    assert(Counter.live() == 0 && Series.live() == 0);
  otherwise
    error(['unknown test family ' family]);
endswitch
printf('Octave %s checks passed\n', family);
