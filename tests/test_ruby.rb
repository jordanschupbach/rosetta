require 'rbconfig'
require 'timeout'
family, demo_path, verify_path, startup = ARGV
require File.expand_path(demo_path)
require File.expand_path(verify_path)
Counter = RubyDemo::R_demo__Counter unless family == 'repl'
Series = RubyVerify::R_verify__Series unless family == 'repl'
Worklist = RubyVerify::R_verify__Worklist unless family == 'repl'
Task = RubyVerify::R_verify__AsyncTask unless family == 'repl'
$checks = 0

def check(value, message = 'assertion failed')
  $checks += 1
  raise message unless value
end

def equal(expected, actual)
  check(expected == actual, "expected #{expected.inspect}, got #{actual.inspect}")
end

def raises(type, pattern = nil)
  $checks += 1
  begin
    yield
  rescue Exception => error
    raise "expected #{type}, got #{error.class}: #{error.message}" unless error.is_a?(type)
    raise "missing #{pattern.inspect} in #{error.message}" if pattern && !pattern.match?(error.message)
    return error
  end
  raise "expected #{type}"
end

def v(name, *args)
  RubyVerify.public_send("r_verify__#{name}", *args)
end

def collect
  3.times { GC.start(full_mark: true, immediate_sweep: true) }
end

def child(code, *args)
  pid = spawn(RbConfig.ruby, '-e', code, *args)
  begin
    status = Timeout.timeout(10) { Process.wait2(pid).last }
    pid = nil # A reaped PID can be reused; cleanup is only for a pending child.
    check(status.success?, 'Ruby child failed')
  ensure
    begin
      if pid
        Process.kill('KILL', pid)
        Process.wait(pid)
      end
    rescue Errno::ESRCH, Errno::ECHILD
    end
  end
end

case family
when 'boundary'
  require File.expand_path(startup)
  20.times do
    equal(:ok, RubyRuntimeProbe.run { equal(1, RubyRuntimeProbe.live); :ok })
    equal(0, RubyRuntimeProbe.live)
    raises(RuntimeError, /Ruby block failure/) { RubyRuntimeProbe.run { raise 'Ruby block failure' } }
    equal(0, RubyRuntimeProbe.live)
    equal(:broken, RubyRuntimeProbe.run { break :broken })
    equal(0, RubyRuntimeProbe.live)
    raises(TypeError, /Ruby allocation probe/) { RubyRuntimeProbe.wrap_failure }
    equal(0, RubyRuntimeProbe.live)
    raises(ArgumentError, /native exception probe/) { RubyRuntimeProbe.native_failure }
    equal(0, RubyRuntimeProbe.live)
  end
