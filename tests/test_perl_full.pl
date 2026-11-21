#!/usr/bin/env perl
# Perl API (--language perl) on the library-shaped example shared with the Python, R6, Lua, Common
# Lisp, Julia, C#, VB, Go, Rust and PHP backends. Argument: the generated module (loaded, and read
# for the documentation checks). The native library is found through FEATURES_PERL_LIBRARY.
use strict;
use warnings;
use Scalar::Util qw(refaddr);

# Loaded at compile time, as `use Features qw(:all)` would: constants are subs.
my $file;
BEGIN {
    $file = shift @ARGV or die "usage: $0 features.pm\n";
    require $file;
    Features->import(qw(:all));
    Features::Data->import(qw(unit scaled));
}

my $checks = 0;
sub check {
    my ($ok, $what) = @_;
    ++$checks;
    die "check failed: $what\n" unless $ok;
}
# The error $action dies with, which must be a $class (or a string, without one) matching $pattern.
sub fails {
    my ($action, $pattern, $class) = @_;
    ++$checks;
    my $ok = eval { $action->(); 1 };
    die "expected an error matching '$pattern', got none\n" if $ok;
    my $error = $@;
    die "expected a $class matching '$pattern', got: $error" if defined $class && !(ref $error && $error->isa($class));
    die "expected an error matching '$pattern', got: $error" unless "$error" =~ $pattern;
    return $error;
}
sub near { abs($_[0] - $_[1]) < 1e-12 }
sub same_list {
    my ($got, $want) = @_;
    return 0 unless ref $got eq 'ARRAY' && @$got == @$want;
    for my $i (0 .. $#$want) {
        if (ref $want->[$i]) { return 0 unless same_list($got->[$i], $want->[$i]) }
        else { return 0 unless defined $got->[$i] && $got->[$i] == $want->[$i] }
    }
    return 1;
}

# Perl subclasses implement C++ virtual methods.
package Triangle {
    our @ISA = ('Features::Shape');
    sub new {
        my ($class, $base, $height) = @_;
        my $self = $class->SUPER::new();
        @$self{qw(base height)} = ($base, $height);
        return $self;
    }
    sub area { my $self = shift; return $self->{base} * $self->{height} / 2 }
    sub name { 'triangle' }
}
package Incomplete { our @ISA = ('Features::Shape') }
package Sentinel { sub new { bless { message => $_[1] }, $_[0] } }
package Failing {
    our @ISA = ('Features::Shape');
    sub area { die Sentinel->new('from Perl') }
}
# SUPER:: runs the C++ implementation: an override builds on it.
package Loud {
    our @ISA = ('Features::Circle');
    sub name { my $self = shift; return uc $self->SUPER::name() }
}
package Louder {
    our @ISA = ('Loud');
    sub name { my $self = shift; return lc($self->SUPER::name()) . '!' }
}
package UnitShape {
    our @ISA = ('Features::Shape');
    sub new { my ($class, $tag) = @_; my $self = $class->SUPER::new(); $self->{tag} = $tag; return $self }
    sub area { 1.0 }
}
package Forgetful {
    our @ISA = ('Features::Shape');
    sub new { return bless {}, shift }
    sub area { 2.0 }
}
package Summer {
    our @ISA = ('Features::Visitor');
    sub visit { my ($self, $value) = @_; return $value * 10 }
    sub finished { my ($self, $values) = @_; $self->{seen} = $values; return }
}
package main;

my $source = do { local $/; open my $in, '<', $file or die "$file: $!"; <$in> };
# The POD section of the sub $name ("=head2 NAME" up to "=cut"), the first one after $after.
sub pod_of {
    my ($name, $after) = @_;
    my $from = defined $after ? index($source, $after) : 0;
    die "no $after\n" if $from < 0;
    my $at = index($source, "\n=head2 $name\n", $from);
    die "no POD for $name\n" if $at < 0;
    return substr($source, $at, index($source, "\n=cut\n", $at) - $at);
}

