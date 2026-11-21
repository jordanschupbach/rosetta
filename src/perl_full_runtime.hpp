#pragma once

// Perl side of the Perl backend (--language perl; src/perl_full.cpp): the runtime at the top of the
// generated module, which speaks the value encoding of lisp_full_runtime.hpp (the C++ library every
// full C ABI backend shares) through the XS glue the library carries. @NS@ is the Perl package, @C@
// the prefix of the native entry points, @LIB@ the library name, @ENV@ the environment variable
// naming its path and @PATH@ the package as a directory (auto/@PATH@).
// (Split in parts: compilers limit string literal sizes.)
namespace rosetta::detail {
inline constexpr const char* perl_full_runtime_parts[] = {
R"perl(package @NS@::NativeObject;
use strict;
use warnings;

# The base class of the classes wrapping C++ classes (documented in the POD above).

sub close {
    my $handle = $_[0]{__rosetta};
    $handle->close if $handle;
    return;
}

sub is_closed {
    my $handle = $_[0]{__rosetta};
    return !($handle && $handle->[0]);
}

sub cast { return @NS@::Rosetta::cast(@_) }

package @NS@::Error;
use strict;
use warnings;
use overload '""' => sub { $_[0]->as_string }, 'bool' => sub { 1 }, fallback => 1;

sub new {
    my ($class, $message, $kind) = @_;
    my ($file, $line) = @NS@::Rosetta::location();
    return bless { message => $message, kind => $kind // $class->default_kind, file => $file, line => $line }, $class;
}
sub throw { my $class = shift; die $class->new(@_) }
sub message { $_[0]{message} }
sub kind { $_[0]{kind} }
sub file { $_[0]{file} }
sub line { $_[0]{line} }
sub default_kind { 'native' }
sub as_string { my $self = shift; return "$self->{message} at $self->{file} line $self->{line}.\n" }

package @NS@::Error::Binding;
our @ISA = ('@NS@::Error');
sub default_kind { 'binding' }

package @NS@::Error::InvalidArgument;
our @ISA = ('@NS@::Error');
sub default_kind { 'invalid-argument' }

package @NS@::Error::OutOfRange;
our @ISA = ('@NS@::Error');
sub default_kind { 'out-of-range' }

package @NS@::Error::Domain;
our @ISA = ('@NS@::Error');
sub default_kind { 'domain-error' }

package @NS@::Error::Length;
our @ISA = ('@NS@::Error');
sub default_kind { 'length-error' }

package @NS@::Error::Arithmetic;
our @ISA = ('@NS@::Error');
sub default_kind { 'arithmetic-error' }

package @NS@::Error::Native;
our @ISA = ('@NS@::Error');
sub default_kind { 'native' }

package @NS@::Error::Callback;
our @ISA = ('@NS@::Error');
sub default_kind { 'callback' }

# The Perl side of a C++ object: [holder, class index, parent handle, id, owner (weak)].
package @NS@::Rosetta::Handle;
use strict;
use warnings;

sub new { my ($class, $holder, $index, $parent) = @_; return bless [$holder, $index, $parent, 0, undef], $class }

sub close {
    my $self = shift;
    my $holder = $self->[0];
    if ($holder) {
        $self->[0] = 0;
        @NS@::Rosetta::_release($holder, 1);
    }
    return;
}

sub DESTROY {
    my $self = shift;
    local ($@, $!, $?);
    my $holder = $self->[0];
    $self->[0] = 0;
    eval { @NS@::Rosetta::_release($holder, 0) } if $holder;
    @NS@::Rosetta::forget($self->[3]) if $self->[3];
    return;
}

package @NS@::Rosetta::Slot;      # a non-const reference argument: [slot, value]
package @NS@::Rosetta::Bytes;     # a string passed as C++ bytes: \$string
package @NS@::Rosetta::Callee;    # a C++ std::function called from Perl: [handle]

)perl",
R"perl(package @NS@::Rosetta;
use strict;
use warnings;
use B ();
use Config ();
use DynaLoader ();
use Scalar::Util ();

# Encodes a call's arguments, calls the native library's entry point (through the XS glue the
# library carries), decodes the result, and runs the Perl callbacks and overrides C++ calls back.

our @CLASSES;    # class index => [Perl class, [base indexes]]
our %METHODS;    # C++ virtual method number => Perl method
my %indexes;     # Perl class => class index
my $ready;
my %objects;     # id => Handle (weak)
my %strong;      # id => [value, count]: values C++ holds
my %causes;      # id => the error a Perl callback died with
my $next = 0;
my $FILE = __FILE__;
my $CAUSE = ' [perl callback #';

BEGIN {
    no warnings;
    *is_bool = $] >= 5.036 ? eval 'no warnings; sub { builtin::is_bool($_[0]) }' : sub { 0 };
}

sub binding_error { die @NS@::Error::Binding->new($_[0]) }

# Where a call into the module came from: the first frame outside this file.
sub location {
    for (my $i = 0; my @frame = caller($i); ++$i) {
        return ($frame[1], $frame[2]) if $frame[1] ne $FILE;
    }
    return ($FILE, 0);
}

# The library: @ENV@ (a path), else beside this file, in lib/ or ../lib, in auto/@PATH@ under
# @INC, else the loader's search path.
sub library {
    my $path = $ENV{'@ENV@'};
    return $path if defined $path && length $path;
    my $file = 'lib@LIB@.' . ($Config::Config{dlext} || 'so');
    (my $directory = $FILE) =~ s{[^/\\]*\z}{};
    $directory = '.' if $directory eq '';
    $directory =~ s{[/\\]\z}{} unless $directory =~ m{\A[/\\]\z};
    for my $candidate ("$directory/$file", "$directory/lib/$file", "$directory/../lib/$file") {
        return $candidate if -f $candidate;
    }
    for my $root (@INC) {
        next if ref $root;
        return "$root/auto/@PATH@/$file" if -f "$root/auto/@PATH@/$file";
    }
    return $file;
}

sub init {
    return if $ready;
    my $path = library();
    my $library = DynaLoader::dl_load_file($path, 0)
        or binding_error("cannot load the native library $path: " . (DynaLoader::dl_error() // 'unknown error'));
    for my $name (qw(call release init)) {
        my $symbol = DynaLoader::dl_find_symbol($library, "@C@_perl_$name")
            or binding_error("$path has no @C@_perl_$name: it is not the Perl build of the @LIB@ library");
        DynaLoader::dl_install_xsub("@NS@::Rosetta::_$name", $symbol, $path);
    }
    _init(\&invoke_hook, \&retain_hook) == 0
        or binding_error("native library $path was generated by a different Rosetta version");
    $indexes{ $CLASSES[$_][0] } = $_ for 0 .. $#CLASSES;
    $ready = 1;
    return;
}

sub forget {
    my ($id) = @_;
    delete $objects{$id} unless defined $objects{$id};
    return;
}

sub id_of {
    my ($handle) = @_;
    unless ($handle->[3]) {
        my $id = ++$next;
        $handle->[3] = $id;
        $objects{$id} = $handle;
        Scalar::Util::weaken($objects{$id});
    }
    return 0 + $handle->[3];
}

sub derives {
    my ($from, $to) = @_;
    return 1 if $from == $to;
    for my $base (@{ ($CLASSES[$from] // [undef, []])->[1] }) {
        return 1 if derives($base, $to);
    }
    return 0;
}

# An object of $class (default: the handle's class) holding $handle.
sub wrap {
    my ($handle, $class) = @_;
    $class //= ($CLASSES[ $handle->[1] ] // binding_error('C++ result type is not exported to Perl'))->[0];
    my $object = bless { __rosetta => $handle }, $class;
    unless (defined $handle->[4]) {
        $handle->[4] = $object;
        Scalar::Util::weaken($handle->[4]);
    }
    return $object;
}

sub unconstructed {
    my ($object) = @_;
    return ref($object) . ' has no C++ object: its constructor must call SUPER::new';
}

sub cast {
    my ($object, $class) = @_;
    return $object if $object->isa($class);
    init();
    my $to = $indexes{$class} // binding_error("$class is not a class of the C++ library");
    my $handle = $object->{__rosetta};
    return undef unless $handle && derives($handle->[1], $to);
    my $owner = $handle->[4];
    return $owner if defined $owner && $owner->isa($class);
    return wrap($handle, $class);
}

# Constructs $self with C++ constructor number $index; with $director, as a C++ subclass whose
# virtual methods call $self's Perl methods.
sub construct {
    my ($self, $index, $director, $arguments) = @_;
    die "the C++ object of $self is already constructed\n" if $self->{__rosetta};
    my $handle;
    if ($director) {
        $handle = @NS@::Rosetta::Handle->new(0, 0);
        $handle->[4] = $self;
        Scalar::Util::weaken($handle->[4]);
        unshift @$arguments, id_of($handle);
    } else {
        unshift @$arguments, 0;
    }
    my $created = call($index, $arguments);
    binding_error('C++ constructor returned no object') unless Scalar::Util::blessed($created) && $created->{__rosetta};
    my $made = $created->{__rosetta};
    if ($handle) {
        @$handle[0, 1] = @$made[0, 1];
        $made->[0] = 0;
    } else {
        $handle = $made;
        $handle->[4] = $self;
        Scalar::Util::weaken($handle->[4]);
    }
    $self->{__rosetta} = $handle;
    return $self;
}

sub invoke_native {
    my ($handle, @arguments) = @_;
    return call(0, [bless([$handle], '@NS@::Rosetta::Callee'), @arguments]);
}

# ------------------------------------------------------------ encoding
sub text { my ($kind, $bytes) = @_; return $kind . pack('Q', length $bytes) . $bytes }

sub function {
    my ($value, $functions) = @_;
    my $id = ++$next;
    $strong{$id} = [$value, 1];
    push @$functions, $id;
    return "\x0c" . pack('Q', $id);
}

sub encode {
    my ($value, $functions) = @_;
    return "\x00" unless defined $value;
    if (my $type = ref $value) {
        if (defined(my $class = Scalar::Util::blessed($value))) {
            if ($value->isa('@NS@::NativeObject')) {
                my $handle = $value->{__rosetta} // binding_error(unconstructed($value));
                my $holder = $handle->[0];
                return "\x0a" . pack('QQ', $holder, $holder ? id_of($handle) : 0);
            }
            return "\x0d" . pack('L', $value->[0]) . encode($value->[1], $functions) if $class eq '@NS@::Rosetta::Slot';
            return text("\x0f", $$value) if $class eq '@NS@::Rosetta::Bytes';
            return "\x0a" . pack('QQ', $value->[0][0], 0) if $class eq '@NS@::Rosetta::Callee';
            return "\x12" . pack('dd', $value->Re, $value->Im) if $value->isa('Math::Complex');
            return text("\x03", $value->bstr) if $value->isa('Math::BigInt');
            return "\x04" . pack('d', $value->numify) if $value->isa('Math::BigFloat');
            return function($value, $functions) if overload::Method($value, '&{}');
            return encode("$value", $functions) if overload::Method($value, '""');
            return encode(0 + $value, $functions) if overload::Method($value, '0+');
            binding_error("cannot pass a $class object to C++");
        }
        if ($type eq 'ARRAY') {
            my $out = "\x06" . pack('Q', scalar @$value);
            $out .= encode($_, $functions) for @$value;
            return $out;
        }
        if ($type eq 'HASH') {
            my @keys = sort keys %$value;
            my $out = "\x09" . pack('Q', scalar @keys);
            $out .= encode($_, $functions) . encode($value->{$_}, $functions) for @keys;
            return $out;
        }
        return function($value, $functions) if $type eq 'CODE';
        binding_error("cannot pass a $type reference to C++");
    }
    return $value ? "\x01" : "\x13" if is_bool($value);
    # A number, or a number whose string form Perl made (an interpolated number).
    my $flags = B::svref_2object(\$value)->FLAGS;
    if (($flags & (B::SVp_IOK | B::SVp_NOK)) && (!($flags & B::SVp_POK) || do { no warnings; (0 + $value) . '' eq $value })) {
        if ($flags & B::SVf_IOK) {
            return text("\x03", "$value") if ($flags & B::SVf_IVisUV) && $value > 9223372036854775807;
            return "\x02" . pack('q', $value);
        }
        return "\x04" . pack('d', $value) if $flags & B::SVp_NOK;
        return "\x02" . pack('q', $value);
    }
    utf8::encode($value) if utf8::is_utf8($value);
    return text("\x05", $value);
}

# ------------------------------------------------------------ decoding
sub malformed { binding_error('malformed native value') }

sub count {
    my ($data, $at) = @_;
    malformed() if $$at + 8 > length $$data;
    my $n = unpack('Q', substr($$data, $$at, 8));
    $$at += 8;
    malformed() if $n > length($$data) - $$at;
    return $n;
}

sub value {
    my ($data, $at, $arguments) = @_;
    malformed() if $$at >= length $$data;
    my $tag = ord substr($$data, $$at++, 1);
    return undef if $tag == 0;
    return !!1 if $tag == 1;
    return !!0 if $tag == 19;
    if ($tag == 2 || $tag == 4) {
        my $value = unpack($tag == 2 ? 'q' : 'd', substr($$data, $$at, 8));
        $$at += 8;
        return $value;
    }
    if ($tag == 18) {
        my ($re, $im) = unpack('d2', substr($$data, $$at, 16));
        $$at += 16;
        require Math::Complex;
        return Math::Complex->make($re, $im);
    }
    if ($tag == 3 || $tag == 5 || $tag == 14 || $tag == 15) {
        my $n = count($data, $at);
        my $text = substr($$data, $$at, $n);
        $$at += $n;
        if ($tag == 3) {
            return 0 + $text if $text =~ /\A[0-9]{1,20}\z/ && (length $text < 20 || $text le '18446744073709551615');
            require Math::BigInt;
            return Math::BigInt->new($text);
        }
        utf8::decode($text) if $tag != 15;
        return $text;
    }
    if ($tag == 6 || $tag == 16) {
        my $n = count($data, $at);
        return [map { value($data, $at, $arguments) } 1 .. $n];
    }
    if ($tag == 7) {
        my $n = unpack('Q', substr($$data, $$at, 8));
        $$at += 8;
        malformed() if 8 * $n > length($$data) - $$at;
        my @items = $n ? unpack("d$n", substr($$data, $$at, 8 * $n)) : ();
        $$at += 8 * $n;
        return \@items;
    }
    if ($tag == 8) {
        my $key = value($data, $at, $arguments);
        return [$key, value($data, $at, $arguments)];
    }
    if ($tag == 9) {
        my $n = count($data, $at);
        my (@pairs, $keyed);
        $keyed = 1;
        for (1 .. $n) {
            my $key = value($data, $at, $arguments);
            $keyed &&= defined $key && !ref $key;
            push @pairs, [$key, value($data, $at, $arguments)];
        }
        return \@pairs unless $keyed;
        return { map { @$_ } @pairs };
    }
    if ($tag == 10) {
        my ($holder, $class, $parent) = unpack('QLq', substr($$data, $$at, 20));
        $$at += 20;
        my $owner;
        if ($parent >= 0 && Scalar::Util::blessed($arguments->[$parent]) && $arguments->[$parent]->isa('@NS@::NativeObject')) {
            $owner = $arguments->[$parent]{__rosetta};
        }
        return wrap(@NS@::Rosetta::Handle->new($holder, $class, $owner));
    }
    if ($tag == 11) {
        my $id = unpack('Q', substr($$data, $$at, 8));
        $$at += 8;
        return lookup($id);
    }
    if ($tag == 12) {
        my $holder = unpack('Q', substr($$data, $$at, 8));
        $$at += 8;
        my $handle = @NS@::Rosetta::Handle->new($holder, -1);
        return sub { invoke_native($handle, @_) };
    }
    if ($tag == 17) {
        my $index = unpack('L', substr($$data, $$at, 4));
        $$at += 4;
        return $arguments->[$index];
    }
    binding_error("malformed native value (tag $tag)");
}

sub lookup {
    my ($id) = @_;
    return $strong{$id}[0] if $strong{$id};
    my $handle = $objects{$id} // binding_error('the Perl object of a native reference is gone');
    return $handle->[4] // wrap($handle);
}

sub adjust {
    my ($id, $delta) = @_;
    if (my $entry = $strong{$id}) {
        delete $strong{$id} if ($entry->[1] += $delta) <= 0;
        return;
    }
    if ($delta > 0) {
        my $handle = $objects{$id};
        my $owner = $handle ? $handle->[4] : undef;
        $strong{$id} = [$owner, $delta] if defined $owner;
    }
    return;
}

# Calls native function number $index with the arguments in @$arguments; what C++ writes back to
# reference parameters lands in %$writebacks (slot => value).
sub call {
    my ($index, $arguments, $writebacks) = @_;
    init() unless $ready;
    my (@functions, $status, $result);
    my $ok = do {
        local $@;
        my $done = eval {
            my $data = pack('L', scalar @$arguments);
            $data .= encode($_, \@functions) for @$arguments;
            ($status, $result) = _call($index, $data);
            1;
        };
        $result = $@ unless $done;
        $done;
    };
    # Callbacks passed in are dropped unless C++ retained them.
    adjust($_, -1) for @functions;
    die $result unless $ok;
    my $at = 0;
    if ($status == 0) {
        my $value = value(\$result, \$at, $arguments);
        if ($writebacks && $at + 4 <= length $result) {
            my $n = unpack('L', substr($result, $at, 4));
            $at += 4;
            for (1 .. $n) {
                my $slot = unpack('L', substr($result, $at, 4));
                $at += 4;
                $writebacks->{$slot} = value(\$result, \$at, $arguments);
            }
        }
        return $value;
    }
    if ($status == 1 || $status == 2) {
        my $kind = value(\$result, \$at, $arguments) // '';
        my $message = value(\$result, \$at, $arguments) // '';
        die($status == 2 ? take_cause($message) : failure($kind, $message));
    }
    die @NS@::Error::Native->new('out of memory in a native call', 'allocation');
}

my %failures = ('binding' => 'Binding', 'invalid-argument' => 'InvalidArgument', 'out-of-range' => 'OutOfRange',
    'domain-error' => 'Domain', 'length-error' => 'Length', 'arithmetic-error' => 'Arithmetic');

sub failure {
    my ($kind, $message) = @_;
    my $class = $failures{$kind};
    return "@NS@::Error::$class"->new($message) if $class;
    return @NS@::Error::Native->new($message, $kind eq '' ? 'native' : $kind);
}

# The error a Perl callback died with: C++ carries its message back, and the call that reached
# C++ dies with the original.
sub save_cause {
    my ($error) = @_;
    my $id = ++$next;
    $causes{$id} = $error;
    my $message = Scalar::Util::blessed($error) && $error->can('message') ? $error->message : "$error";
    $message = '' unless defined $message;
    chomp $message;
    return $message . $CAUSE . $id . ']';
}

sub take_cause {
    my ($message) = @_;
    if ($message =~ /\A(.*)\Q$CAUSE\E([0-9]+)\]\z/s) {
        return delete $causes{$2} if exists $causes{$2};
        $message = $1;
    }
    return @NS@::Error::Callback->new($message);
}

# ------------------------------------------------------------ helpers of the generated code
# A bytes argument: a string as bytes, an array of integers as it is.
sub bytes {
    my ($value) = @_;
    return $value if !defined $value || ref $value;
    utf8::downgrade($value, 1) or binding_error('wide character in a byte string');
    return bless \$value, '@NS@::Rosetta::Bytes';
}

# A non-const reference argument: a reference to a scalar, an array or hash reference (written
# back in place), or a variable (written back through @_).
sub slot {
    my ($slot, $value) = @_;
    $value = $$value if ref $value eq 'SCALAR' || ref $value eq 'REF';
    return bless [$slot, $value], '@NS@::Rosetta::Slot';
}

sub write_back {
    my ($arguments, $index, $value) = @_;
    my $target = \$arguments->[$index];
    my $type = ref $$target;
    if (($type eq 'SCALAR' || $type eq 'REF') && !Scalar::Util::blessed($$target)) { ${$$target} = $value }
    elsif ($type eq 'ARRAY' && ref $value eq 'ARRAY' && !Scalar::Util::blessed($$target)) { @{$$target} = @$value }
    elsif ($type eq 'HASH' && ref $value eq 'HASH' && !Scalar::Util::blessed($$target)) { %{$$target} = %$value }
    else { local $@; eval { $$target = $value; 1 } }
    return;
}

# Overloaded binary operators: $method takes the other operand on the right, $reflected on the left.
sub binary {
    my ($self, $other, $swapped, $method, $reflected, $operator) = @_;
    my $name = $swapped ? $reflected : $method;
    return $self->$name($other) if defined $name && $self->can($name);
    binding_error('no C++ operator' . $operator . ' takes ' . ref($self) . ($swapped ? ' on the right' : ' on the left'));
}

# ------------------------------------------------------------ calls from C++
sub invoke {
    my ($kind, $id, $generic, $data) = @_;
    my $at = 4;
    my $n = length $data >= 4 ? unpack('L', substr($data, 0, 4)) : 0;
    my @arguments = map { value(\$data, \$at, []) } 1 .. $n;
    my $result;
    if ($kind == 0) {
        my $function = lookup($id);
        binding_error('the Perl callback of a native reference is gone') unless ref $function;
        $result = $function->(@arguments);
    } else {
        my $object = shift @arguments;
        my $method = $METHODS{$generic};
        binding_error('no Perl method for this C++ virtual method')
            unless defined $method && Scalar::Util::blessed($object) && $object->isa('@NS@::NativeObject');
        $result = $object->$method(@arguments);
    }
    # Callbacks inside the result stay registered until C++ retains or drops them.
    return encode($result, []);
}

sub invoke_hook {
    my ($kind, $id, $generic, $data) = @_;
    local $@;
    my $out;
    return (0, $out) if eval { $out = invoke($kind, $id, $generic, $data); 1 };
    my $message = save_cause($@);
    utf8::encode($message) if utf8::is_utf8($message);
    return (2, text("\x05", $message));
}

sub retain_hook {
    local $@;
    eval { adjust(@_) };
    return;
}
)perl"
};
} // namespace rosetta::detail
