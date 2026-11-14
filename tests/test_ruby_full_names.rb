# frozen_string_literal: true
# Reserved names, Ruby protocols and conversions of the full Ruby API (--ruby-api full) on
# tests/ruby_full_names.hpp. Argument: the generated names.rb (names/names.so beside it).
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

SOURCE = File.read(DOC_FILE)

# Classes named like Ruby core classes live in the module and leave the core ones alone.
check(Names::Object.new.value == 1 && Names::Object.superclass == Names::NativeObject, 'Names::Object')
check(Names::Kernel.new.puts_ == 2, 'Names::Kernel with puts_')
check(Names::Comparable.new.is_a?(Names::NativeObject) && !Names::Comparable.instance_of?(Module), 'Names::Comparable is a class')
check(Names::Complex.new.norm == 25 && Names.norm_of(Names::Complex.new) == 25, 'Names::Complex')
check(Names::Set.new.size.zero? && Names.unique([3, 1, 3]).is_a?(::Set), 'Names::Set beside ::Set')
check(Names::Proc.new.is_a?(Names::NativeObject), 'Names::Proc')
check(Names::Error_.new.code == 5 && Names::Error.instance_of?(Module), 'Error class renamed beside the Error module')
check(::Object.new.respond_to?(:puts, true) && ::Comparable.instance_of?(Module), 'core classes untouched')
check(Names.class_name_length(Names::Object.new) == 1, 'core-named class as argument')

# Methods named like keywords and Object methods.
w = Names::Words.new
check(w.end_ == 1 && w.then_ == 2 && w.begin_ == 4 && w.next_ == 5, 'keyword names get _')
check(w.object_id_ == 3 && w.object_id != 3, 'object_id stays Ruby\'s')
check(w.send_(7) == 7 && w.send(:end_) == 1, 'send stays Ruby\'s')
check(w.freeze_ == 8 && !w.frozen?, 'freeze stays Ruby\'s')
check(w.display == 6 && w.hash_code == 7, 'plain names stay')
check(w.camel_case == 10 && w.http_server == 11, 'camelCase to snake_case')
check(Names::Words.create == 9, 'static method as class method')
check(w.class == Names::Words && w.is_a?(Names::NativeObject), 'class stays Ruby\'s')

# Predicates, setters and getters.
check(w.empty? && w.is_empty && !w.has_name?, 'predicates')
w.name = 'abc'
check(w.get_name == 'abc' && w.name == 'abc' && w.has_name? && !w.empty?, 'setter and getter aliases')
w.set_name('xyz')
check(w.is_named('xyz') && !w.is_named('abc'), 'predicate with an argument keeps its name')
check(w.to_s == 'Words(xyz)' && "#{w}" == 'Words(xyz)' && w.to_string == 'Words(xyz)', 'to_string as to_s')
check(Names::Words.instance_method(:name=).arity == -1, 'setter alias is a native method')
check(Names::Table.new.to_s == 'Table(3)' && Names::Table.new.to_string(5) == 'Table(5)', 'to_string with defaults as to_s')
check(SOURCE.include?('alias_method :name=, :set_name') && SOURCE.include?('alias_method :to_s, :to_string'), 'aliases documented')
check(SOURCE.include?('def empty?; end') && SOURCE.include?('def has_name?; end'), 'predicates documented')

# Comparable, hash and eql? through ==, < and std::hash.
a = Names::Point.new(1, 2)
b = Names::Point.new(1, 3)
check(a < b && b > a && a <= Names::Point.new(1, 2) && (a <=> b) == -1 && (a <=> a).zero?, 'Comparable')
check([b, a].sort.first == a && [b, a].min == a && a.between?(a, b) && a.clamp(a, b) == a, 'Comparable methods')
check((a <=> 5).nil?, '<=> with another type is nil')
check(a.eql?(Names::Point.new(1, 2)) && a.hash == Names::Point.new(1, 2).hash && !a.eql?(b), 'eql? and hash')
check({ a => 'found' }[Names::Point.new(1, 2)] == 'found', 'value-equal Hash keys')
check([a, Names::Point.new(1, 2), b].uniq.size == 2, 'uniq')
check(Names::Point.include?(::Comparable), 'includes ::Comparable')

# each and Enumerable through size and operator[]; []= through a mutable reference.
row = Names::Row.new(4)
check(row.to_a == [0.0, 1.5, 3.0, 4.5] && row.map { |v| v * 2 } == [0.0, 3.0, 6.0, 9.0], 'each and Enumerable')
check(row.include?(3.0) && row.sum == 9.0 && row.max == 4.5 && row.each_slice(2).to_a.size == 2, 'Enumerable methods')
check(row.each.is_a?(Enumerator) && row.each.next.zero?, 'each without a block')
row[1] = 10
check(row[1] == 10 && row.to_a[1] == 10, '[]=')
fails('Row index', ::IndexError) { row[9] }
fails('Row index', Names::IndexError) { row[9] = 1 }
fails('negative', ::RangeError) { row[-1] }
check(Names::Row.new.size == 3, 'default constructor argument')

# Lifetime: borrowed results keep their parent alive; shared results own themselves.
parent = Names::Node.new
check(parent.self_.equal?(parent), 'a reference to self comes back as the same object')
node_self = Names::Node.new.self_
GC.start
GC.compact if GC.respond_to?(:compact)
check(node_self.value == 1, 'borrowed self stays valid')
sib = Names::Node.new.sibling
GC.start
check(sib.value == 2, 'shared result')
node = Names::Node.new
items = node.items
items << 4
check(node.items == [1, 2, 3], 'containers are copies')
node.items = [9]
check(node.items == [9], 'container field set')

