#pragma once

// PHP side of the PHP backend (--language php; src/php_full.cpp): the runtime at the top of the
// generated file, which speaks the value encoding of lisp_full_runtime.hpp (the C++ library every
// full C ABI backend shares) through the FFI extension. @NS@ is the PHP namespace, @C@ the prefix of
// the native entry points, @LIB@ the library name and @ENV@ the environment variable naming its path.
// (Split in parts: compilers limit string literal sizes.)
namespace rosetta::detail {
inline constexpr const char* php_full_runtime_parts[] = {
R"php(namespace @NS@ {

/**
 * The base class of the classes wrapping C++ classes: an object holds a C++ object, released when
 * the last PHP reference to it goes away (or at once by close()).
 */
abstract class NativeObject
{
    /** @internal */
    private ?Rosetta\Handle $rosetta = null;

    /**
     * Releases the C++ object now (calling it again does nothing). Later calls fail with a
     * BindingException.
     */
    public function close(): void
    {
        $this->rosetta?->close();
    }

    /** Whether the C++ object is released (or was never created). */
    public function isClosed(): bool
    {
        return $this->rosetta === null || $this->rosetta->holder === 0;
    }

    /**
     * This object as `$class`: its most-derived class (a C++ function declared to return a base
     * class may return a derived object) or a base class, including a secondary C++ base class
     * that is not a PHP parent class; null when the object is none of these.
     *
     * @template T of NativeObject
     * @param class-string<T> $class
     * @return T|null
     */
    public function cast(string $class): ?NativeObject
    {
        return Rosetta\Runtime::cast($this, $class);
    }

    public function __get(string $name): mixed
    {
        throw new \Error('Undefined property: ' . static::class . '::$' . $name);
    }

    public function __set(string $name, mixed $value): void
    {
        throw new \Error('Cannot create dynamic property ' . static::class . '::$' . $name);
    }

    public function __isset(string $name): bool
    {
        return false;
    }

    public function __unset(string $name): void
    {
        throw new \Error('Cannot unset property ' . static::class . '::$' . $name);
    }

    public function __debugInfo(): array
    {
        return ['closed' => $this->isClosed()];
    }

    public function __serialize(): array
    {
        throw new \LogicException(static::class . ' holds a C++ object and cannot be serialized');
    }

    public function __unserialize(array $data): void
    {
        throw new \LogicException(static::class . ' holds a C++ object and cannot be unserialized');
    }
}

/**
 * Implemented by the exceptions a call into the C++ library throws for C++ exceptions and binding
 * errors. A PHP exception thrown by a callback or an override that C++ called is rethrown
 * unchanged instead.
 */
interface NativeError extends \Throwable
{
    /**
     * What failed: "invalid-argument", "out-of-range", "domain-error", "length-error",
     * "arithmetic-error" (the C++ standard exceptions), "allocation", "native" (any other C++
     * exception), "binding" (a value that does not convert, no matching overload or a closed
     * object) or "callback".
     */
    public function getKind(): string;
}

/** A value that does not convert, no matching C++ overload, or a closed object. */
class BindingException extends \LogicException implements NativeError
{
    public function getKind(): string
    {
        return 'binding';
    }
}

/** C++ `std::invalid_argument`. */
class InvalidArgumentException extends \InvalidArgumentException implements NativeError
{
    public function getKind(): string
    {
        return 'invalid-argument';
    }
}

/** C++ `std::out_of_range`. */
class OutOfRangeException extends \OutOfRangeException implements NativeError
{
    public function getKind(): string
    {
        return 'out-of-range';
    }
}

/** C++ `std::domain_error`. */
class DomainException extends \DomainException implements NativeError
{
    public function getKind(): string
    {
        return 'domain-error';
    }
}

/** C++ `std::length_error`. */
class LengthException extends \LengthException implements NativeError
{
    public function getKind(): string
    {
        return 'length-error';
    }
}

/** C++ `std::overflow_error`, `std::underflow_error` or `std::range_error`. */
class RangeException extends \RangeException implements NativeError
{
    public function getKind(): string
    {
        return 'arithmetic-error';
    }
}

/** Any other C++ exception ("native"), or no memory for a call ("allocation"). */
class NativeException extends \RuntimeException implements NativeError
{
    public function __construct(string $message = '', private string $kind = 'native')
    {
        parent::__construct($message);
    }

    public function getKind(): string
    {
        return $this->kind;
    }
}

/** A PHP callback C++ called failed, and its exception is no longer known. */
class CallbackException extends \RuntimeException implements NativeError
{
    public function getKind(): string
    {
        return 'callback';
    }
}

/** A C++ `std::complex`. */
final class Complex implements \Stringable
{
    public function __construct(public readonly float $re = 0.0, public readonly float $im = 0.0)
    {
    }

    public function __toString(): string
    {
        return $this->re . ($this->im < 0 ? '-' : '+') . \abs($this->im) . 'i';
    }
}

/** A C++ `std::function` a call returned: call it like any PHP callable. */
final class NativeFunction
{
    /** @internal */
    public function __construct(private Rosetta\Handle $handle)
    {
    }

    public function __invoke(mixed ...$arguments): mixed
    {
        return Rosetta\Runtime::invokeNative($this->handle, $arguments);
    }
}

} // namespace @NS@

)php",
R"php(namespace @NS@\Rosetta {

