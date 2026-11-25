#!/usr/bin/env perl
# The Perl API for tests/perl_full_names.hpp: Perl builtins as function names, the runtime's
# package names, reserved subs, a renamed virtual method, multiple inheritance, C++ hiding,
# operators, 64-bit unsigned values and booleans. Argument: the generated module.
use strict;
use warnings;
use Scalar::Util qw(refaddr);

my $file;
BEGIN {
    $file = shift @ARGV or die "usage: $0 names.pm\n";
    require $file;
    Names->import(qw(:all));
}

my $checks = 0;
sub check {
    my ($ok, $what) = @_;
    ++$checks;
    die "check failed: $what\n" unless $ok;
}
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

package Shouting { our @ISA = ('Names::Named'); sub label { uc shift->SUPER::label() } }
package Pipe { our @ISA = ('Names::Resource'); sub close_2 { 'pipe ' . shift->SUPER::close_2() } }
package Deep { our @ISA = ('Names::Leaf'); sub describe { 'deep ' . shift->SUPER::describe() } }
package main;

my $source = do { local $/; open my $in, '<', $file or die "$file: $!"; <$in> };

# Packages named like the runtime's, a policy module's, or alike but for case.
check(Names::NativeObject_->new->native == 2 && Names::Error_->new->what eq 'not an exception', "the runtime's names");
check(Names::Rosetta_->new->id == 3 && Names::Data->new->value == 4 && Names::Data_::twice(4) == 8, 'a class before a policy module');
check(point_x(Names::Point->new) == 1 && point_y(Names::Point_->new) == 2, 'classes alike but for case');
check(!Names::NativeObject_->new->isa('Names::Error') && Names::NativeObject_->new->isa('Names::NativeObject'), 'class hierarchy');

# Builtins as functions take an underscore; other names stay.
check(print_(4) == 4 && length_('abc') == 30 && join(',', @{ sort_([1, 3, 2]) }) eq '2,3,1' && count([1, 2]) == 2, 'builtin names');
check(length('abc') == 3 && !defined &Names::print, 'the builtins stay builtins');

# Enum cases.
check(mode_value(Names::Mode::ON) == 0 && mode_value(Names::Mode::ON_2) == 1, 'cases alike but for case');
check(mode_value(Names::Mode::BEGIN_2) == 2 && mode_value(Names::Mode::STDIN_2) == 3 && mode_value(Names::Mode::OFF) == 4, 'Perl specials as cases');

# Members.
my $cursor = Names::Cursor->new;
check($cursor->count == 20 && $cursor->count_2 == 2, 'a method before a field');
check($cursor->new_2 == 1 && $cursor->import_2 == 7 && !$cursor->isa_2 && $cursor->isa('Names::Cursor'), 'reserved subs numbered');
check($cursor->close_2 && !$cursor->is_closed && $cursor->cast_2 eq 'cast' && refaddr($cursor->cast('Names::Cursor')) == refaddr($cursor), 'NativeObject subs first');
check($cursor->get_name eq 'camel' && $cursor->get_name_2(2) eq 'snakesnake', 'names alike in snake_case');
check($cursor->print(3) == 6 && $cursor->move(4) == 4 && $cursor->position == 4, 'builtins as methods');
check(Names::Cursor->create == 5, 'class method');
$cursor->close;
check($cursor->is_closed, 'the generated close');

# A virtual method renamed in Perl: the override has the Perl name, and C++ calls it.
check(close_resource(Names::Resource->new) eq 'C++ close' && close_resource(Names::File->new) eq 'file close', 'renamed virtual method');
check(close_resource(Pipe->new) eq 'pipe C++ close' && Names::File->new->close_2 eq 'file close', 'Perl override of a renamed method');

# Multiple inheritance: @ISA lists both bases.
my $thing = Names::Thing->new;
check($thing->isa('Names::Counted') && $thing->isa('Names::Named'), 'both bases');
check($thing->counted == 3 && counted_of($thing) == 3 && $thing->tag eq 't', "the second base's members");
check($thing->label eq 'thing' && label_of($thing) eq 'thing', 'second base argument');
my $named = as_named($thing);
check($named->isa('Names::Thing') && label_of($named) eq 'thing', 'a pointer to the second base');
check(label_of(Shouting->new) eq 'NAMED', 'a Perl subclass of a base');

# C++ hiding: Leaf's scale and size hide Base's.
my $leaf = Names::Leaf->new;
check($leaf->scale(2, 3) == 6 && $leaf->size eq 'big' && Names::Base->new->size == 1, 'hiding');
fails(sub { $leaf->scale(2) }, qr/scale/, 'Names::Error::Binding');
check(describe_base(Deep->new) eq 'deep leaf' && describe_base($leaf) eq 'leaf', 'override two levels deep');

# Operators.
my $money = Names::Money->new(250);
check("$money" eq '2.50' && ($money + Names::Money->new(5))->cents == 255 && ($money * 2)->cents == 500, 'overloads');
check($money < Names::Money->new(300) && !($money < Names::Money->new(1)) && $money->(5) == 255, 'comparison and call');
fails(sub { my $x = 2 * $money }, qr/no C\+\+ operator\* takes Names::Money on the right/, 'Names::Error::Binding');
check($money ? 1 : 0, 'an object is true');

# Values.
check(biggest() == 18446744073709551615 && biggest() eq '18446744073709551615', 'unsigned 64-bit result');
check(echo_u64(biggest()) == 18446744073709551615 && echo_u64('18446744073709551615') == 18446744073709551615, 'unsigned 64-bit argument');
check(echo_i64(-9223372036854775807 - 1) == -9223372036854775807 - 1, 'signed 64-bit minimum');
fails(sub { echo_u64(-1) }, qr/range/, 'Names::Error::Binding');
check(!negate(1 == 1) && negate(1 == 0) && !negate(1) && negate(0) && negate('') && !negate('1'), 'booleans');
check(kind_of(1 == 1) eq 'bool' && kind_of(5) eq 'int' && kind_of('5') eq 'string', 'booleans, numbers and strings stay apart');
my ($n, $text) = (1, 'a');
bump($n, $text);
check($n == 2 && $text eq 'a!', 'variables written back');
bump(\$n, \$text);
check($n == 3 && $text eq 'a!!', 'references written back');
check(call_twice(sub { $_[0] + 1 }, 1) == 3, 'callback');

# Perl's buffered STDOUT and C++'s std::cout/stdout stay in order (a file, not a terminal).
{
    open(my $saved, '>&', \*STDOUT) or die $!;
    my $path = "names_order_$$.txt";
    open(STDOUT, '>', $path) or die $!;
    print 'a';
    say_around('b', sub { print 'c' });
    print "d\n";
    eval { say_then_throw('e') };
    print "f\n";
    close(STDOUT);
    open(STDOUT, '>&', $saved) or die $!;
    open(my $in, '<', $path) or die $!;
    local $/;
    my $written = <$in>;
    close($in);
    unlink $path;
    check($written eq "ab[c]\nd\ne\nf\n", 'output order with C++, also when it throws');
}

check($source !~ /CE<lt>/ && $source =~ /^Wraps the C\+\+ class C<names::Cursor>\.$/m, 'POD codes in leads are not escaped');
check($source =~ /^sub print_ \{/m && $source =~ /^package Names::Data_;$/m, 'generated names');
print "Perl names passed ($checks checks)\n";
