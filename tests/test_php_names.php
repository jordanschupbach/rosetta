<?php
// The PHP API for tests/php_full_names.hpp: reserved words, names PHP compares case-insensitively,
// classes named like the runtime's, members named like NativeObject's, incompatible hiding and
// multiple inheritance. Argument: the generated PHP file.
declare(strict_types=1);

require $argv[1];

use Names\{Base, Closure, Complex_, Counted, Cursor, Leaf, List_, Mode, Named, NativeObject_, Object_, Point, Point_, Thing};
use function Names\{as_named, count, counted_of, describe_base, echo_, label_of, mode_value, point_x, point_y, print_, strlen};

$checks = 0;
function check(bool $ok, string $what): void
{
    global $checks;
    ++$checks;
    if (!$ok) {
        throw new RuntimeException("check failed: $what");
    }
}

class Shouting extends Named
{
    public function label(): string
    {
        return strtoupper(parent::label());
    }
}

class Deep extends Leaf
{
    public function describe(): string
    {
        return 'deep ' . parent::describe();
    }
}

$source = file_get_contents($argv[1]);

// Classes named like reserved words, the runtime's classes and PHP classes.
check((new List_())->size() === 3 && (new Object_())->id === 1, 'reserved words as class names');
check((new NativeObject_())->native === 2 && (new Complex_())->im === 2.0, "the runtime's names");
check((new Closure())->run() === 4 && !(new Closure()) instanceof \Closure, 'a class named like a PHP class');
check(point_x(new Point()) === 1 && point_y(new Point_()) === 2, 'class names that differ only in case');

// Reserved words and builtins as function names.
check(print_(1) === 1 && echo_(1) === 2, 'reserved words as functions');
check(count([1, 2, 3]) === 3 && \count([1]) === 1 && strlen('a') === 'a!', 'namespaced functions named like builtins');

// Enum cases.
check(mode_value(Mode::On) === 0 && mode_value(Mode::On2) === 1 && mode_value(Mode::Class2) === 2, 'cases that collide');
check(mode_value(Mode::List) === 3 && mode_value(Mode::Fn) === 4, 'reserved words as cases');

// Methods and parameters.
$cursor = new Cursor();
check($cursor->list() === 1 && $cursor->match(2, 3) === 6, 'reserved words as methods and parameters');
check($cursor->move(4) === 4 && $cursor->position === 4 && $cursor->self(2) === 4, 'parameters named self');
check($cursor->count() === 20 && $cursor->count === 2, 'a method and a property named alike');
check($cursor->close2() && !$cursor->isClosed(), 'a C++ close next to the generated close');
check($cursor->cast2() === 'cast' && $cursor->cast(Cursor::class) === $cursor, 'a C++ cast next to the generated cast');
check($cursor->offsetGet2(5) === 5 && !$cursor instanceof ArrayAccess, 'ArrayAccess names');
check($cursor->getName() === 'camel' && $cursor->getName2(2) === 'snakesnake' && $cursor->getname3() === 'upper', 'names equal ignoring case');
check($cursor->value(1) === 2, 'a parameter named like a generated local');
check(Cursor::create() === 5, 'static method');
$cursor->close();
check($cursor->isClosed(), 'the generated close');

// Multiple inheritance: the primary base by extends, the other through asNamed().
$thing = new Thing();
check($thing instanceof Counted && !$thing instanceof Named, 'the primary base is the PHP parent');
check($thing->counted() === 3 && counted_of($thing) === 3, 'primary base');
check($thing->label() === 'thing' && label_of($thing) === 'thing' && label_of($thing->asNamed()) === 'thing', 'second base');
check($thing->asNamed() instanceof Named && $thing->asNamed()->tag === 't', 'asNamed');
$named = as_named($thing);
check($named instanceof Thing && $named->cast(Named::class) instanceof Named, 'cast across bases');
$shouting = new Shouting();
check(label_of($shouting) === 'NAMED' && $shouting->label() === 'NAMED', 'a subclass of a second base');

// Hiding: compatible signatures override, others are renamed.
$leaf = new Leaf();
check($leaf->describe() === 'leaf' && $leaf->name() === 'leaf' && describe_base($leaf) === 'leaf', 'override and hiding');
check($leaf->scale2(2.0, 3.0) === 6.0 && $leaf->scale(2.0) === 2.0, 'a hiding method with another signature');
check($leaf->size2() === 'big' && $leaf->size() === 1, 'a hiding method with another result');
$deep = new Deep();
check(describe_base($deep) === 'deep leaf' && $deep->describe() === 'deep leaf', 'override three levels deep');

check(str_contains($source, 'class List_ extends NativeObject') && str_contains($source, 'function print_('), 'generated names');
echo "PHP names passed ($checks checks)\n";