use @NS@\NativeObject;

/**
 * @internal The PHP side of a C++ object: its holder in the native library.
 */
final class Handle
{
    public int $id = 0;
    public ?\WeakReference $owner = null;

    public function __construct(public int $holder, public int $class, public ?Handle $parent = null)
    {
    }

    public function close(): void
    {
        $holder = $this->holder;
        if ($holder !== 0) {
            $this->holder = 0;
            Runtime::release($holder, 1);
        }
    }

    public function __destruct()
    {
        $holder = $this->holder;
        $this->holder = 0;
        if ($holder !== 0) {
            Runtime::release($holder, 0);
        }
        if ($this->id !== 0) {
            Runtime::forget($this->id);
        }
    }
}

/** @internal A C++ std::function called from PHP: the function object, its first argument. */
final class Callee
{
    public function __construct(public Handle $handle)
    {
    }
}

/** @internal A PHP string passed as C++ bytes (a vector of char-sized integers). */
final class Bytes
{
    public function __construct(public string $data)
    {
    }
}

/** @internal A non-const reference argument C++ writes back. */
final class Slot
{
    public function __construct(public int $slot, public mixed $value)
    {
    }
}

/**
 * @internal The runtime of the generated API: it encodes a call's arguments, calls the native
 * library's single entry point through FFI, decodes the result, and runs the PHP callbacks and
 * overrides C++ calls back.
 */
final class Runtime
{
    /** @var list<array{0: class-string, 1: list<int>}> class index => (PHP class, base indexes) */
    public static array $classes = [];
    /** @var array<int, string> C++ virtual method number => PHP method */
    public static array $methods = [];
    /** @var array<class-string, int> */
    private static array $indexes = [];
    private static ?\FFI $ffi = null;
    /** @var array<int, \WeakReference> id => Handle */
    private static array $objects = [];
    /** @var array<int, array{0: mixed, 1: int}> id => (object or callback, count): values C++ holds */
    private static array $strong = [];
    /** @var array<int, \Throwable> */
    private static array $causes = [];
    private static int $next = 0;
    private static ?\Closure $handleOf = null;
    private static ?\Closure $setHandle = null;
    private static ?\Closure $invoke = null;
    private static ?\Closure $retain = null;
    /** @var array<class-string, \ReflectionClass> */
    private static array $reflections = [];

    private const CAUSE = ' [php callback #';

