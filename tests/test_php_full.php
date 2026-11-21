<?php
// PHP API (--language php) on the library-shaped example shared with the Python, R6, Lua, Common
// Lisp, Julia, C#, VB, Go and Rust backends. Argument: the generated PHP file (loaded, and read
// for the documentation checks). The native library is found through FEATURES_PHP_LIBRARY.
declare(strict_types=1);

require $argv[1];

use Features\{Aim, BindingException, Canvas, Circle, Complex, FeaturesLegacyMode, FeaturesLegacyTag, Fill, IntStack, Kind, Ledger,
    NativeError, NativeFunction, NativeObject, Segment, Shape, Square, Vec2, Visitor};
use function Features\{add, adder, apply, area_of, byte_sum, bytes_of, circles, clamp_int, clamp_real, count_words, defaults,
    describe, features_legacy_version, filled, find, golden, initial, make_shape, min_max, pick, repeat, run_visitor, scale,
    shared_square, sum, total_area, transpose};

$checks = 0;
function check(bool $ok, string $what): void
{
    global $checks;
    ++$checks;
    if (!$ok) {
        throw new RuntimeException("check failed: $what");
    }
}
// The exception `$action` throws, which must be a `$class` whose message contains `$pattern`.
function fails(callable $action, string $pattern, string $class = Throwable::class): Throwable
{
    global $checks;
    ++$checks;
    try {
        $action();
    } catch (Throwable $error) {
        if (!$error instanceof $class || !str_contains($error->getMessage(), $pattern)) {
            throw new RuntimeException("expected a $class containing '$pattern', got " . get_class($error) . ': ' . $error->getMessage(), 0, $error);
        }
        return $error;
    }
    throw new RuntimeException("expected an exception containing '$pattern', got none");
}

// PHP subclasses implement C++ virtual methods.
class Triangle extends Shape
{
    public function __construct(public float $base, public float $height)
    {
        parent::__construct();
    }

    public function area(): float
    {
        return $this->base * $this->height / 2;
    }

    public function name(): string
    {
        return 'triangle';
    }
}

class Incomplete extends Shape
{
}

class Sentinel extends Exception
{
}

class Failing extends Shape
{
    public function area(): float
    {
        throw new Sentinel('from PHP');
    }
}

// parent:: runs the C++ implementation: an override builds on it.
class Loud extends Circle
{
    public function name(): string
    {
        return strtoupper(parent::name());
    }
}

class Louder extends Loud
{
    public function name(): string
    {
        return strtolower(parent::name()) . '!';
    }
}

class UnitShape extends Shape
{
    public function __construct(public string $tag)
    {
        parent::__construct();
    }

    public function area(): float
    {
        return 1.0;
    }
}

class Forgetful extends Shape
{
    public function __construct()
    {
    }

    public function area(): float
    {
        return 2.0;
    }
}

class Summer extends Visitor
{
    public array $seen = [];

    public function visit(float $value): float
    {
        return $value * 10;
    }

    public function finished(array $values): void
    {
        $this->seen = $values;
    }
}

$source = file_get_contents($argv[1]);
// The doc comment right above the first line containing `$declaration`.
$doc = function (string $declaration) use ($source): string {
    $lines = explode("\n", $source);
    foreach ($lines as $at => $line) {
        if (str_contains($line, $declaration) && !str_starts_with(ltrim($line), '*') && !str_starts_with(ltrim($line), '//')) {
            $block = [];
            for ($i = $at - 1; $i >= 0; --$i) {
                $text = ltrim($lines[$i]);
                if (!str_starts_with($text, '*') && !str_starts_with($text, '/**')) {
                    break;
                }
                array_unshift($block, $lines[$i]);
                if (str_starts_with($text, '/**')) {
                    break;
                }
            }
            return implode("\n", $block);
        }
    }
    throw new RuntimeException("not declared: $declaration");
};