# Classes, inheritance and virtual dispatch.
my $c1 = Features::Circle->new(2);
check($c1->area == 12 && $c1->name eq 'circle' && $c1->twice_area == 24 && $c1->radius == 2, 'circle methods');
check($c1->isa('Features::Shape') && $c1->isa('Features::NativeObject'), 'class hierarchy');
my $error = fails(sub { Features::Circle->new(-1) }, qr/negative radius/, 'Features::Error::InvalidArgument');
check($error->isa('Features::Error') && $error->kind eq 'invalid-argument' && $error->message =~ /negative radius/, 'error object');
check($error->file eq __FILE__ && "$error" =~ /negative radius at \Q${\ __FILE__}\E line \d+\.$/, 'error location is the caller');
my $s = Features::Square->new;
check($s->side == 1 && $s->area == 1, 'default constructor');
$s->side(3);
check($s->area == 9 && Features::Square->new(2)->area == 4, 'accessor set and overloaded constructor');
check(area_of($s) == 9 && describe($c1) eq 'circle:0', 'functions on classes');
# Enums are constants of their values.
check($c1->kind == Features::Kind::CIRCLE && $s->kind == Features::Kind::SQUARE, 'enum results');
check(filled(Features::Fill::SOLID) && !filled(Features::Fill::NONE) && filled(1), 'enum arguments');
check(Features::Kind::CUSTOM == 7 && Features::Kind->SQUARE == 1, 'enum constants');
check(Features::Shape->SIDES_UNKNOWN == -1 && near(GOLDEN(), 1.618), 'constants');

# Most-derived results, ownership transfer and shared ownership.
my $made = make_shape(Features::Kind::CIRCLE, 1.0);
check(ref $made eq 'Features::Circle' && $made->radius == 1, 'most-derived result');
check($made->cast('Features::Circle') == $made && !defined $made->cast('Features::Square'), 'cast');
check(refaddr($made->cast('Features::Shape')) == refaddr($made), 'cast to a base is the object');
my $canvas = Features::Canvas->new;
$canvas->add(make_shape(Features::Kind::SQUARE, 2.0));
$canvas->share(shared_square(3.0));
my $transferred = Features::Circle->new(1);
$canvas->add($transferred);
fails(sub { $transferred->area }, qr/closed/, 'Features::Error::Binding');
check($canvas->size == 3 && $canvas->total == 4 + 9 + 3, 'ownership transfer');
my $first = $canvas->at(0);
check(ref $first eq 'Features::Square' && $first->area == 4, 'borrowed element');
my $shared = shared_square(2.0);
$canvas->share($shared);
check($shared->area == 4 && $canvas->total == 20, 'shared ownership');
check(total_area([Features::Circle->new(1), $shared]) == 7, 'list of objects');

# Overloads: one Perl function per C++ name, resolved in C++.
check(pick(1) eq 'int' && pick(1.5) eq 'double' && pick(2.0) eq 'double' && pick('x') eq 'string', 'scalar overloads');
check(pick([1.0, 2.0]) eq 'vector' && pick($s) eq 'shape', 'container and class overloads');
check(pick('7') eq 'string' && pick(3 * 2) eq 'int', 'strings stay strings, arithmetic results numbers');
my $interpolated = 5;
my $text = "n=$interpolated";
check(pick($interpolated) eq 'int' && $text eq 'n=5', 'a number stays a number after interpolation');
check(add(1) == 111 && add(1, 2) == 103 && add(1, 2, 3) == 6, 'default arguments');
check(add('1', '2') == 103 && repeat('z', '3') eq 'zzz', 'numeric strings as numbers');
check(describe($c1) eq 'circle:0' && Features::Data::repeat(12) eq '1212', 'numbers as strings');

# Containers.
my $words = count_words([qw(a b a)]);
check(ref $words eq 'HASH' && $words->{a} == 2 && $words->{b} == 1 && keys %$words == 2, 'map result');
check(same_list(min_max([3.0, 1.0, 2.0]), [1, 3]), 'pair result');
$error = fails(sub { min_max([]) }, qr/empty input/, 'Features::Error::OutOfRange');
check($error->kind eq 'out-of-range', 'out_of_range kind');
check(find([5, 7], 7) == 1 && !defined find([5, 7], 9), 'optional result');
check(same_list(transpose([[1, 2, 3], [4, 5, 6]]), [[1, 4], [2, 5], [3, 6]]), 'nested lists');
check(byte_sum([1, 2, 250]) == 253 && byte_sum("\x01\x02\xfa") == 253 && same_list(bytes_of('AZ'), [65, 90]), 'bytes');
my $cs = circles(3);
check(@$cs == 3 && $cs->[2]->radius == 3, 'list of class results');
check(repeat('z', 3) eq 'zzz' && initial('hello') eq 'h', 'chars');
check(Features::Data::repeat("\x{e9}t") eq "\x{e9}t\x{e9}t" && length(Features::Data::repeat("\x{263a}")) == 2, 'UTF-8 strings');
# Non-const reference parameters are written back.
my @values = (1.0, 2.0);
scale(\@values, 3.0);
check(same_list(\@values, [3, 6]), 'array reference updated in place');
my $list = [1.0, 2.0];
scale($list, 2.0);
check(same_list($list, [2, 4]), 'array reference variable');
my $ref = [5.0];
scale(\$ref, 2.0);
check(same_list($ref, [10]), 'reference to a variable');
fails(sub { repeat('z', -1) }, qr/negative count/);
check(defaults() eq 'x*1-1.500000', 'all defaults');
check(defaults(Features::Kind::CIRCLE, 'y', 1) eq 'y*0!-1.500000', 'some defaults (1 as true)');
check(pod_of('defaults') =~ /\$weight = 0\.5/, 'C++ defaults documented');
$error = fails(sub { pick(['a']) }, qr/pick/, 'Features::Error::Binding');
check($error->kind eq 'binding', 'no overload for the values');
fails(sub { pick({}) }, qr/pick/, 'Features::Error::Binding');
fails(sub { pick(\*STDOUT) }, qr/cannot pass a GLOB reference/, 'Features::Error::Binding');