    private static function ffi(): \FFI
    {
        if (self::$ffi !== null) {
            return self::$ffi;
        }
        if (!\extension_loaded('ffi')) {
            throw new \@NS@\BindingException('the PHP FFI extension is required to load the native library @LIB@');
        }
        $ffi = \FFI::cdef(<<<'CDEF'
            typedef int (*rosetta_invoke)(int, unsigned long long, unsigned int, const void*, size_t, void**, size_t*);
            typedef void (*rosetta_retain)(unsigned long long, int);
            int @C@_call(unsigned int, const char*, size_t, char**, size_t*);
            void @C@_free(void*);
            void* @C@_alloc(size_t);
            void @C@_release(unsigned long long, int);
            int @C@_init(int, rosetta_invoke, rosetta_retain);
            CDEF, self::library());
        self::$handleOf = \Closure::bind(static fn (NativeObject $o): ?Handle => $o->rosetta, null, NativeObject::class);
        self::$setHandle = \Closure::bind(static function (NativeObject $o, Handle $h): void {
            $o->rosetta = $h;
        }, null, NativeObject::class);
        self::$invoke = static fn (int $kind, int $id, int $generic, $data, int $size, $result, $resultSize): int
            => self::invokeHook($kind, $id, $generic, $data, $size, $result, $resultSize);
        self::$retain = static function (int $id, int $delta): void {
            try {
                self::adjust($id, $delta);
            } catch (\Throwable) {
            }
        };
        if ($ffi->@C@_init(1, self::$invoke, self::$retain) !== 0) {
            throw new \@NS@\BindingException('native library @LIB@ was generated by a different Rosetta version');
        }
        foreach (self::$classes as $index => [$class]) {
            self::$indexes[$class] = $index;
        }
        return self::$ffi = $ffi;
    }

    // The library: @ENV@ (a path), else beside this file, in lib/ or ../lib, else the loader's search path.
    private static function library(): string
    {
        $path = \getenv('@ENV@');
        if (\is_string($path) && $path !== '') {
            return $path;
        }
        $file = match (\PHP_OS_FAMILY) {
            'Windows' => '@LIB@.dll',
            'Darwin' => 'lib@LIB@.dylib',
            default => 'lib@LIB@.so',
        };
        foreach ([__DIR__, __DIR__ . '/lib', \dirname(__DIR__) . '/lib'] as $directory) {
            if (\is_file($directory . '/' . $file)) {
                return $directory . '/' . $file;
            }
        }
        return $file;
    }

    public static function release(int $holder, int $now): void
    {
        self::$ffi?->@C@_release($holder, $now);
    }

    public static function forget(int $id): void
    {
        if (isset(self::$objects[$id]) && self::$objects[$id]->get() === null) {
            unset(self::$objects[$id]);
        }
    }

    private static function handleOf(NativeObject $object): ?Handle
    {
        self::ffi();
        return (self::$handleOf)($object);
    }

    private static function idOf(Handle $handle): int
    {
        if ($handle->id === 0) {
            $handle->id = ++self::$next;
            self::$objects[$handle->id] = \WeakReference::create($handle);
            if (self::$next % 4096 === 0) {
                foreach (self::$objects as $id => $weak) {
                    if ($weak->get() === null) {
                        unset(self::$objects[$id]);
                    }
                }
            }
        }
        return $handle->id;
    }

    private static function index(string $class): int
    {
        self::ffi();
        return self::$indexes[$class] ?? throw new \@NS@\BindingException("$class is not a class of the C++ library");
    }

    private static function derives(int $from, int $to): bool
    {
        if ($from === $to) {
            return true;
        }
        foreach (self::$classes[$from][1] ?? [] as $base) {
            if (self::derives($base, $to)) {
                return true;
            }
        }
        return false;
    }

    /** An object of `$class` holding `$handle`. */
    private static function wrap(Handle $handle, ?string $class = null): NativeObject
    {
        $class ??= self::$classes[$handle->class][0] ?? throw new \@NS@\BindingException('C++ result type is not exported to PHP');
        $reflection = self::$reflections[$class] ??= new \ReflectionClass($class);
        $object = $reflection->newInstanceWithoutConstructor();
        (self::$setHandle)($object, $handle);
        if ($handle->owner === null || $handle->owner->get() === null) {
            $handle->owner = \WeakReference::create($object);
        }
        return $object;
    }