when 'scalars'
  %w[char schar uchar short ushort int uint long ulong ll ull].each do |name|
    equal(42, v("echo_#{name}", 42))
    [nil, true, false, 1.0, '42', [], {}, Complex(1, 2), Rational(1, 2)].each do |bad|
      raises(TypeError) { v("echo_#{name}", bad) }
    end
    raises(RangeError) { v("echo_#{name}", 1 << 100) }
    raises(RangeError) { v("echo_#{name}", -(1 << 100)) }
  end
  ranges = { 'schar' => [-128, 127], 'uchar' => [0, 255], 'short' => [-32768, 32767],
             'ushort' => [0, 65535], 'int' => [-(1 << 31), (1 << 31) - 1],
             'uint' => [0, (1 << 32) - 1], 'll' => [-(1 << 63), (1 << 63) - 1],
             'ull' => [0, (1 << 64) - 1] }
  ranges.each do |name, (lo, hi)|
    equal(lo, v("echo_#{name}", lo))
    equal(hi, v("echo_#{name}", hi))
    [lo - 1, hi + 1].each { |bad| raises(RangeError) { v("echo_#{name}", bad) } }
  end
  %w[uchar ushort uint ulong ull].each { |name| raises(RangeError) { v("echo_#{name}", -1) } }
  equal(9007199254740993, RubyDemo.r_demo__too_large)
  equal(1.25, v('echo_float', 1.25))
  equal(Float::MAX, v('echo_double', Float::MAX))
  equal(42.0, v('echo_double', 42))
  [Float::INFINITY, -Float::INFINITY, Float::NAN, 1 << 10000].each do |bad|
    raises(RangeError) { v('echo_double', bad) }
  end
  raises(RangeError) { v('echo_float', 1e39) }
  raises(RangeError) { v('nonfinite_result') }
  [nil, true, false, '1', Object.new].each { |bad| raises(TypeError) { v('echo_double', bad) } }
  equal(true, v('echo_bool', true))
  equal(false, v('echo_bool', false))
  [nil, 0, 1, 'true'].each { |bad| raises(TypeError) { v('echo_bool', bad) } }
  ['', 'world', '世界😀', "a\0b", "\xff\xfe".b, 'long' * 10000].each do |text|
    result = v('echo_string', text)
    equal(text.b, result)
    equal(Encoding::BINARY, result.encoding)
  end
  equal("a\0b".b, v('embedded_nul'))
  coercible = Object.new
  def coercible.to_int = raise('must not coerce')
  def coercible.to_f = raise('must not coerce')
  def coercible.to_str = raise('must not coerce')
  %w[int double string].each { |name| raises(TypeError) { v("echo_#{name}", coercible) } }
  equal(42, v('defaulted'))
  equal(12, v('defaulted', 12))
  equal(42, v('alias_identity', 42))
  equal(nil, v('do_nothing'))
  raises(ArgumentError) { v('echo_int') }
  raises(ArgumentError) { v('echo_int', 1, 2) }
  raises(ArgumentError, /positional/) { RubyVerify.r_verify__defaulted(value: 1) }
  20.times do
    raises(RuntimeError, /native failure/) { RubyDemo.r_demo__fail }
    raises(RuntimeError, /unknown C\+\+ exception/) { v('unknown_error') }
    raises(NoMemoryError) { v('allocation_error') }
    equal(42, RubyDemo.r_demo__add(20, 22))
  end
when 'objects'
  equal(0, Counter.live)
  raises(RuntimeError, /negative initial/) { Counter.new(-1) }
  equal(0, Counter.live)
  raises(ArgumentError) { Counter.new }
  raises(ArgumentError, /positional/) { Counter.new(value: 1) }
  c = Counter.new(20)
  alias_owner = c
  equal(1, Counter.live)
  equal(42, c.add(22))
  equal(42, c.count)
  equal(20, c.initial)
  equal(42, Counter.answer)
  c.count = 7
  c.label = "世界😀\0text"
  equal(7, c.value)
  equal("世界😀\0text".b, c.label)
  raises(NoMethodError) { c.initial = 3 }
  raises(TypeError) { c.count = true }
  raises(TypeError) { c.label = nil }
  raises(RuntimeError, /already initialized/) { c.send(:initialize, 1) }
  raises(TypeError) { c.dup }
  raises(TypeError) { c.clone }
  raises(TypeError) { Marshal.dump(c) }
  raises(TypeError) { Class.new(Counter) }
  raises(TypeError) { Counter.instance_method(:value).bind(Series.new).call }
  collect
  c.freeze
  equal(7, c.value)
  raises(FrozenError) { c.add(1) }
  raises(FrozenError) { c.count = 1 }
  raises(FrozenError) { c.label = 'x' }
  equal(nil, c.close)
  equal(nil, c.close)
  equal(0, Counter.live)
  raises(RuntimeError, /closed/) { alias_owner.value }
  raises(RuntimeError) { alias_owner.count }
  raises(RuntimeError) { c.send(:initialize, 1) } # FrozenError is a RuntimeError.
  raw = Counter.allocate
  raises(RuntimeError, /uninitialized/) { raw.value }
  raw.send(:initialize, 4)
  equal(4, raw.value)
  raw.close
  raises(RuntimeError, /already initialized/) { raw.send(:initialize, 5) }
  captured = nil
  equal(42, Counter.open(20) { |owner| captured = owner; owner.add(22) })
  equal(0, Counter.live)
  raises(RuntimeError) { captured.value }
  raises(RuntimeError, /block failure/) { Counter.open(1) { raise 'block failure' } }
  equal(0, Counter.live)
  equal(:done, Counter.open(1) { break :done })
  equal(0, Counter.live)
  raises(ArgumentError, /block/) { Counter.open(1) }
  child('RubyDemo = Module.new; begin; require ARGV.fetch(0); abort "accepted existing module"; rescue NameError => e; raise unless e.message.include?("already exists"); raise unless RubyDemo.singleton_methods.empty?; end', File.expand_path(demo_path))
  def temporary_owners
    100.times { Counter.new(1) }
    cycle = Counter.new(2)
    cycle.instance_variable_set(:@cycle, cycle)
    nil
  end
  # Retire the allocating stack so conservative C stack roots do not retain
  # its last temporary and make a GC assertion depend on compiler registers.
  Thread.new { temporary_owners }.join
  collect
  equal(0, Counter.live)
  owner = Counter.new(2)
  GC.verify_compaction_references(double_heap: true, toward: :empty) if GC.respond_to?(:verify_compaction_references)
  equal(2, owner.value)
  owner.close
  equal(0, Counter.live)