# Callbacks both ways.
check(apply(sub { $_[0] * 2 }, 21.0) == 42, 'callback');
check(apply(\&CORE::sqrt, 16.0) == 4, 'named sub');
my $thrown = Sentinel->new('inside callback');
$error = fails(sub { apply(sub { die $thrown }, 1.0) }, qr/Sentinel/, 'Sentinel');
check($error == $thrown, 'the Perl error crosses C++ unchanged');
$error = fails(sub { apply(sub { die "plain\n" }, 1.0) }, qr/^plain$/);
check(!ref $error, 'a string error crosses unchanged');
my $plus = adder(40);
check(ref $plus eq 'CODE' && $plus->(2) == 42, 'native function as a code reference');
check(apply(sub { adder(1)->($_[0]) }, 3.0) == 4, 'native function inside a callback');
check(apply($plus, 2.0) == 42, 'native function passed back');
my $count = 0;
check(apply(sub { ++$count; $_[0] }, 5.0) == 5 && $count == 1, 'capturing closure');

# Value type operators, chaining and borrowed references.
my $v = Features::Vec2->new(1, 2);
my $w = $v->add(Features::Vec2->new(3, 4));
check($w->x == 4 && $w->y == 6 && $v->multiply(2)->y == 4 && $v->neg->x == -1 && $v->subtract($w)->x == -3, 'operator methods');
my $sum = $v + $w;
check($sum->x == 5 && ($v * 2)->y == 4 && (-$v)->x == -1 && ($v - $w)->x == -3, 'overloaded operators');
check($v == Features::Vec2->new(1, 2) && !($v == $w), 'equality operator');
fails(sub { my $x = 2 * $v }, qr/no C\+\+ operator\* takes Features::Vec2 on the right/, 'Features::Error::Binding');
check($v->at(1) == 2 && $v->at(0) == 1 && $v->inner(Features::Vec2->new(1, 1)) == 3, 'operator[] and rename');
fails(sub { $v->at(2) }, qr/Vec2 index/, 'Features::Error::OutOfRange');
my $chained = $v->set_x(10)->set_y(20);
check($v->x == 10 && $v->y == 20 && refaddr($chained) == refaddr($v), 'chaining returns the same Perl object');
my $before = refaddr($v);
$v += $w;
check($v->x == 14 && refaddr($v) == $before, 'compound assignment');
$v->x(5);
check($v->x == 5, 'accessor next to the set_x method');
my $seg = Features::Segment->new;
$seg->end(Features::Vec2->new(3, 4));
check($seg->length2 == 25, 'object accessor set');
$seg->first->x(3);
check($seg->start->x == 3 && $seg->length2 == 16, 'borrowed reference writes through');
$seg->start->y(4);
check($seg->length2 == 0, 'accessor object borrowed');
check(Features::Vec2->new(1, 2)->clone->y == 2, 'C++ clone');
check(sum([Features::Vec2->new(1, 2), Features::Vec2->new(3, 4)]) == 10, 'list of values');
my $held = Features::Segment->new->first;
check($held->x == 0, 'borrowed reference keeps its owner alive');

# Templates, extensions, qualified names and copy types.
my $stack = Features::IntStack->new;
$stack->push(1);
$stack->push(2);
check($stack->size == 2 && same_list($stack->items, [1, 2]) && $stack->pop == 2, 'template instance');
check(clamp_int(42) == 10 && clamp_int(3, 5) == 3 && clamp_real(2.5) == 2.5, 'function templates');
check($c1->label eq '#circle' && $c1->label('@') eq '@circle' && Features::Circle->unit->radius == 1, 'extensions');
check(features_legacy_version() == 2 && Features::FeaturesLegacyTag->new->id == 7, 'qualified names');
check(Features::FeaturesLegacyMode::OFF == 1, 'qualified enum');
check(Features::Circle->new(2)->diameter == 4, 'extension method');
my $ledger = Features::Ledger->new;
my $copy = $ledger->latest;
$copy->value(5);
check($ledger->stamp->value == 1, 'copy type');
my $aim = Features::Aim->new;
$aim->origin->x(2);
check($aim->target->point->x == 2, 'nested borrowed accessors');
fails(sub { $aim->target->point(Features::Vec2->new) }, qr/read-only/, 'Features::Error::Binding');