    /** @internal NativeObject::cast. */
    public static function cast(NativeObject $object, string $class): ?NativeObject
    {
        if ($object instanceof $class) {
            return $object;
        }
        $handle = self::handleOf($object);
        if ($handle === null || !self::derives($handle->class, self::index($class))) {
            return null;
        }
        $owner = $handle->owner?->get();
        return $owner instanceof $class ? $owner : self::wrap($handle, $class);
    }

    /** @internal The object as one of its C++ base classes (asBase()). */
    public static function view(NativeObject $object, string $class): NativeObject
    {
        if ($object instanceof $class) {
            return $object;
        }
        return self::wrap(self::handleOf($object) ?? throw self::unconstructed($object), $class);
    }

    private static function unconstructed(NativeObject $object): \Throwable
    {
        return new \@NS@\BindingException(\get_class($object) . ' has no C++ object: its constructor must call parent::__construct()');
    }

    /**
     * @internal Constructs `$self` with C++ constructor number `$index`; with `$director`, as a
     * C++ subclass whose virtual methods call `$self`'s PHP methods.
     */
    public static function construct(NativeObject $self, int $index, bool $director, array $arguments): void
    {
        if (self::handleOf($self) !== null) {
            throw new \Error('Cannot call the constructor of ' . \get_class($self) . ' twice');
        }
        $handle = null;
        if ($director) {
            $handle = new Handle(0, 0);
            $handle->owner = \WeakReference::create($self);
            \array_unshift($arguments, self::idOf($handle));
        } else {
            \array_unshift($arguments, 0);
        }
        $created = self::call($index, $arguments);
        if (!$created instanceof NativeObject) {
            throw new \@NS@\BindingException('C++ constructor returned no object');
        }
        $made = self::handleOf($created);
        if ($handle !== null) {
            $handle->holder = $made->holder;
            $handle->class = $made->class;
            $made->holder = 0;
        } else {
            $handle = $made;
            $handle->owner = \WeakReference::create($self);
        }
        (self::$setHandle)($self, $handle);
    }

    /** @internal Calls a C++ std::function a call returned. */
    public static function invokeNative(Handle $function, array $arguments): mixed
    {
        \array_unshift($arguments, new Callee($function));
        return self::call(0, $arguments);
    }

    // ------------------------------------------------------------ encoding

    private static function encode(string &$out, mixed $value, array &$functions): void
    {
        if ($value === null) {
            $out .= "\x00";
        } elseif ($value === true) {
            $out .= "\x01";
        } elseif ($value === false) {
            $out .= "\x13";
        } elseif (\is_int($value)) {
            $out .= "\x02" . \pack('q', $value);
        } elseif (\is_float($value)) {
            $out .= "\x04" . \pack('d', $value);
        } elseif (\is_string($value)) {
            $out .= "\x05" . \pack('Q', \strlen($value)) . $value;
        } elseif (\is_array($value)) {
            if (\array_is_list($value)) {
                $out .= "\x06" . \pack('Q', \count($value));
                foreach ($value as $item) {
                    self::encode($out, $item, $functions);
                }
            } else {
                $out .= "\x09" . \pack('Q', \count($value));
                foreach ($value as $key => $item) {
                    self::encode($out, $key, $functions);
                    self::encode($out, $item, $functions);
                }
            }
        } elseif ($value instanceof NativeObject) {
            $handle = self::handleOf($value) ?? throw self::unconstructed($value);
            $holder = $handle->holder;
            $out .= "\x0a" . \pack('QQ', $holder, $holder === 0 ? 0 : self::idOf($handle));
        } elseif ($value instanceof \BackedEnum) {
            self::encode($out, $value->value, $functions);
        } elseif ($value instanceof Slot) {
            $out .= "\x0d" . \pack('L', $value->slot);
            self::encode($out, $value->value, $functions);
        } elseif ($value instanceof \@NS@\Complex) {
            $out .= "\x12" . \pack('dd', $value->re, $value->im);
        } elseif ($value instanceof \Closure || $value instanceof \@NS@\NativeFunction) {
            $id = ++self::$next;
            self::$strong[$id] = [$value, 1];
            $functions[] = $id;
            $out .= "\x0c" . \pack('Q', $id);
        } elseif ($value instanceof Bytes) {
            $out .= "\x0f" . \pack('Q', \strlen($value->data)) . $value->data;
        } elseif ($value instanceof Callee) {
            $out .= "\x0a" . \pack('QQ', $value->handle->holder, 0);
        } else {
            throw new \@NS@\BindingException('cannot pass ' . \get_debug_type($value) . ' to C++');
        }
    }

