// Names and values that are hard for Dart (tests/dart_full_names.hpp). Argument: the generated
// lib/names.dart. dart:io is imported with a prefix: the C++ class File is File here.
import 'dart:io' as io;

import 'package:names/names.dart';

var checks = 0;
late final String source;

void check(bool ok, String what) {
  checks++;
  if (!ok) {
    io.stderr.writeln('check failed: $what');
    io.exit(1);
  }
}

void fails(void Function() body, String pattern) {
  try {
    body();
  } catch (error) {
    checks++;
    if (!'$error'.contains(pattern)) {
      io.stderr.writeln('expected an error containing $pattern, got: $error');
      io.exit(1);
    }
    return;
  }
  io.stderr.writeln('expected an error containing $pattern, got none');
  io.exit(1);
}

class Handle extends Resource {
  @override
  String close2() => 'dart ${super.close2()}';
}

class Gadget extends Thing {
  @override
  String label() => 'gadget';
}

class Leafy extends Leaf {
  @override
  String describe() => 'leafy';
}

void main(List<String> arguments) {
  source = io.File(arguments.first).readAsStringSync();

  // Types named like dart:core's and the runtime's take an underscore.
  check(String_().n == 1 && List_().n == 2 && Object_().n == 3 && Duration_().n == 4, 'core type names');
  check(NativeObject_().native == 5 && Ref_().ref == 6 && Complex_().re == 7, 'runtime type names');
  check(stringN(String_()) == 1 && String_() is NativeObject, 'renamed class as argument');
  check(Data().value == 8 && Data_.twice(4) == 8, 'class and policy module named alike');

  // Keywords and dart:core functions.
  check(is_(3) == 3 && switch_('a') == 'aa' && print_(2) == 20 && identical_(1, 1) == 1, 'keyword and core function names');
  check(newValue(1, 2) == 3 && totalCount(4) == 4 && totalCount2(1, 2) == 3, 'parameter keywords and camelCase collisions');
  check(source.contains('int newValue(int in_, int var_)'), 'keyword parameters');

  // Enumerators.
  check(modeValue(Mode.in_) == 0 && modeValue(Mode.value2) == 1 && modeValue(Mode.name2) == 2, 'enumerator names');
  check(modeValue(Mode.values2) == 3 && modeValue(Mode.default_) == 4 && modeValue(Mode.is_) == 5 && modeValue(Mode.off) == 6, 'more enumerators');
  check(Mode.name2.name == 'name2' && Mode.values.length == 7 && Mode.in_.value == 0, 'enum members');

  // Members.
  final cursor = Cursor();
  check(cursor.count() == 20 && cursor.count2 == 2, 'field meets method');
  check(cursor.close2() && !cursor.isClosed && cursor.cast2() == 'cast', 'NativeObject member names');
  check(cursor.hashCode_() == 11 && cursor.runtimeType_() == 'rt' && cursor.isClosed2() == 12, 'Object member names');
  check(cursor.getName() == 'camel' && cursor.getName2(2) == 'snakesnake', 'camelCase meeting');
  check(cursor.int_() == 13 && cursor.double_() == 1.5, 'core type member names');
  cursor.set(4);
  check(cursor.get() == 14 && cursor.position == 4 && cursor.move(3) == 7 && Cursor.create() == 5, 'get, set and static');
  check(cursor.cast<Cursor>() == cursor, 'the generated cast');
  cursor.close();
  check(cursor.isClosed, 'the generated close');

  // Merged overloads.
  check(kindOf(true) == 'bool' && kindOf(5) == 'int' && kindOf('s') == 'string' && kindOf(1, 2) == 'two', 'merged overloads');
  check(twice(3) == 6 && twice('ab') == 'abab', 'merged result');
  fails(() => kindOf(1.5), 'kind_of');
  final made = make(true), base = make(false), leaf = make(1);
  check(made is Leaf && base is! Leaf && leaf is Leaf && base!.describe() == 'base', 'common base class result');
  check(source.contains('Base? make(Object? leaf)'), 'common base class result typed');

  // Merged and renamed methods.
  final l = Leaf();
  check(l.scale(2) == 2 && l.scale(2, 3) == 6 && Base().scale(5) == 5, 'merged with the base method');
  check(l.size() == 1 && l.size2() == 'big', 'another result renames');
  check(describeBase(l) == 'leaf' && describeBase(Leafy()) == 'leafy', 'override');

  // A renamed virtual method.
  check(closeResource(Resource()) == 'C++ close' && closeResource(File()) == 'file close', 'renamed virtual');
  final h = Handle();
  check(closeResource(h) == 'dart C++ close' && !h.isClosed, 'Dart override of a renamed virtual');

  // Multiple inheritance.
  final thing = Thing();
  check(thing.counted() == 3 && countedOf(thing) == 3 && thing.label() == 'thing', 'superclass members');
  check(thing.tag == 't' && labelOf(thing.asNamed) == 'thing', 'further base members and asBase');
  thing.tag = 'u';
  check(thing.asNamed.tag == 'u', 'further base field');
  final g = Gadget();
  check(labelOf(g.asNamed) == 'gadget' && countedOf(g) == 3, 'override through a further base');

  // Operators.
  final a = Money(150), b = Money(25);
  check((a + b).cents == 175 && (a * 2).cents == 300 && (a * b).cents == 3750, 'operators and merged operator');
  check(b < a && !(a < b) && (-a).cents == -150, 'comparison and unary');
  check(Money().not() && !a.not() && a.equals(Money(150)), 'named operators');
  check('$a' == '1.50' && '${Money(5)}' == '0.05', 'toString');
  check(a(5) == 155 && a.call(1) == 151, 'call');
  a.close();
  check('$a'.contains('closed'), 'toString when closed');
  final padded = Padded(7);
  check('$padded' == '007' && padded.toString() == '007' && padded.toString(5) == '00007', 'toString with default arguments');
  padded.close();

  // Values.
  check(biggest() == -1 && echoU64(-1) == -1 && echoU64(1 << 62) == 1 << 62, 'uint64 bits');
  check(echoI64(9223372036854775807) == 9223372036854775807 && echoI64(-9223372036854775807 - 1) == -9223372036854775807 - 1, 'int64 range');
  check(echoU8(255) == 255, 'uint8');
  fails(() => echoU8(256), 'outside C++ type range');
  final count = Ref(1), text = Ref('x');
  bump(count, text);
  check(count.value == 2 && text.value == 'x!', 'Ref arguments');
  check(callTwice((x) => x * 3, 2) == 18, 'function argument');
  check(callMaybe(2) == 2 && callMaybe(2, (x) => x * 5) == 10, 'optional callback');
  check(source.contains('[int Function(int)? f]'), 'nullable function type');
  final r = ranked(['a', 'b']);
  check(r.length == 2 && r[1].$1 == 'b' && r[1].$2 == 2, 'list of records');
  final (one, two, three) = triple();
  check(one == 1 && two == 'two' && three == 3.5, 'tuple record');

  print('Dart names passed ($checks checks)');
}
