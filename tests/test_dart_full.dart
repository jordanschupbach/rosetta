// Dart API (--language dart) on the library-shaped example shared with the Python, Lua, Common Lisp,
// Julia, C#, VB, Go, Rust, PHP, Perl, Swift and Pascal backends. Argument: the generated
// lib/features.dart, for the documentation checks.
import 'dart:io';
import 'dart:typed_data';

import 'package:features/features.dart';

var checks = 0;
late final List<String> source;

void check(bool ok, String what) {
  checks++;
  if (!ok) {
    stderr.writeln('check failed: $what');
    exit(1);
  }
}

// Runs [body], which must throw; returns the error, which must mention [pattern].
Object fails(void Function() body, String pattern) {
  try {
    body();
  } catch (error) {
    checks++;
    if (!'$error'.contains(pattern)) {
      stderr.writeln('expected an error containing $pattern, got: ${error.runtimeType}: $error');
      exit(1);
    }
    return error;
  }
  stderr.writeln('expected an error containing $pattern, got none');
  exit(1);
}

NativeErrorKind? kindOf(Object error) => error is NativeError ? error.kind : null;

// The doc comment right above the first line containing [declaration].
String doc(String declaration) {
  final at = source.indexWhere((line) => line.contains(declaration) && !line.trimLeft().startsWith('///'));
  if (at < 0) {
    stderr.writeln('not declared: $declaration');
    exit(1);
  }
  final lines = <String>[];
  for (var i = at - 1; i >= 0; i--) {
    final line = source[i].trim();
    if (line.startsWith('@')) continue;
    if (!line.startsWith('///')) break;
    lines.insert(0, line.length > 3 ? line.substring(4) : '');
  }
  return lines.join('\n');
}

class Sentinel implements Exception {
  @override
  String toString() => 'from Dart';
}

// ---------------------------------------------------------------- Dart subclasses
class Triangle extends Shape {
  Triangle(this.base, this.height);
  final double base, height;
  @override
  double area() => base * height / 2;
  @override
  String name() => 'triangle';
}

class Incomplete extends Shape {}

class Failing extends Shape {
  @override
  double area() => throw Sentinel();
}

class Loud extends Circle {
  Loud(super.radius);
  @override
  String name() => super.name().toUpperCase();
}

class Noisy extends Circle {
  Noisy(super.radius);
  @override
  String name() => '${super.name().toLowerCase()}!';
}

class Louder extends Noisy {
  Louder(super.radius);
}

class UnitShape extends Shape {
  UnitShape(this.tag);
  final String tag;
  @override
  double area() => 1;
}

class Summer extends Visitor {
  List<double> seen = [];
  @override
  double visit(double value) => value * 10;
  @override
  void finished(List<double> arg1) => seen = List.of(arg1);
}

// ---------------------------------------------------------------- checks
void testClasses() {
  final c1 = Circle(2);
  check(c1.area() == 12 && c1.name() == 'circle' && c1.twiceArea() == 24 && c1.radius() == 2, 'circle methods');
  check('$c1' == 'Circle', 'toString names the class');
  final Shape shape = c1;
  check(shape is Circle, 'class hierarchy');
  final error = fails(() => Circle(-1), 'negative radius');
  check(kindOf(error) == NativeErrorKind.invalidArgument, 'exception kind');
  final s = Square();
  check(s.side == 1 && s.area() == 1, 'default constructor');
  s.side = 3;
  check(s.area() == 9, 'property set');
  check(Square(2).area() == 4, 'constructor with an argument');
  check(areaOf(s) == 9 && describe(c1) == 'circle:0', 'functions on classes');
  // Enums.
  check(c1.kind() == Kind.circle && s.kind() == Kind.square, 'enum results');
  check(filled(Fill.solid) && !filled(Fill.none), 'enum arguments');
  check(Kind.custom.name == 'custom' && Kind(42).name == '42' && Kind.custom.value == 7 && Kind.values.length == 3, 'enum values');
  check(Shape.sidesUnknown == -1 && (golden - 1.618).abs() < 1e-12, 'constants');

  // Most-derived results, ownership transfer and shared ownership.
  final made = makeShape(Kind.circle, 1);
  check(made is Circle && made.radius() == 1, 'most-derived result');
  check(made.cast<Square>() == null, 'cast to another class fails');
  check(identical(made.cast<Shape>(), made), 'cast to a base is the object');
  final canvas = Canvas();
  canvas.add(makeShape(Kind.square, 2));
  canvas.share(sharedSquare(3));
  final transferred = Circle(1);
  canvas.add(transferred);
  fails(() => transferred.area(), 'closed');
  check(canvas.size() == 3 && canvas.total() == 4 + 9 + 3, 'ownership transfer');
  check(canvas.at(0) is Square && canvas.at(0).area() == 4, 'borrowed element');
  final shared = sharedSquare(2);
  canvas.share(shared);
  check(shared.area() == 4 && canvas.total() == 20, 'shared ownership');
  check(totalArea([Circle(1), shared]) == 7, 'list of objects');
  shared.close();
  check(canvas.total() == 20, 'C++ keeps a shared object after Dart closes it');
}