    // ------------------------------------------------------------ decoding

    private static function value(string $data, int &$at, array $arguments): mixed
    {
        $tag = \ord($data[$at++] ?? throw self::malformed());
        switch ($tag) {
            case 0:
                return null;
            case 1:
                return true;
            case 19:
                return false;
            case 2:
                $value = \unpack('q', $data, $at)[1];
                $at += 8;
                return $value;
            case 4:
                $value = \unpack('d', $data, $at)[1];
                $at += 8;
                return $value;
            case 18:
                [, $re, $im] = \unpack('d2', $data, $at);
                $at += 16;
                return new \@NS@\Complex($re, $im);
            case 3:
            case 5:
            case 14:
            case 15:
                $n = self::count($data, $at);
                $text = \substr($data, $at, $n);
                $at += $n;
                return $text;
            case 6:
            case 16:
                $n = self::count($data, $at);
                $items = [];
                for ($i = 0; $i < $n; ++$i) {
                    $items[] = self::value($data, $at, $arguments);
                }
                return $items;
            case 7:
                $n = self::count($data, $at);
                $items = $n === 0 ? [] : \array_values(\unpack("d$n", $data, $at));
                $at += 8 * $n;
                return $items;
            case 8:
                $key = self::value($data, $at, $arguments);
                return [$key, self::value($data, $at, $arguments)];
            case 9:
                $n = self::count($data, $at);
                $pairs = [];
                $keyed = true;
                for ($i = 0; $i < $n; ++$i) {
                    $key = self::value($data, $at, $arguments);
                    $keyed = $keyed && (\is_int($key) || \is_string($key));
                    $pairs[] = [$key, self::value($data, $at, $arguments)];
                }
                if (!$keyed) {
                    return $pairs;
                }
                $map = [];
                foreach ($pairs as [$key, $item]) {
                    $map[$key] = $item;
                }
                return $map;
            case 10:
                ['h' => $holder, 'c' => $class, 'p' => $parent] = \unpack('Qh/Lc/qp', $data, $at);
                $at += 20;
                $owner = null;
                if ($parent >= 0 && ($arguments[$parent] ?? null) instanceof NativeObject) {
                    $owner = self::handleOf($arguments[$parent]);
                }
                return self::wrap(new Handle($holder, $class, $owner));
            case 11:
                $id = \unpack('Q', $data, $at)[1];
                $at += 8;
                return self::lookup($id);
            case 12:
                $holder = \unpack('Q', $data, $at)[1];
                $at += 8;
                return new \@NS@\NativeFunction(new Handle($holder, -1));
            case 17:
                $index = \unpack('L', $data, $at)[1];
                $at += 4;
                return $arguments[$index] ?? null;
            default:
                throw new \@NS@\BindingException("malformed native value (tag $tag)");
        }
    }

    private static function count(string $data, int &$at): int
    {
        $n = \unpack('Q', $data, $at)[1] ?? throw self::malformed();
        $at += 8;
        if ($n < 0 || $n > \strlen($data) - $at) {
            throw self::malformed();
        }
        return $n;
    }

    private static function malformed(): \Throwable
    {
        return new \@NS@\BindingException('malformed native value');
    }

    private static function lookup(int $id): mixed
    {
        if (isset(self::$strong[$id])) {
            return self::$strong[$id][0];
        }
        $handle = (self::$objects[$id] ?? null)?->get();
        if ($handle === null) {
            throw new \@NS@\BindingException('the PHP object of a native reference is gone');
        }
        return $handle->owner?->get() ?? self::wrap($handle);
    }