// Classes, inheritance and virtual dispatch.
$c1 = new Circle(2);
check($c1->area() === 12.0 && $c1->name() === 'circle' && $c1->twiceArea() === 24.0 && $c1->radius() === 2.0, 'circle methods');
check($c1 instanceof Shape && $c1 instanceof NativeObject, 'class hierarchy');
$error = fails(fn () => new Circle(-1), 'negative radius', \InvalidArgumentException::class);
check($error instanceof NativeError && $error->getKind() === 'invalid-argument', 'exception kind');
$s = new Square();
check($s->side === 1.0 && $s->area() === 1.0, 'default constructor');
$s->side = 3;
check($s->area() === 9.0 && (new Square(2))->area() === 4.0, 'property set and overloaded constructor');
check(isset($s->side) && !isset($s->nothing), 'isset on properties');
fails(fn () => $s->nothing, 'Undefined property', \Error::class);
check(area_of($s) === 9.0 && describe($c1) === 'circle:0', 'functions on classes');
// Enums.
check($c1->kind() === Kind::Circle && $s->kind() === Kind::Square, 'enum results');
check(filled(Fill::Solid) && !filled(Fill::None) && filled(1), 'enum arguments (and their values)');
check(Kind::Custom->value === 7 && Kind::from(1) === Kind::Square, 'backed enum');
check(Shape::sidesUnknown() === -1 && abs(golden() - 1.618) < 1e-12, 'constants');

// Most-derived results, ownership transfer and shared ownership.
$made = make_shape(Kind::Circle, 1.0);
check($made instanceof Circle && $made->radius() === 1.0, 'most-derived result');
check($made->cast(Circle::class) === $made && $made->cast(Square::class) === null, 'cast');
$canvas = new Canvas();
$canvas->add(make_shape(Kind::Square, 2.0));
$canvas->share(shared_square(3.0));
$transferred = new Circle(1);
$canvas->add($transferred);
fails(fn () => $transferred->area(), 'closed', BindingException::class);
check($canvas->size() === 3 && $canvas->total() === 4.0 + 9.0 + 3.0, 'ownership transfer');
$first = $canvas->at(0);
check($first instanceof Square && $first->area() === 4.0, 'borrowed element');
$shared = shared_square(2.0);
$canvas->share($shared);
check($shared->area() === 4.0 && $canvas->total() === 20.0, 'shared ownership');
check(total_area([new Circle(1), $shared]) === 7.0, 'list of objects');

// Overloads: one PHP function per C++ name, resolved in C++.
check(pick(1) === 'int' && pick(1.5) === 'double' && pick(2.0) === 'double' && pick('x') === 'string', 'scalar overloads');
check(pick([1.0, 2.0]) === 'vector' && pick($s) === 'shape', 'container and class overloads');
check(add(1) === 111 && add(1, 2) === 103 && add(1, 2, 3) === 6, 'default arguments');
check(add(1, c: 3) === 14, 'named argument (the PHP default b = 10)');

// Containers.
check(count_words(['a', 'b', 'a']) === ['a' => 2, 'b' => 1], 'map result');
check(min_max([3.0, 1.0, 2.0]) === [1.0, 3.0], 'pair result');
$error = fails(fn () => min_max([]), 'empty input', \OutOfRangeException::class);
check($error->getKind() === 'out-of-range', 'out_of_range kind');
check(find([5, 7], 7) === 1 && find([5, 7], 9) === null, 'optional result');
check(transpose([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]) === [[1.0, 4.0], [2.0, 5.0], [3.0, 6.0]], 'nested lists');
check(byte_sum([1, 2, 250]) === 253 && byte_sum("\x01\x02\xfa") === 253 && bytes_of('AZ') === [65, 90], 'bytes');
$cs = circles(3);
check(count($cs) === 3 && $cs[2]->radius() === 3.0, 'list of class results');
check(repeat('z', 3) === 'zzz' && initial('hello') === 'h', 'chars');
// Non-const reference parameters are by-reference parameters, written back.
$values = [1.0, 2.0];
scale($values, 3.0);
check($values === [3.0, 6.0], 'by-reference argument written back');
fails(fn () => repeat('z', -1), 'negative count');
check(defaults() === 'x*1-1.500000', 'all defaults');
check(defaults(Kind::Circle, 'y', true) === 'y*0!-1.500000', 'some defaults');
check(str_contains($doc('function defaults('), '$weight = 0.5'), 'C++ defaults documented');
fails(fn () => pick(null), 'must be of type', \TypeError::class);
$error = fails(fn () => pick(['a']), 'pick', BindingException::class);
check($error->getKind() === 'binding', 'no overload for the values');