double twice(double a) => a * 2;

void testValues() {
  // Overloads: one Dart function, resolved again in C++.
  check(pick(1) == 'int' && pick(1.5) == 'double' && pick(2.0) == 'double' && pick('x') == 'string', 'scalar overloads');
  check(pick([1.0, 2.0]) == 'vector' && pick(Circle(1)) == 'shape', 'container and class overloads');
  check(add(1) == 111 && add(1, 2) == 103 && add(1, 2, 3) == 6, 'default arguments');
  fails(() => add(1, null, 3), 'b');

  // Containers.
  final words = countWords(['a', 'b', 'a']);
  check(words.length == 2 && words['a'] == 2 && words['b'] == 1, 'map result');
  final (low, high) = minMax([3, 1, 2]);
  check(low == 1 && high == 3, 'record result');
  final error = fails(() => minMax([]), 'empty input');
  check(kindOf(error) == NativeErrorKind.outOfRange, 'out_of_range kind');
  check(find([5, 7], 7) == 1 && find([5, 7], 9) == null, 'optional result');
  final tr = transpose([
    [1, 2, 3],
    [4, 5, 6]
  ]);
  check(tr.length == 3 && tr[0][0] == 1 && tr[0][1] == 4 && tr[2][1] == 6, 'nested lists');
  final bytes = bytesOf('AZ');
  check(byteSum(Uint8List.fromList([1, 2, 250])) == 253 && bytes.length == 2 && bytes[0] == 65 && bytes[1] == 90, 'bytes');
  final cs = circles(3);
  check(cs.length == 3 && cs[2].radius() == 3, 'list of class results');
  check(repeat('z', 3) == 'zzz' && initial('hello') == 'h', 'characters');
  fails(() => repeat('zz', 1), 'one-character');
  // Non-const reference parameters are Ref arguments, written back.
  final values = Ref([1.0, 2.0]);
  scale(values, 3);
  check(values.value.length == 2 && values.value[0] == 3 && values.value[1] == 6, 'Ref argument written back');
  fails(() => repeat('z', -1), 'negative count');
  check(defaults() == 'x*1-1.500000', 'all defaults');
  check(defaults(Kind.circle, 'y', true) == 'y*0!-1.500000', 'some defaults');
  check(doc('String defaults(').contains('`weight = 0.5`'), 'defaults named');

  // Functions both ways.
  check(apply(twice, 21) == 42, 'function');
  check(apply((x) => x + 0.5, 1) == 1.5, 'closure');
  final caught = fails(() => apply((_) => throw Sentinel(), 1), 'from Dart');
  check(caught is Sentinel, 'a closure\'s exception comes back unchanged');
  final plus = adder(40);
  check(plus(2) == 42, 'C++ std::function as a Dart function');
  check(apply((x) => adder(1)(x.toInt()).toDouble(), 3) == 4, 'C++ function inside a closure');
  var calls = 0;
  check(apply((x) {
            calls++;
            return x;
          }, 5) ==
          5 &&
      calls == 1,
      'closure state');
}

void testObjects() {
  // Value type operators, chaining and borrowed references.
  final v = Vec2(1, 2), w = Vec2(3, 4);
  final r = v + w;
  check(r.x == 4 && r.y == 6, 'operator');
  check((v * 2).y == 4 && (-v).x == -1 && (v - w).x == -2, 'operators');
  check(v.equals(Vec2(1, 2)) && !v.equals(w), 'equality operator');
  check(v[1] == 2 && v.at(1) == 2, 'index operator and at');
  check(v.inner(Vec2(1, 1)) == 3, 'rename');
  fails(() => v[2], 'Vec2 index');
  final chained = v.setX(10).setY(20);
  check(v.x == 10 && v.y == 20 && identical(chained, v), 'chaining returns the same Dart object');
  check(identical(v.addAssign(w), v) && v.x == 13, 'compound assignment');
  v.x = 5;
  check(v.x == 5 && v.clone().y == 24, 'property setter and clone');
  check(sum([v, w]) == 5 + 24 + 3 + 4, 'list of values');

  final seg = Segment();
  seg.end = Vec2(3, 4);
  check(seg.length2() == 25, 'object property set');
  final start = seg.first();
  start.x = 3;
  check(seg.start.x == 3 && seg.length2() == 16, 'borrowed reference writes through');
  seg.start.y = 4;
  check(seg.length2() == 0, 'object property borrowed');

  // Templates, extensions, qualified names and copy types.
  final stack = IntStack()
    ..push(1)
    ..push(2);
  final items = stack.items();
  check(stack.size() == 2 && items.length == 2 && items[1] == 2 && stack.pop() == 2, 'template instance');
  check(clampInt(42) == 10 && clampInt(3, 5) == 3 && clampReal(2.5) == 2.5, 'function templates');
  final x = Circle(2);
  check(x.label() == '#circle' && x.label('@') == '@circle' && x.diameter() == 4, 'extensions');
  check(Circle.unit().radius() == 1, 'static extension');
  check(featuresLegacyVersion() == 2 && FeaturesLegacyTag().id == 7, 'qualified names');
  check(FeaturesLegacyMode.off.name == 'off', 'qualified enum');
  final ledger = Ledger();
  final copy = ledger.latest();
  copy.value = 5;
  check(ledger.stamp.value == 1, 'copy type');
  final aim = Aim();
  aim.origin.x = 2;
  check(aim.target.point.x == 2, 'nested borrowed properties');
  check(source.any((line) => line.contains('Vec2 get point =>')) && !source.any((line) => line.contains('set point(')), 'read-only property');

  // Errors, closing.
  final y = Circle(1);
  check(y.area() == 3 && !y.isClosed, 'before close');
  y.close();
  check(y.isClosed && '$y' == 'Circle (closed)', 'closed');
  final error = fails(() => y.area(), 'closed');
  check(kindOf(error) == NativeErrorKind.binding, 'binding error kind');
  y.close();
  for (var i = 1; i <= 200; i++) {
    final t = makeShape(Kind.square, i.toDouble());
    check(t.area() == i * i, 'many objects');
    if (i % 2 == 0) t.close();
  }
}

