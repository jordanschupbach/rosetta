# frozen_string_literal: true
# Full Ruby API (--ruby-api full) on the library-shaped example shared with the Python, C#,
# Julia, Common Lisp and Fortran backends. Argument: the generated features.rb, which loads the
# native extension (features/features.so beside it) and carries the YARD documentation.
require 'set'

DOC_FILE = File.expand_path(ARGV.fetch(0))
require DOC_FILE

$checks = 0
def check(ok, what)
  $checks += 1
  raise "check failed: #{what}" unless ok
end

def fails(pattern, klass = StandardError)
  $checks += 1
  begin
    yield
  rescue Exception => e # rubocop:disable Lint/RescueException
    raise "expected #{klass}, got #{e.class}: #{e.message}" unless e.is_a?(klass)
    raise "expected an error containing #{pattern.inspect}, got: #{e.message}" unless e.message.include?(pattern)
    return e
  end
  raise "expected an error containing #{pattern.inspect}, got none"
end

def collect
  3.times { GC.start(full_mark: true, immediate_sweep: true) }
  GC.compact if GC.respond_to?(:compact)
end

SOURCE = File.read(DOC_FILE).lines
# The comment block right above the first line containing `declaration`.
def doc(declaration, after: nil)
  from = after ? SOURCE.index { |line| line.include?(after) } : 0
  at = SOURCE.each_index.find { |i| i >= from && SOURCE[i].include?(declaration) && !SOURCE[i].lstrip.start_with?('#') }
  raise "not declared: #{declaration}" unless at
  block = []
  i = at - 1
  while i >= 0 && SOURCE[i].lstrip.start_with?('#')
    block.unshift(SOURCE[i].strip.sub(/\A# ?/, ''))
    i -= 1
  end
  block.join("\n")
end

include Features

# Directors: Ruby subclasses implement C++ virtual methods.
class Triangle < Shape
  def initialize(base, height)
    super()
    @base = base
    @height = height
  end

  def area = @base * @height / 2.0
  def name = 'triangle'
end

class Incomplete < Shape; end

class Failing < Shape
  def area = raise(ZeroDivisionError, 'from Ruby')
end

# super runs the native implementation rather than dispatching back to the override.
class Loud < Circle
  def name = super.upcase
end

class Noisy < Circle
  def name = "#{super.downcase}!"
end

class Louder < Noisy; end

class UnitShape < Shape
  attr_accessor :tag

  def area = 1.0
end

class Summer < Visitor
  attr_reader :seen

  def visit(value) = value * 10
  def finished(values) = (@seen = values)
end

class NoSuper < Shape
  def initialize; end # forgets super
end

# --- Classes, inheritance and virtual dispatch ---------------------------------------------------
c1 = Circle.new(2)
check(c1.area == 12 && c1.name == 'circle' && c1.twice_area == 24 && c1.radius == 2, 'circle methods')
check(c1.inspect.include?('Features::Circle'), 'inspect names the class')
check(c1.is_a?(Shape) && c1.is_a?(NativeObject) && Circle.superclass == Shape, 'class hierarchy')
check(Circle.ancestors.include?(Features::NativeObject), 'NativeObject ancestor')
fails('negative radius', Features::ArgumentError) { Circle.new(-1) }
fails('negative radius', ::ArgumentError) { Circle.new(-1) }
e = fails('negative radius', Features::Error) { Circle.new(-1) }
check(e.cpp_class == 'std::invalid_argument', 'exception cpp_class')
s = Square.new
check(s.side == 1 && s.area == 1, 'default constructor')
s.side = 3
check(s.area == 9 && Square.new(2).area == 4, 'field set')
check(area_of(s) == 9 && describe(c1) == 'circle:0', 'functions on classes')
check(Features.area_of(s) == 9, 'module function called on the module')
check(Circle.new(2.5).radius == 2.5 && Circle.new(3).radius == 3.0, 'Integer converts to Float')
fails('Float', ::TypeError) { Circle.new('2') }
fails('wrong number', ::ArgumentError) { Circle.new }
fails('cannot be constructed', ::TypeError) { Target.new }

# --- Enums --------------------------------------------------------------------------------------
check(c1.kind == :circle && s.kind == :square, 'enum results are Symbols')
check(Kind::CIRCLE == :circle && Kind::CUSTOM == :custom, 'enum constants')
check(filled(:solid) && !filled(:None) && filled(Fill::SOLID), 'enum arguments')
check(filled(Fill.to_i(:solid)), 'enum Integer argument')
check(Kind.values == %i[circle square custom], 'enum values')
check(Kind.to_h == { circle: 0, square: 1, custom: 7 }, 'enum to_h')
check(Kind.to_i(:custom) == 7 && Kind.from_i(7) == :custom && Kind.from_i(99).nil?, 'enum to_i/from_i')
fails('unknown enumerator :hexagon', ::ArgumentError) { make_shape(:hexagon, 1) }
fails('Symbol', ::TypeError) { make_shape('circle', 1) }
check(Legacy::LEGACY_A == :legacy_a && Features::LEGACY_B == :legacy_b, 'unscoped enumerators also module constants')
check(Shape::SIDES_UNKNOWN == -1 && (GOLDEN - 1.618).abs < 1e-12, 'constants')
check(Shape.const_defined?(:SIDES_UNKNOWN) && Features.constants.include?(:GOLDEN), 'constants defined')

# --- Most-derived results, ownership transfer and shared ownership ------------------------------
made = make_shape(:circle, 1)
check(made.instance_of?(Circle) && made.radius == 1, 'most-derived result')
check(make_shape(:square, 2).instance_of?(Square), 'most-derived square')
check(made.owned? && made.alive? && !made.closed?, 'owned result')
canvas = Canvas.new
canvas.add(make_shape(:square, 2))
canvas.share(shared_square(3))
transferred = Circle.new(1)
canvas.add(transferred)
fails('closed', ::RuntimeError) { transferred.area }
check(transferred.closed? && !transferred.alive?, 'transferred object is closed')
check(canvas.size == 3 && canvas.total == 4 + 9 + 3, 'ownership transfer')
first = canvas.at(0)
check(first.instance_of?(Square) && first.area == 4 && !first.owned?, 'borrowed element')
fails('cannot transfer ownership', ::ArgumentError) { canvas.add(first) }
shared = shared_square(2)
canvas.share(shared)
check(shared.area == 4 && canvas.total == 20, 'shared ownership')
check(total_area([Circle.new(1), shared]) == 7, 'array of objects')
fails('Array', ::TypeError) { total_area(Circle.new(1)) }
check(!canvas.is_a?(Enumerable), 'no each from size and at')

# --- Overload resolution --------------------------------------------------------------------------
check(pick(1) == 'int' && pick(1.5) == 'double' && pick(2.0) == 'double' && pick('x') == 'string', 'scalar overloads')
check(pick([1.0, 2.0]) == 'vector' && pick(s) == 'shape', 'container and class overloads')
check(pick([1, 2]) == 'vector', 'lenient pass converts Integer elements')
fails('pick(', ::TypeError) { pick(nil) }
fails('wrong number', ::ArgumentError) { pick(:sym, 2, 3) }
check(add(1) == 111 && add(1, 2) == 103 && add(1, 2, 3) == 6, 'default arguments')
check(add(1, c: 3) == 14 && add(1, b: 2, c: 3) == 6 && add(a: 1) == 111, 'keyword arguments skip defaults')
check(add(c: 0, a: 5) == 15, 'keywords in any order')
fails('wrong number', ::ArgumentError) { add }
fails('unknown keywords', ::ArgumentError) { add(1, d: 3) }
check(Square.new(2).area == 4 && Square.new.area == 1, 'constructor overloads')

# --- Integer range checks and bigints -----------------------------------------------------------
fails('outside the range', ::RangeError) { add(2**40) }
fails('outside the range', ::RangeError) { add(2**80) }
fails('Integer', ::TypeError) { add(1.5) }
check(add(-(2**31), 0, 0) == -(2**31), 'int minimum')

# --- Containers ---------------------------------------------------------------------------------
counts = count_words(%w[a b a])
check(counts.is_a?(Hash) && counts == { 'a' => 2, 'b' => 1 }, 'Hash result')
check(min_max([3.0, 1.0, 2.0]) == [1.0, 3.0], 'pair as Array')
check(min_max([3, 1, 2]) == [1.0, 3.0], 'Integer elements convert')
fails('empty input', Features::IndexError) { min_max([]) }
check(find([5, 7], 7) == 1 && find([5, 7], 9).nil?, 'optional result is nil')
tr = transpose([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]])
check(tr.size == 3 && tr[0] == [1.0, 4.0], 'nested arrays')
check(byte_sum([1, 2, 250]) == 253 && byte_sum("\x01\x02".b) == 3, 'bytes from Array or String')
check(bytes_of('AZ') == [65, 90], 'bytes result')
fails('outside the range', ::RangeError) { byte_sum([256]) }
cs = circles(3)
check(cs.size == 3 && cs[2].radius == 3 && cs.all? { |c| c.instance_of?(Circle) }, 'array of class results')
check(repeat('z', 3) == 'zzz' && initial('hello') == 'h', 'chars')
check(repeat('z', 3).encoding == Encoding::UTF_8, 'UTF-8 strings')
check(initial('héllo') == 'h' && count_words(['ü', 'ü'])['ü'] == 2, 'UTF-8 round trip')
fails('repeat(c [String]', ::TypeError) { repeat(1, 3) }
fails('no overload accepts (String, Integer)', ::TypeError) { repeat('zz', 3) }
values = [1.0, 2.0]
scale(values, 3)
check(values == [3.0, 6.0], 'mutable reference argument written back')
fails('negative count', Features::RuntimeError) { repeat('z', -1) }
check(defaults == 'x*1-1.500000', 'all defaults')
check(defaults(label: 'y') == 'y*1-1.500000', 'keyword skips leading defaults')
check(defaults(:circle, 'q') == 'q*0-1.500000', 'positional defaults')
check(defaults(weight: 1.0, kind: :custom) == 'x*7-1.000000', 'keyword enum default')
check(doc('def self.defaults(').include?('@option options [Float] :weight (0.5)'), 'keyword options documented')
check(doc('def self.defaults(').include?('@param weight [Float] (default: `0.5`)'), 'defaults documented')

# --- Callbacks both ways ------------------------------------------------------------------------
check(apply(->(x) { x * 2 }, 21) == 42, 'lambda callback')
check(apply(proc { |x| x + 1 }, 1) == 2, 'proc callback')
check(apply(21) { |x| x * 2 } == 42, 'block fills the callback')
check(apply(x: 4) { |x| x * 3 } == 12, 'block with keywords')
doubler = Object.new
def doubler.twice(x) = x * 2
check(apply(doubler.method(:twice), 5) == 10, 'Method object callback')
callable = Object.new
def callable.call(x) = x - 1
check(apply(callable, 5) == 4, 'object with #call')
fails('inside callback', ::RuntimeError) { apply(->(_x) { raise 'inside callback' }, 1) }
fails('divided', ::ZeroDivisionError) { apply(->(x) { x.to_i / 0 }, 1) }
e = fails('custom', KeyError) { apply(->(_x) { raise KeyError, 'custom' }, 1) }
check(e.instance_of?(KeyError), 'the Ruby exception comes back as itself')
fails('does not convert', ::TypeError) { apply(->(_x) { 'nope' }, 1) }
check(catch(:done) { apply(->(_x) { throw :done, 7 }, 1) } == 7, 'throw through C++')
check([1, 2].sum { |i| apply(->(x) { x * i }, 1) } == 3, 'break-free iteration')
plus = adder(40)
check(plus.call(2) == 42 && plus.(2) == 42 && plus[2] == 42, 'native function is callable')
check(plus.is_a?(NativeCallable) && plus.arity == 1, 'NativeCallable')
check([1, 2].map(&plus) == [41, 42], 'to_proc')
fails('expected Integer, got Float', ::TypeError) { apply(plus, 2) } # Float arguments do not truncate
check(apply(->(x) { adder(1).call(x.to_i) }, 3) == 4, 'native function inside a callback')
check(Thread.new { apply(->(x) { x + 1 }, 1) }.value == 2, 'callback from another Ruby thread')
fails('Proc', ::TypeError) { apply(5, 1) }
fails('wrong number', ::ArgumentError) { plus.call }

# --- Operators, chaining and borrowed references ------------------------------------------------
v = Vec2.new(1, 2)
w = v + Vec2.new(3, 4)
check(w.x == 4 && w.y == 6 && (v * 2).y == 4 && (-v).x == -1 && (v - w).x == -3, 'operators')
check(v == Vec2.new(1, 2) && v != w && !(v == w), 'equality')
check(v != 5 && !(v == 'x'), 'equality with other types is false')
check(v.eql?(v) && !v.eql?(Vec2.new(1, 2)), 'eql? stays identity without std::hash')
check(v[1] == 2 && v.inner(Vec2.new(1, 1)) == 3, 'indexer')
fails('Vec2 index', Features::IndexError) { v[2] }
fails('Vec2 index', ::IndexError) { v[2] }
check(!v.respond_to?(:[]=), 'no []= from a const operator[]')
chained = v.set_x(10).set_y(20)
check(v.x == 10 && v.y == 20 && chained.x == 10, 'chaining')
chained.x = 11
check(v.x == 11, 'chained result borrows the same object')
check(v.respond_to?(:-@) && v.respond_to?(:+), 'operator methods')
seg = Segment.new
seg.end_ = Vec2.new(3, 4)
check(seg.length2 == 25, 'field object set')
start = seg.first
start.x = 3
check(seg.start.x == 3 && seg.length2 == 16, 'borrowed reference writes through')
seg.start.y = 4
check(seg.length2.zero?, 'field object borrowed')
check(Vec2.new(1, 2).clone_.y == 2, 'clone_ (renamed from the reserved clone)')
dup = Vec2.new(5, 6).dup
check(dup.x == 5 && dup.owned?, 'dup copies the C++ object')
cl = Vec2.new(5, 6)
cl2 = cl.clone
cl2.x = 9
check(cl.x == 5 && cl2.x == 9, 'clone is independent')
check(sum([Vec2.new(1, 2), Vec2.new(3, 4)]) == 10, 'array of values')
held = Segment.new.first
collect
check(held.x.zero?, 'borrowed reference keeps its owner alive')
fz = Vec2.new(1, 2).freeze
fails('frozen', ::FrozenError) { fz.x = 3 }
check(fz.x == 1 && fz.inner(Vec2.new(1, 0)) == 1, 'frozen object still reads')

# --- Templates, extensions, qualified names, copy types -----------------------------------------
stack = IntStack.new
stack.push(1)
stack.push(2)
check(stack.size == 2 && stack.items == [1, 2] && stack.pop == 2, 'template instance')
check(clamp_int(42) == 10 && clamp_int(3, 5) == 3 && clamp_real(2.5) == 2.5, 'function templates')
check(c1.label == '#circle' && c1.label('@') == '@circle' && Circle.unit.radius == 1, 'extensions')
check(c1.label(prefix: '!') == '!circle', 'extension keyword')
check(features_legacy_version == 2 && FeaturesLegacyTag.new.id == 7, 'qualified names')
check(FeaturesLegacyMode::OFF == :off, 'qualified enum')
check(Circle.new(2).diameter == 4, 'extension method')
ledger = Ledger.new
copy = ledger.latest
copy.value = 5
check(ledger.stamp.value == 1, 'copy type')
aim = Aim.new
aim.origin.x = 2
check(aim.target.point.x == 2, 'nested borrowed fields')
check(!Target.method_defined?(:point=) && Target.method_defined?(:point), 'read-only reference field has no setter')
check(Target.method_defined?(:scale=), 'value field has a setter')

# --- Errors, closing and garbage collection ------------------------------------------------------
fails('Shape', ::TypeError) { area_of(nil) }
fails('Shape', ::TypeError) { area_of(Vec2.new(1, 2)) }
x = Circle.new(1)
check(x.area == 3, 'before close')
x.close
check(x.closed? && !x.alive?, 'closed')
fails('closed', ::RuntimeError) { x.area }
x.close
check(x.inspect.include?('closed'), 'inspect shows closed')
(1..200).each do |i|
  tmp = make_shape(:square, i)
  check(tmp.area == i * i, 'many objects')
  tmp.close if i.even?
end
collect
check(Features::Error.instance_of?(Module) && Features::IndexError < ::IndexError, 'error hierarchy')
check(Features::NotImplementedError.include?(Features::Error), 'errors include Error')

# --- Directors -------------------------------------------------------------------------------------
t = Triangle.new(4, 3)
check(t.area == 6 && area_of(t) == 6 && t.twice_area == 12 && describe(t) == 'triangle:7', 'Ruby override called from C++')
check(total_area([t, Circle.new(1), Square.new(2)]) == 13 && t.label('@') == '@triangle', 'mixed overrides')
check(t.kind == :custom, 'native default implementation of a virtual')
fails('pure virtual method', ::NotImplementedError) { area_of(Incomplete.new) }
fails('pure virtual method', Features::NotImplementedError) { Incomplete.new.area }
fails('from Ruby', ::ZeroDivisionError) { area_of(Failing.new) }
fails('abstract', ::TypeError) { Shape.new }
check(describe(Loud.new(1)) == 'CIRCLE:0' && Loud.new(2).area == 12, 'super runs the C++ method')
check(describe(Louder.new(1)) == 'circle!:0', 'deeper subclasses')
fails('closed', ::RuntimeError) { NoSuper.new.area }
canvas = Canvas.new
u = UnitShape.new
u.tag = 'kept'
canvas.add(u)
check(u.area == 1 && !u.owned?, 'subclass object owned by C++')
u = nil
collect
canvas.share(Square.new(2))
check(canvas.total == 5 && canvas.size == 2, 'C++ keeps the Ruby object alive')
back = canvas.at(0)
check(back.instance_of?(UnitShape) && back.tag == 'kept' && back.equal?(canvas.at(0)), 'the same Ruby object comes back')
fails('cannot be closed', ::RuntimeError) { back.close }
back = nil
canvas.clear
collect
canvas.share(UnitShape.new)
collect
check(canvas.total == 1, 'shared subclass object')
canvas.clear
collect
summer = Summer.new
check(run_visitor(summer, [1.0, 2.0]) == 30 && summer.seen == [1.0, 2.0], 'callback interface')
check(run_visitor(Visitor.new, [1.0, 2.0]) == 3, 'native default implementation')
check(UnitShape.new.then { |shape| area_of(shape) } == 1, 'director through then')

# --- Documentation (YARD) -------------------------------------------------------------------------
d = doc('def self.add(')
check(d.include?('Adds three integers, Socha & Dorigo style.'), 'summary')
check(d.include?('@param a [Integer] first term'), 'parameter')
check(d.include?('@param b [Integer] second term (default: `10`)'), 'parameter default')
check(d.include?('@return [Integer] the sum'), 'returns')
check(d.include?('@raise [IndexError]'), 'raises')
check(d.include?("@example\n  int total = add(1);"), 'example')
check(d.include?('@see pick'), 'see')
check(d.include?('@note'), 'note')
check(doc('def self.add(').include?('@overload add(a, **options)'), 'keyword overload')
check(doc('def initialize(radius)').include?('@raise [ArgumentError]'), 'constructor raises')
check(doc('attr_accessor :x').include?('Horizontal component.'), 'field documentation')
check(doc('attr_accessor :x').include?('@return [Float]'), 'field type')
check(doc('class Shape < NativeObject').include?('Subclass it in Ruby'), 'class remarks')
check(doc('def self.pick(').include?('@overload pick(arg0)'), 'overloads')
check(doc('def self.count_words(').include?('Hash{String => Integer}'), 'Hash type')
check(doc('def self.find(').include?('[Integer, nil]'), 'optional type')
check(doc('def self.min_max(').include?('Array(Float, Float)'), 'pair type')
check(doc('def self.apply(').include?('[Proc]'), 'Proc type')
check(doc('def self.transpose(').include?('Array<Array<Float>>'), 'nested type')
check(doc('def self.filled(').include?('[Symbol]'), 'enum type')
check(doc('module Kind').include?('Symbols'), 'enum representation documented')
check(SOURCE.any? { |line| line.start_with?('require_relative') }, 'requires the extension')
check(SOURCE.any? { |line| line.strip == 'if nil # documentation only' }, 'docs are not executed')
check(Circle.instance_method(:area).owner == Circle && Circle.instance_method(:area).source_location.nil?,
      'stubs do not replace native methods')

# --- Policy modules ---------------------------------------------------------------------------------
pt = Features::Data.unit
check(pt.x == 1 && pt.y.zero?, 'submodule result')
check(Features::Data.scaled(3) == 6 && Features::Data.scaled(2.5) == 6.25, 'submodule overloads')
check(Features::Data.repeat('ab') == 'abab' && repeat('x', 3) == 'xxx', 'submodule names are separate')
check(doc('def self.unit;', after: 'module Data').include?("Mapped by the policy's modules"), 'submodule docs')

collect
puts "Ruby full API passed (#{$checks} checks)"