    private static function adjust(int $id, int $delta): void
    {
        if (isset(self::$strong[$id])) {
            self::$strong[$id][1] += $delta;
            if (self::$strong[$id][1] <= 0) {
                unset(self::$strong[$id]);
            }
            return;
        }
        if ($delta > 0) {
            $handle = (self::$objects[$id] ?? null)?->get();
            $owner = $handle?->owner?->get();
            if ($owner !== null) {
                self::$strong[$id] = [$owner, $delta];
            }
        }
    }

    /**
     * @internal Calls native function number `$index`; what C++ wrote back to reference
     * parameters lands in `$writebacks` (slot => value).
     */
    public static function call(int $index, array $arguments, ?array &$writebacks = null): mixed
    {
        $ffi = self::ffi();
        $data = \pack('L', \count($arguments));
        $functions = [];
        try {
            foreach ($arguments as $argument) {
                self::encode($data, $argument, $functions);
            }
            $out = $ffi->new('char*');
            $size = $ffi->new('size_t');
            $status = $ffi->@C@_call($index, $data, \strlen($data), \FFI::addr($out), \FFI::addr($size));
            $result = '';
            if (!\FFI::isNull($out)) {
                $result = \FFI::string($out, $size->cdata);
                $ffi->@C@_free($out);
            }
        } finally {
            // Callbacks passed in are dropped unless C++ retained them.
            foreach ($functions as $id) {
                self::adjust($id, -1);
            }
        }
        $at = 0;
        if ($status === 0) {
            $value = self::value($result, $at, $arguments);
            $writebacks = [];
            $n = \unpack('L', $result, $at)[1] ?? 0;
            $at += 4;
            for ($i = 0; $i < $n; ++$i) {
                $slot = \unpack('L', $result, $at)[1];
                $at += 4;
                $writebacks[$slot] = self::value($result, $at, $arguments);
            }
            return $value;
        }
        if ($status === 1 || $status === 2) {
            $kind = (string) self::value($result, $at, $arguments);
            $message = (string) self::value($result, $at, $arguments);
            throw $status === 2 ? self::takeCause($message) : self::failure($kind, $message);
        }
        throw new \@NS@\NativeException('out of memory in a native call', 'allocation');
    }

    private static function failure(string $kind, string $message): \Throwable
    {
        return match ($kind) {
            'binding' => new \@NS@\BindingException($message),
            'invalid-argument' => new \@NS@\InvalidArgumentException($message),
            'out-of-range' => new \@NS@\OutOfRangeException($message),
            'domain-error' => new \@NS@\DomainException($message),
            'length-error' => new \@NS@\LengthException($message),
            'arithmetic-error' => new \@NS@\RangeException($message),
            default => new \@NS@\NativeException($message, $kind === '' ? 'native' : $kind),
        };
    }

    // A PHP exception in a callback: C++ carries its message back, and the call that reached C++
    // rethrows the original.
    private static function saveCause(\Throwable $error): string
    {
        // The trace holds the hook's FFI arguments, which point into C++ memory that is gone
        // after the hook returns: they must not be read later (printing the trace would).
        for ($e = $error; $e !== null; $e = $e->getPrevious()) {
            $property = new \ReflectionProperty($e instanceof \Exception ? \Exception::class : \Error::class, 'trace');
            $trace = $property->getValue($e);
            foreach ($trace as $i => $frame) {
                foreach ($frame['args'] ?? [] as $j => $argument) {
                    if ($argument instanceof \FFI\CData) {
                        $trace[$i]['args'][$j] = 'FFI\\CData';
                    }
                }
            }
            $property->setValue($e, $trace);
        }
        $id = ++self::$next;
        self::$causes[$id] = $error;
        return $error->getMessage() . self::CAUSE . $id . ']';
    }