void testSubclasses() {
  final t = Triangle(4, 3);
  check(t.area() == 6 && areaOf(t) == 6 && t.twiceArea() == 12 && describe(t) == 'triangle:7', 'override called from C++ and from Dart');
  check(totalArea([t, Circle(1), Square(2)]) == 13 && t.label('@') == '@triangle', 'mixed overrides');
  fails(() => areaOf(Incomplete()), 'pure virtual');
  final caught = fails(() => areaOf(Failing()), 'from Dart');
  check(caught is Sentinel, 'an override\'s exception comes back unchanged');
  final error = fails(() => Shape(), 'abstract');
  check(kindOf(error) == NativeErrorKind.binding, 'constructing an abstract class');
  final l = Loud(2);
  check(describe(l) == 'CIRCLE:0' && l.name() == 'CIRCLE' && l.area() == 12, 'super runs the C++ method');
  check(describe(Louder(1)) == 'circle!:0', 'inherited override');
  final canvas = Canvas();
  var u = UnitShape('kept');
  canvas.add(u);
  check(u.area() == 1, 'Dart object owned by C++');
  canvas.share(Square(2));
  check(canvas.total() == 5 && canvas.size() == 2, 'C++ calls the Dart object it owns');
  final back = canvas.at(0);
  check(identical(back, u) && (back as UnitShape).tag == 'kept', 'the same Dart object comes back');
  canvas.clear();
  u = UnitShape('');
  canvas.share(u);
  check(canvas.total() == 1, 'shared Dart object');
  final sm = Summer();
  check(runVisitor(sm, [1, 2]) == 30, 'callback interface');
  check(sm.seen.length == 2 && sm.seen[1] == 2, 'subclass state');
  check(sm.visit(4) == 40, 'a direct call runs the override');
  check(runVisitor(Visitor(), [1, 2]) == 3, 'C++ default implementation');
}

void testDocs() {
  final d = doc('int add(int a, [int? b, int? c])');
  check(d.contains('Adds three integers, Socha & Dorigo style.'), 'summary');
  check(d.contains('- [a]: first term'), 'parameters');
  check(d.contains('Returns the sum'), 'returns');
  check(d.contains('## Example') && d.contains('```') && d.contains('int total = add(1);'), 'heading and code block');
  check(d.contains('See also [pick]'), 'see also');
  check(d.contains('[NativeError] of kind [NativeErrorKind.outOfRange] (C++ `std::out_of_range`)'), 'errors');
  check(d.contains('`b = 10`, `c = 100`'), 'omitted defaults');
  check(doc('Circle(double radius)').contains('[NativeErrorKind.invalidArgument]'), 'constructor errors');
  check(doc('double get x =>').contains('Horizontal component.'), 'property documentation');
  check(doc('class Shape extends NativeObject').contains('Abstract in C++: subclass it in Dart'), 'class remarks');
  check(doc('String pick(Object? arg1)').contains('`pick(int arg1) → String`'), 'overloads listed');

  // Policy modules: features::data functions are static methods of Data.
  final u = Data.unit();
  check(u.x == 1 && u.y == 0, 'submodule result');
  check(Data.scaled(3) == 6 && Data.scaled(2.5) == 6.25, 'submodule overloads');
  check(Data.repeat('ab') == 'abab' && repeat('x', 3) == 'xxx', 'submodule names are separate');
}

void main(List<String> arguments) {
  source = File(arguments.first).readAsLinesSync();
  testClasses();
  testValues();
  testObjects();
  testSubclasses();
  testDocs();
  print('Dart API passed ($checks checks)');
}