// Callbacks both ways.
check(apply(fn (float $x) => $x * 2, 21.0) === 42.0, 'callback');
check(apply('sqrt', 16.0) === 4.0 && apply(abs(...), -2.0) === 2.0, 'named and first-class callables');
$thrown = new Sentinel('inside callback');
$error = fails(fn () => apply(fn ($x) => throw $thrown, 1.0), 'inside callback', Sentinel::class);
check($error === $thrown, 'the PHP exception crosses C++ unchanged');
$plus = adder(40);
check($plus instanceof NativeFunction && $plus(2) === 42, 'native function as a PHP callable');
check(apply(fn ($x) => (float) adder(1)((int) $x), 3.0) === 4.0, 'native function inside a callback');
check(apply($plus, 2.0) === 42.0, 'native function passed back');
$count = 0;
check(apply(function ($x) use (&$count) { ++$count; return $x; }, 5.0) === 5.0 && $count === 1, 'capturing closure');

// Value type operators, chaining and borrowed references.
$v = new Vec2(1, 2);
$w = $v->add(new Vec2(3, 4));
check($w->x === 4.0 && $w->y === 6.0 && $v->mul(2)->y === 4.0 && $v->neg()->x === -1.0 && $v->sub($w)->x === -3.0, 'operators');
check($v->equals(new Vec2(1, 2)) && !$v->equals($w), 'equality operator');
check($v[1] === 2.0 && $v->at(0) === 1.0 && isset($v[1]) && !isset($v[2]) && $v->inner(new Vec2(1, 1)) === 3.0, 'ArrayAccess and rename');
fails(fn () => $v[2], 'Vec2 index', \OutOfRangeException::class);
fails(function () use ($v) { $v[0] = 1.0; }, 'read-only', \LogicException::class);
$chained = $v->setX(10)->setY(20);
check($v->x === 10.0 && $v->y === 20.0 && $chained === $v, 'chaining returns the same PHP object');
check($v->addAssign($w) === $v && $v->x === 14.0, 'compound assignment');
$v->x = 5;
check($v->x === 5.0, 'property next to the setX method');
$seg = new Segment();
$seg->end = new Vec2(3, 4);
check($seg->length2() === 25.0, 'object property set');
$seg->first()->x = 3;
check($seg->start->x === 3.0 && $seg->length2() === 16.0, 'borrowed reference writes through');
$seg->start->y = 4;
check($seg->length2() === 0.0, 'property object borrowed');
check((new Vec2(1, 2))->clone()->y === 2.0, 'C++ clone');
check(sum([new Vec2(1, 2), new Vec2(3, 4)]) === 10.0, 'list of values');
$held = (new Segment())->first();
check($held->x === 0.0, 'borrowed reference keeps its owner alive');

// Templates, extensions, qualified names and copy types.
$stack = new IntStack();
$stack->push(1);
$stack->push(2);
check($stack->size() === 2 && $stack->items() === [1, 2] && $stack->pop() === 2, 'template instance');
check(clamp_int(42) === 10 && clamp_int(3, 5) === 3 && clamp_real(2.5) === 2.5, 'function templates');
check($c1->label() === '#circle' && $c1->label('@') === '@circle' && Circle::unit()->radius() === 1.0, 'extensions');
check(features_legacy_version() === 2 && (new FeaturesLegacyTag())->id === 7, 'qualified names');
check(FeaturesLegacyMode::Off->value === 1, 'qualified enum');
check((new Circle(2))->diameter() === 4.0, 'extension method');
$ledger = new Ledger();
$copy = $ledger->latest();
$copy->value = 5;
check($ledger->stamp->value === 1, 'copy type');
$aim = new Aim();
$aim->origin->x = 2;
check($aim->target->point->x === 2.0, 'nested borrowed properties');
fails(function () use ($aim) { $aim->target->point = new Vec2(); }, 'read-only', \Error::class);