    private static function takeCause(string $message): \Throwable
    {
        $at = \strrpos($message, self::CAUSE);
        if ($at !== false && \str_ends_with($message, ']')) {
            $id = (int) \substr($message, $at + \strlen(self::CAUSE), -1);
            if (isset(self::$causes[$id])) {
                $cause = self::$causes[$id];
                unset(self::$causes[$id]);
                return $cause;
            }
            $message = \substr($message, 0, $at);
        }
        return new \@NS@\CallbackException($message);
    }

    /** @internal A bytes argument: a string as bytes, a list of integers as it is. */
    public static function bytes(string|array|null $value): Bytes|array|null
    {
        return \is_string($value) ? new Bytes($value) : $value;
    }

    /** @internal A callable argument as a Closure (a function name or [object, method] too). */
    public static function callable(?callable $value): ?object
    {
        return $value === null || $value instanceof \Closure || $value instanceof \@NS@\NativeFunction ? $value : \Closure::fromCallable($value);
    }

    /**
     * @internal Converts what C++ returned for a type holding enumerations: `$spec` is an enum
     * class, or ['list', spec], ['map', key spec, value spec] or ['tuple', spec, ...].
     */
    public static function convert(mixed $value, string|array|null $spec): mixed
    {
        if ($spec === null || $value === null) {
            return $value;
        }
        if (\is_string($spec)) {
            return \is_int($value) ? ($spec::tryFrom($value) ?? $value) : $value;
        }
        if (!\is_array($value)) {
            return $value;
        }
        switch ($spec[0]) {
            case 'list':
                return \array_map(static fn ($item) => self::convert($item, $spec[1]), $value);
            case 'tuple':
                foreach ($value as $i => $item) {
                    $value[$i] = self::convert($item, $spec[$i + 1] ?? null);
                }
                return $value;
            case 'map':
                if (\array_is_list($value) && ($value === [] || \is_array($value[0]))) {
                    return \array_map(static fn ($pair) => [self::convert($pair[0], $spec[1]), self::convert($pair[1], $spec[2])], $value);
                }
                foreach ($value as $key => $item) {
                    $value[$key] = self::convert($item, $spec[2]);
                }
                return $value;
        }
        return $value;
    }

    // ------------------------------------------------------------ calls from C++

    private static function invoke(int $kind, int $id, int $generic, string $data): string
    {
        $at = 0;
        $n = \unpack('L', $data, $at)[1] ?? 0;
        $at += 4;
        $arguments = [];
        for ($i = 0; $i < $n; ++$i) {
            $arguments[] = self::value($data, $at, []);
        }
        if ($kind === 0) {
            $function = self::lookup($id);
            if (!\is_callable($function)) {
                throw new \@NS@\BindingException('the PHP callback of a native reference is gone');
            }
            $result = $function(...$arguments);
        } else {
            $object = \array_shift($arguments);
            $method = self::$methods[$generic] ?? null;
            if (!$object instanceof NativeObject || $method === null) {
                throw new \@NS@\BindingException('no PHP method for this C++ virtual method');
            }
            $result = $object->$method(...$arguments);
        }
        $out = '';
        $functions = [];
        self::encode($out, $result, $functions);
        // Callbacks inside the result stay registered until C++ retains or drops them.
        return $out;
    }

    private static function invokeHook(int $kind, int $id, int $generic, $data, int $size, $result, $resultSize): int
    {
        try {
            $out = self::invoke($kind, $id, $generic, $size > 0 ? \FFI::string($data, $size) : '');
            $status = 0;
        } catch (\Throwable $error) {
            $message = self::saveCause($error);
            $out = "\x05" . \pack('Q', \strlen($message)) . $message;
            $status = 2;
        }
        $ffi = self::$ffi;
        $n = \strlen($out);
        $buffer = $ffi->@C@_alloc(\max(1, $n));
        if (\FFI::isNull($buffer)) {
            $result[0] = null;
            $resultSize[0] = 0;
            return 3;
        }
        \FFI::memcpy($buffer, $out, $n);
        $result[0] = $buffer;
        $resultSize[0] = $n;
        return $status;
    }
}

} // namespace @NS@\Rosetta
)php"
};
} // namespace rosetta::detail