when 'features'
  equal(42, v('maximum', 20, 42))
  equal(42, v('template_box', 42))
  equal(42, v('variadic_sum', 10, 20, 12))
  equal(42, v('constant_answer'))
  equal(42, v('constrained_sum', 20, 22))
  equal(42, v('hierarchy_value', 42))
  equal(true, v('hierarchy_crosscast'))
  equal(42, v('rosetta_r_c_identity', 42))
  raises(RangeError) { v('constrained_sum', (1 << 31) - 1, 1) }
  raises(RangeError) { v('variadic_sum', (1 << 31) - 1, 1, 1) }
  raises(IndexError) { Series.new(0, -1) }
  Series.open do |s|
    equal(2, s.size)
    equal(20, s.get)
    equal(82, s.scaled_sum(2))
    equal('[20, 21]'.b, s.text)
    s.set(1, 22)
    equal(42, s.scaled_sum(1))
    raises(IndexError) { s.get(2) }
    raises(IndexError) { s.set(-1, 1) }
    raises(RangeError) { s.scaled_sum((1 << 31) - 1) }
    s.freeze
    equal(20, s.get)
    raises(FrozenError) { s.set(0, 1) }
  end
  equal(0, Series.live)
  expected = [%w[m z a], %w[a z m], %w[z m a], %w[a m z]]
  (1..4).each do |order|
    Worklist.open(order) do |q|
      %w[a z m].each { |text| q.push(text) }
      equal(3, q.size)
      equal(expected[order - 1], 3.times.map { q.pop })
      raises(IndexError, /empty/) { q.pop }
    end
  end
  raises(ArgumentError) { Worklist.new(0) }
  Task.open(21, 0, false) do |task|
    check(task.wait(2000))
    equal(42, task.result)
    equal(42, task.result)
  end
  Task.open(1, 0, true) do |task|
    check(task.wait(2000))
    raises(RuntimeError, /async worker failure/) { task.result }
  end
  Task.open(1, 60000, false) do |task|
    equal(false, task.ready)
    equal(false, task.wait(0))
    equal(true, task.cancel)
    check(task.wait(2000))
    raises(RuntimeError, /cancelled/) { task.result }
  end
  raises(IndexError) { Task.new(1, -1, false) }
  def pending_objects
    Task.new(1, 60000, false)
    Worklist.new(2)
    Series.new
    nil
  end
  Thread.new { pending_objects }.join
  collect
  equal(0, Series.live)
  equal(0, Worklist.live)
  equal(0, Task.live)
  equal(0, Task.workers)
  child('require ARGV.fetch(0); $task = RubyVerify::R_verify__AsyncTask.new(1, 60000, false)', File.expand_path(verify_path))
when 'repl'
  ARGV.replace([demo_path, verify_path])
  load startup
  equal(42, RubyDemo.r_demo__add(20, 22))
  equal(42, Counter.open(20) { |owner| owner.add(22) })
  equal(82, Series.open { |s| s.scaled_sum(2) })
  check(defined?(AsyncTask))
else
  raise "unknown test family #{family}"
end
puts "Ruby #{family}: #{$checks} checks passed"