# Enums: Symbols, unscoped constants, flags as Integers.
check(Names.next_color(:red) == :green && Names.next_color(Names::Color::GREEN) == :blue, 'enum symbols')
check(Names::RED == :red && Names::Color.to_i(:green) == 5 && Names.next_color(5) == :blue, 'unscoped enumerators')
check(Names.all_permissions == 7 && Names::Permission::WRITE == 2, 'flags are Integers')
check(Names.permission_bits(%i[read execute]) == 5 && Names.permission_bits(:write) == 2, 'flags from Symbols')
check(Names.permission_bits(Names::Permission::READ | Names::Permission::WRITE) == 3, 'flags combine with |')
check(SOURCE.include?('READ = 1') && SOURCE.include?('RED = :red'), 'enum constants documented')
fails('unknown enumerator :purple', ::ArgumentError) { Names.next_color(:purple) }

# Conversions.
check(Names.twice(Complex(1, 2)) == Complex(2, 4) && Names.twice(3) == Complex(6, 0), 'complex')
check(Names.range(3) == [0.0, 1.0, 2.0], 'vector result')
check(Names.counts(%w[a b a]) == { 'a' => 2, 'b' => 1 }, 'map result')
check(Names.unique([3, 1, 3]) == Set[1, 3] && Names.unique(Set[2]) == Set[2], 'set result and Set argument')
check(Names.pair == [1, 2.5] && Names.triple == [1, 'two', true], 'pair and tuple')
check(Names.big == 18_446_744_073_709_551_615 && Names.smallest == -(2**63), 'bigint results')
check(Names.echo_u64(2**64 - 1) == 2**64 - 1, 'bigint argument')
fails('outside the range', ::RangeError) { Names.echo_u64(2**64) }
fails('negative integer', ::RangeError) { Names.echo_u64(-1) }
check(Names.echo_i8(-128) == -128, 'int8 minimum')
fails('outside the range', ::RangeError) { Names.echo_i8(128) }
check(Names.maybe(nil).nil? && Names.maybe(4) == 4, 'optional')
check(Names.kind_of_value(1) == 'int' && Names.kind_of_value('x') == 'string', 'variant argument')
check(Names.either(true) == 'text' && Names.either(false) == 3, 'variant result')
check(Names.echo('héllo ✓') == 'héllo ✓' && Names.echo('x').encoding == Encoding::UTF_8, 'UTF-8')
check(Names.echo('caf'.encode('UTF-16LE') + ''.encode('UTF-16LE')) == 'caf', 'other encodings convert to UTF-8')
check(Names.echo(:sym) == 'sym', 'Symbol as String')
check(Names.total([1.0, 2.5]) == 3.5 && Names.doubled([1.0, 2.0]) == [2.0, 4.0], 'policy convert')

# Defaults and keywords.
check(Names.keyword_args(1) == '12c' && Names.keyword_args(1, 3) == '13c', 'defaults')
check(Names.keyword_args(1, d: true) == '12c!' && Names.keyword_args(1, c: 'z', b: 0) == '10z', 'keywords skip defaults')
check(Names.keyword_args(a: 5) == '52c', 'required argument by keyword')
fails('unknown keywords', ::ArgumentError) { Names.keyword_args(1, e: 1) }
check(SOURCE.include?('@overload keyword_args(a, **options)') && SOURCE.include?('@option options [Boolean] :d (false)'), 'keyword docs')

# Callbacks.
check(Names.apply(->(x) { x + 1 }, 1) == 2 && Names.apply(2) { |x| x * x } == 4, 'callbacks')
seen = []
Names.each([1, 2, 3]) { |v| seen << v }
check(seen == [1, 2, 3], 'block as void callback')
check(Names.each([1, 2, 3]) { |v| break v * 10 if v == 2 } == 20, 'break out of a block through C++')
mul = Names.multiplier
check(mul.call(6, 7) == 42 && mul.arity == 2 && mul.to_proc.call(2, 3) == 6, 'returned two-argument function')

# Exceptions.
e = fails('negative', Names::ArgumentError) { Names.fail_(-1) }
check(e.is_a?(::ArgumentError) && e.is_a?(Names::Error) && e.cpp_class == 'std::invalid_argument', 'invalid_argument')
fails('zero', Names::RuntimeError) { Names.fail_(0) }
fails('one', Names::RangeError) { Names.fail_(1) }
e = fails('two', Names::RuntimeError) { Names.fail_(2) }
check(e.cpp_class == 'std::logic_error', 'logic_error cpp_class')
e = fails('missing', Names::KeyError) { Names.lookup(-1) }
check(e.is_a?(::KeyError) && Names.lookup(3) == 3, 'policy exception mapping')
check(Names.fail_(5) == 5, 'fail_ (renamed from Kernel#fail)')

# Policy modules.
check(Names::Stats.mean([1.0, 2.0, 3.0]) == 2.0, 'policy module')
check(!Names.respond_to?(:mean), 'module function only in its module')
check(SOURCE.include?('module Stats'), 'policy module documented')

# C++ and Ruby standard output stay in order through a pipe (both fully buffered there).
script = 'require ARGV[0]; puts "ruby 1"; Names.say("c++ 2\\n"); puts "ruby 3"; Names.say("c++ 4\\n")'
piped = IO.popen([RbConfig.ruby, '-e', script, DOC_FILE], &:read)
check(piped == "ruby 1\nc++ 2\nruby 3\nc++ 4\n", "C++ and Ruby output interleave in order: #{piped.inspect}")

puts "Ruby names passed (#{$checks} checks)"