# Closing and releasing.
my $x = Features::Circle->new(1);
check($x->area == 3 && !$x->is_closed, 'before close');
$x->close;
check($x->is_closed, 'closed');
$error = fails(sub { $x->area }, qr/closed/, 'Features::Error::Binding');
check($error->kind eq 'binding', 'binding kind');
$x->close;
for my $i (1 .. 200) {
    my $tmp = make_shape(Features::Kind::SQUARE, $i);
    check($tmp->area == $i * $i, 'many objects');
    $tmp->close if $i % 2 == 0;
}

# Perl subclasses implement virtual methods.
my $t = Triangle->new(4, 3);
check($t->area == 6 && area_of($t) == 6 && $t->twice_area == 12 && describe($t) eq 'triangle:7', 'Perl override called from C++ and from Perl');
check(total_area([$t, Features::Circle->new(1), Features::Square->new(2)]) == 13 && $t->label('@') eq '@triangle', 'mixed overrides');
fails(sub { area_of(Incomplete->new) }, qr/pure virtual/);
my $sentinel = fails(sub { area_of(Failing->new) }, qr/Sentinel/, 'Sentinel');
check($sentinel->{message} eq 'from Perl', "an override's error crosses C++ unchanged");
fails(sub { area_of(Forgetful->new) }, qr/SUPER::new/, 'Features::Error::Binding');
fails(sub { Features::Shape->new }, qr/abstract/, 'Features::Error::Binding');
my $l = Loud->new(2);
check(describe($l) eq 'CIRCLE:0' && $l->name eq 'CIRCLE' && $l->area == 12, 'SUPER:: runs the C++ method');
check(describe(Louder->new(1)) eq 'circle!:0', 'override of an override');
$canvas = Features::Canvas->new;
my $u = UnitShape->new('kept');
$canvas->add($u);
check($u->area == 1, 'Perl-implemented object owned by C++');
$canvas->share(Features::Square->new(2));
undef $u;
check($canvas->total == 5 && $canvas->size == 2, 'C++ keeps the Perl object alive');
my $back = $canvas->at(0);
check(ref $back eq 'UnitShape' && $back->{tag} eq 'kept' && refaddr($back) == refaddr($canvas->at(0)), 'the same Perl object comes back');
$canvas->clear;
$canvas->share(UnitShape->new(''));
check($canvas->total == 1, 'shared Perl-implemented object');
$canvas->clear;
my $sm = Summer->new;
check(run_visitor($sm, [1.0, 2.0]) == 30 && same_list($sm->{seen}, [1, 2]), 'callback interface');
check($sm->visit(4.0) == 40, 'a direct call runs the Perl method');
check(run_visitor(Features::Visitor->new, [1.0, 2.0]) == 3, 'native default implementation');

# POD carries the C++ documentation.
my $d = pod_of('add', "\n=head1 FUNCTIONS\n");
check($d =~ /Adds three integers, Socha & Dorigo style\./, 'summary');
check($d =~ /=item C<\$a> \(integer\)\n\nfirst term/, 'parameters');
check($d =~ /Returns integer: the sum/, 'returns');
check($d =~ /\n    int total = add\(1\);/, 'code block');
check($d =~ m{See also: L</pick>, L</sum>}, 'see also');
check($d =~ /Dies with C<Features::Error::OutOfRange>/, 'throws');
check($d =~ /C<int> arithmetic and B<no> overflow checks/, 'inline markup');
check(pod_of('new', "\n=head1 Features::Circle\n") =~ /C<SUPER::new>/ || $source =~ /=head1 Features::Circle\n.*?SUPER::method/s, 'subclassing documented');
check(pod_of('pick') =~ /Overloads/, 'overloads listed');
check(pod_of('x', "\n=head1 Features::Vec2\n") =~ /Horizontal component\. \(number\)/, 'accessor documentation');

# Policy modules: features::data functions live in the package Features::Data.
my $unit = unit();
check(ref $unit eq 'Features::Vec2' && $unit->x == 1 && $unit->y == 0, 'submodule result');
check(scaled(3) == 6 && scaled(2.5) == 6.25, 'submodule overloads');
check(Features::Data::repeat('ab') eq 'abab' && repeat('x', 3) eq 'xxx', 'submodule names are separate');
check($source =~ /^package Features::Data;$/m, 'submodule package');

print "Perl API passed ($checks checks)\n";