// Closing and releasing.
$x = new Circle(1);
check($x->area() === 3.0 && !$x->isClosed(), 'before close');
$x->close();
check($x->isClosed(), 'closed');
$error = fails(fn () => $x->area(), 'closed', BindingException::class);
check($error->getKind() === 'binding', 'binding kind');
$x->close();
for ($i = 1; $i <= 200; ++$i) {
    $tmp = make_shape(Kind::Square, (float) $i);
    check($tmp->area() === (float) ($i * $i), 'many objects');
    if ($i % 2 === 0) {
        $tmp->close();
    }
}
fails(fn () => serialize(new Circle(1)), 'cannot be serialized', \LogicException::class);

// PHP subclasses implement virtual methods.
$t = new Triangle(4, 3);
check($t->area() === 6.0 && area_of($t) === 6.0 && $t->twiceArea() === 12.0 && describe($t) === 'triangle:7', 'PHP override called from C++ and from PHP');
check(total_area([$t, new Circle(1), new Square(2)]) === 13.0 && $t->label('@') === '@triangle', 'mixed overrides');
fails(fn () => area_of(new Incomplete()), 'pure virtual');
$sentinel = fails(fn () => area_of(new Failing()), 'from PHP', Sentinel::class);
check($sentinel instanceof Sentinel, "an override's exception crosses C++ unchanged");
fails(fn () => area_of(new Forgetful()), 'parent::__construct()', BindingException::class);
fails(fn () => new Shape(), 'abstract', BindingException::class);
$l = new Loud(2);
check(describe($l) === 'CIRCLE:0' && $l->name() === 'CIRCLE' && $l->area() === 12.0, 'parent:: runs the C++ method');
check(describe(new Louder(1)) === 'circle!:0', 'override of an override');
$canvas = new Canvas();
$u = new UnitShape('kept');
$canvas->add($u);
check($u->area() === 1.0, 'PHP-implemented object owned by C++');
$canvas->share(new Square(2));
unset($u);
check($canvas->total() === 5.0 && $canvas->size() === 2, 'C++ keeps the PHP object alive');
$back = $canvas->at(0);
check($back instanceof UnitShape && $back->tag === 'kept' && $back === $canvas->at(0), 'the same PHP object comes back');
$canvas->clear();
$canvas->share(new UnitShape(''));
check($canvas->total() === 1.0, 'shared PHP-implemented object');
$canvas->clear();
$sm = new Summer();
check(run_visitor($sm, [1.0, 2.0]) === 30.0 && $sm->seen === [1.0, 2.0], 'callback interface');
check($sm->visit(4.0) === 40.0, 'a direct call runs the PHP method');
check(run_visitor(new Visitor(), [1.0, 2.0]) === 3.0, 'native default implementation');

// PHPDoc carries the C++ documentation.
$d = $doc('function add(int $a');
check(str_contains($d, 'Adds three integers, Socha & Dorigo style.'), 'summary');
check(str_contains($d, '@param int $a first term'), 'parameters');
check(str_contains($d, '@return int the sum'), 'returns');
check(str_contains($d, '```') && str_contains($d, 'int total = add(1);'), 'code block');
check(str_contains($d, '@see pick()') && str_contains($d, '@see sum()'), 'see also');
check(str_contains($d, '@throws OutOfRangeException'), 'throws');
check(str_contains($doc('class Circle extends Shape'), 'parent::method()'), 'subclassing documented');
check(str_contains($doc('function pick('), 'Overloads'), 'overloads listed');
check(str_contains($doc('class Vec2 extends'), '@property float $x Horizontal component.'), 'property documentation');

// Policy modules: features::data functions live in the namespace Features\Data.
$unit = Features\Data\unit();
check($unit instanceof Vec2 && $unit->x === 1.0 && $unit->y === 0.0, 'submodule result');
check(Features\Data\scaled(3) === 6 && Features\Data\scaled(2.5) === 6.25, 'submodule overloads');
check(Features\Data\repeat('ab') === 'abab' && repeat('x', 3) === 'xxx', 'submodule names are separate');
check(str_contains($source, 'namespace Features\\Data {'), 'submodule namespace');

echo "PHP API passed ($checks checks)\n";
