#pragma once

// Objective-C side of the Objective-C backend (--language objective-c; src/objc_full.cpp): the runtime
// of the generated PREFIX.h (its public types) and PREFIX.m (the rest), which speaks the value encoding
// of lisp_full_runtime.hpp (the C++ library every full C ABI backend shares) through its C entry points.
// @P@ is the prefix, @C@ the prefix of the native entry points, @LIB@ the library name.
// (Split in parts: compilers limit string literal sizes.)
namespace rosetta::detail {
inline constexpr const char* objc_full_runtime_header = R"objc(#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// What failed in a call into the C++ library.
typedef NS_ENUM(NSInteger, @P@ErrorKind) {
    /// A value that does not convert, no matching overload, or a closed object.
    @P@ErrorKindBinding,
    /// C++ `std::invalid_argument`.
    @P@ErrorKindInvalidArgument,
    /// C++ `std::out_of_range`.
    @P@ErrorKindOutOfRange,
    /// C++ `std::domain_error`.
    @P@ErrorKindDomainError,
    /// C++ `std::length_error`.
    @P@ErrorKindLengthError,
    /// C++ `std::overflow_error`, `std::underflow_error` or `std::range_error`.
    @P@ErrorKindArithmeticError,
    /// C++ `std::bad_alloc`, or no memory for the call.
    @P@ErrorKindAllocation,
    /// Any other C++ exception.
    @P@ErrorKindNative,
    /// A block or override failed, and its exception is no longer known.
    @P@ErrorKindCallback,
};

/// The exception a failed call raises: a C++ exception, a value that does not convert, or a
/// closed object. (An exception a block or an override raises crosses C++ and is raised again
/// unchanged.) The reason is the C++ exception's message.
@interface @P@Error : NSException
/// What failed.
@property (nonatomic, readonly) @P@ErrorKind kind;
/// An exception of `kind` with `message` as its reason.
+ (instancetype)errorWithKind:(@P@ErrorKind)kind message:(NSString *)message;
@end

/// The base class of the C++ classes. An object holds a C++ object, released when the object is
/// deallocated, or at once by -close.
@interface @P@Object : NSObject
- (instancetype)init __attribute__((unavailable));
+ (instancetype)new __attribute__((unavailable));
/// Releases the C++ object now (again does nothing); later calls raise a binding error.
- (void)close;
/// Whether the C++ object is released (or was never created).
@property (nonatomic, readonly) BOOL isClosed;
/// The object as an object of `cls`: its most-derived class (a C++ function declared to return
/// a base class may return a derived object) or a base class; nil when it is neither.
- (nullable id)cast:(Class)cls;
@end

/// A C++ `std::complex<double>`.
@interface @P@Complex : NSObject <NSCopying>
@property (nonatomic, readonly) double real;
@property (nonatomic, readonly) double imaginary;
+ (instancetype)complexWithReal:(double)real imaginary:(double)imaginary;
- (instancetype)initWithReal:(double)real imaginary:(double)imaginary NS_DESIGNATED_INITIALIZER;
- (instancetype)init __attribute__((unavailable));
@end

/// A C++ `std::function` inside a container or an `id` value, or a block C++ holds there: it
/// takes and returns values as `id` (nil as NSNull inside the arguments).
@interface @P@Function : NSObject
/// A function calling `block`.
+ (instancetype)functionWithBlock:(id _Nullable (^)(NSArray *arguments))block;
- (instancetype)init __attribute__((unavailable));
/// Calls the function.
- (nullable id)call:(NSArray *)arguments;
@end

NS_ASSUME_NONNULL_END
)objc";
inline constexpr const char* objc_full_runtime_parts[] = {
R"objc(#if !__has_feature(objc_arc)
#error "compile with -fobjc-arc (and -fobjc-arc-exceptions)"
#endif
#pragma clang diagnostic ignored "-Wunused-function"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Runtime of the generated classes: it encodes a call's arguments, calls the native library's
// single entry point (@C@_call), decodes the result, and runs blocks and overrides of C++ virtual
// methods that C++ calls back. Types travel as descriptors: B bool, c char, a/C int8/uint8, s/S
// int16/uint16, i/I int32/uint32, q/Q int64/uint64, f float, d double, @ NSString, y NSData, z
// complex, F function, ? any, O<n>; an object of class n, n<T> optional, [<T> NSArray, {<T> NSSet,
// <<K><V> NSDictionary, p<K><V> an NSArray of key-value pairs, (<n><T>... a tuple (an NSArray).

typedef int (*rosetta_invoke_fn)(int kind, unsigned long long id, unsigned generic, const unsigned char* data, size_t size,
                                 unsigned char** result, size_t* result_size);
typedef void (*rosetta_retain_fn)(unsigned long long id, int delta);
extern int @C@_call(unsigned index, const unsigned char* data, size_t size, unsigned char** out, size_t* out_size);
extern void @C@_free(void* pointer);
extern void* @C@_alloc(size_t size);
extern void @C@_release(void* holder, int now);
extern int @C@_init(int version, rosetta_invoke_fn invoke, rosetta_retain_fn retain);

// The generated code's class table and dispatch (defined after the runtime).
static uint32_t rosetta_class_count(void);
static NSString* rosetta_class_name(uint32_t index);
static Class rosetta_class_at(uint32_t index);
static BOOL rosetta_derives(uint32_t from, uint32_t to);
static id rosetta_dispatch(@P@Object* object, uint32_t generic, NSArray* arguments);

static NSString* const rosetta_marker = @" [objective-c callback #";

// ---------------------------------------------------------------- errors
@implementation @P@Error {
    @P@ErrorKind _kind;
}
+ (instancetype)errorWithKind:(@P@ErrorKind)kind message:(NSString*)message {
    @P@Error* error = [[self alloc] initWithName:@"@P@Error" reason:message userInfo:nil];
    error->_kind = kind;
    return error;
}
- (@P@ErrorKind)kind {
    return _kind;
}
@end

__attribute__((noreturn)) static void rosetta_fail(NSString* message) {
    @throw [@P@Error errorWithKind:@P@ErrorKindBinding message:message];
}

// ---------------------------------------------------------------- tables
@interface @P@RosettaWeak : NSObject
@property (nonatomic, weak) id object;
@end
@implementation @P@RosettaWeak
@end

@interface @P@RosettaEntry : NSObject
@property (nonatomic, strong) id value;
@property (nonatomic) NSInteger count;
@end
@implementation @P@RosettaEntry
@end

static pthread_mutex_t rosetta_mutex;
static pthread_once_t rosetta_once = PTHREAD_ONCE_INIT;
static uint64_t rosetta_next = 0;
static NSMutableDictionary<NSNumber*, @P@RosettaWeak*>* rosetta_objects;   // objects C++ refers to
static NSMutableDictionary<NSNumber*, @P@RosettaEntry*>* rosetta_strong;   // what C++ keeps alive
static NSMutableDictionary<NSNumber*, NSException*>* rosetta_causes;       // exceptions of callbacks
static BOOL rosetta_initialized = NO;

static void rosetta_setup(void) {
    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&rosetta_mutex, &attributes);
    pthread_mutexattr_destroy(&attributes);
    rosetta_objects = [NSMutableDictionary dictionary];
    rosetta_strong = [NSMutableDictionary dictionary];
    rosetta_causes = [NSMutableDictionary dictionary];
}
static void rosetta_lock(void) {
    pthread_once(&rosetta_once, rosetta_setup);
    pthread_mutex_lock(&rosetta_mutex);
}
static void rosetta_unlock(void) {
    pthread_mutex_unlock(&rosetta_mutex);
}

// Under the lock.
static uint64_t rosetta_next_id(void) {
    rosetta_next += 1;
    if (rosetta_next % 4096 == 0) {
        for (NSNumber* key in [rosetta_objects allKeys])
            if (rosetta_objects[key].object == nil) [rosetta_objects removeObjectForKey:key];
    }
    return rosetta_next;
}

static void rosetta_forget(uint64_t ident) {
    rosetta_lock();
    if (rosetta_objects[@(ident)].object == nil) [rosetta_objects removeObjectForKey:@(ident)];
    rosetta_unlock();
}

static void rosetta_adjust(uint64_t ident, NSInteger delta) {
    id removed = nil;   // what C++ drops is released outside the lock
    rosetta_lock();
    @P@RosettaEntry* entry = rosetta_strong[@(ident)];
    if (entry) {
        entry.count += delta;
        if (entry.count <= 0) {
            removed = entry.value;
            [rosetta_strong removeObjectForKey:@(ident)];
        }
    } else if (delta > 0) {
        id object = rosetta_objects[@(ident)].object;
        if (object) {
            entry = [@P@RosettaEntry new];
            entry.value = object;
            entry.count = delta;
            rosetta_strong[@(ident)] = entry;
        }
    }
    rosetta_unlock();
    (void)removed;
}

static uint64_t rosetta_register(id function) {
    rosetta_lock();
    const uint64_t ident = rosetta_next_id();
    @P@RosettaEntry* entry = [@P@RosettaEntry new];
    entry.value = function;
    entry.count = 1;
    rosetta_strong[@(ident)] = entry;
    rosetta_unlock();
    return ident;
}

static id rosetta_lookup(uint64_t ident) {
    rosetta_lock();
    id found = rosetta_strong[@(ident)].value;
    if (!found) found = rosetta_objects[@(ident)].object;
    rosetta_unlock();
    if (!found) rosetta_fail(@"the Objective-C object of a native reference is gone");
    return found;
}

// An exception in a callback: C++ carries its message back, and the call that reached C++
// raises the original exception.
static NSString* rosetta_save_cause(NSException* exception) {
    rosetta_lock();
    const uint64_t ident = rosetta_next_id();
    rosetta_causes[@(ident)] = exception;
    rosetta_unlock();
    return [NSString stringWithFormat:@"%@%@%llu]", exception.reason ?: exception.name, rosetta_marker, (unsigned long long)ident];
}

static NSException* rosetta_take_cause(NSString* message) {
    const NSRange at = [message rangeOfString:rosetta_marker options:NSBackwardsSearch];
    if (at.location != NSNotFound && [message hasSuffix:@"]"]) {
        const NSUInteger start = at.location + at.length;
        NSString* digits = [message substringWithRange:NSMakeRange(start, message.length - 1 - start)];
        const uint64_t ident = strtoull(digits.UTF8String, NULL, 10);
        rosetta_lock();
        NSException* cause = rosetta_causes[@(ident)];
        if (cause) [rosetta_causes removeObjectForKey:@(ident)];
        rosetta_unlock();
        if (cause) return cause;
        return [@P@Error errorWithKind:@P@ErrorKindCallback message:[message substringToIndex:at.location]];
    }
    return [@P@Error errorWithKind:@P@ErrorKindCallback message:message];
}

static int rosetta_invoke_hook(int kind, unsigned long long ident, unsigned generic, const unsigned char* data, size_t size,
                               unsigned char** result, size_t* result_size);
static void rosetta_retain_hook(unsigned long long ident, int delta) {
    rosetta_adjust(ident, delta);
}

static void rosetta_initialize(void) {
    rosetta_lock();
    if (!rosetta_initialized) {
        if (@C@_init(1, rosetta_invoke_hook, rosetta_retain_hook) != 0) {
            rosetta_unlock();
            NSLog(@"native library @LIB@ was generated by a different Rosetta version");
            abort();
        }
        rosetta_initialized = YES;
    }
    rosetta_unlock();
}

// ---------------------------------------------------------------- objects
// The Objective-C side of a C++ object: its holder in the native library.
@interface @P@RosettaHandle : NSObject {
@public
    uint64_t holder;
    uint32_t cls;
    uint64_t ident;
}
@property (nonatomic, strong, nullable) id parent;          // what a borrowed reference keeps alive
@property (nonatomic, weak, nullable) @P@Object* object;    // the object C++ refers to by id
@end

@implementation @P@RosettaHandle
- (void)dealloc {
    const uint64_t h = holder;
    holder = 0;
    // C++ destroys it at its next call, never while native code runs.
    if (h != 0) @C@_release((void*)(uintptr_t)h, 0);
    if (ident != 0) rosetta_forget(ident);
}
@end

@interface @P@Object () {
@public
    @P@RosettaHandle* _rosettaHandle;
    @P@Object* _rosettaOwner;   // a cast's original object
}
- (instancetype)initRosetta;
- (instancetype)initRosettaWithHandle:(@P@RosettaHandle*)handle;
@end

static uint64_t rosetta_id_of(@P@Object* object) {
    @P@RosettaHandle* handle = object->_rosettaHandle;
    rosetta_lock();
    if (handle->ident == 0) {
        handle->ident = rosetta_next_id();
        @P@RosettaWeak* weak = [@P@RosettaWeak new];
        weak.object = handle.object ?: object;
        rosetta_objects[@(handle->ident)] = weak;
    }
    const uint64_t ident = handle->ident;
    rosetta_unlock();
    return ident;
}

@implementation @P@Object
- (instancetype)initRosetta {
    self = [super init];
    if (self) {
        _rosettaHandle = [@P@RosettaHandle new];
        _rosettaHandle.object = self;
    }
    return self;
}
- (instancetype)initRosettaWithHandle:(@P@RosettaHandle*)handle {
    self = [super init];
    if (self) {
        _rosettaHandle = handle;
        if (handle.object == nil) handle.object = self;
    }
    return self;
}
- (void)close {
    const uint64_t h = _rosettaHandle->holder;
    _rosettaHandle->holder = 0;
    if (h != 0) @C@_release((void*)(uintptr_t)h, 1);
}
- (BOOL)isClosed {
    return _rosettaHandle->holder == 0;
}
- (id)cast:(Class)cls {
    if ([self isKindOfClass:cls]) return self;
    @P@Object* primary = _rosettaHandle.object;
    if (primary && [primary isKindOfClass:cls]) return primary;
    for (uint32_t i = 0; i < rosetta_class_count(); ++i) {
        if (rosetta_class_at(i) != cls) continue;
        if (!rosetta_derives(_rosettaHandle->cls, i)) return nil;
        @P@Object* result = [[cls alloc] initRosettaWithHandle:_rosettaHandle];
        result->_rosettaOwner = primary ?: self;
        return result;
    }
    return nil;
}
- (NSString*)description {
    NSString* name = rosetta_class_name(_rosettaHandle->cls);
    return self.isClosed ? [name stringByAppendingString:@" (closed)"] : name;
}
@end

// ---------------------------------------------------------------- values
@implementation @P@Complex
+ (instancetype)complexWithReal:(double)real imaginary:(double)imaginary {
    return [[self alloc] initWithReal:real imaginary:imaginary];
}
- (instancetype)initWithReal:(double)real imaginary:(double)imaginary {
    self = [super init];
    if (self) {
        _real = real;
        _imaginary = imaginary;
    }
    return self;
}
- (id)copyWithZone:(NSZone*)zone {
    (void)zone;
    return self;
}
- (BOOL)isEqual:(id)other {
    if (![other isKindOfClass:[@P@Complex class]]) return NO;
    @P@Complex* z = other;
    return z.real == _real && z.imaginary == _imaginary;
}
- (NSUInteger)hash {
    return [@(_real) hash] * 31 + [@(_imaginary) hash];
}
- (NSString*)description {
    return _imaginary < 0 ? [NSString stringWithFormat:@"%g-%gi", _real, -_imaginary] : [NSString stringWithFormat:@"%g+%gi", _real, _imaginary];
}
@end

@interface @P@Function () {
@public
    id _Nullable (^_block)(NSArray*);
    @P@RosettaHandle* _handle;   // a C++ std::function
}
- (instancetype)initRosettaWithHandle:(nullable @P@RosettaHandle*)handle;
@end

// A decoded C++ map: its keys and items (made a dictionary or pairs by the declared type).
@interface @P@RosettaMap : NSObject
@property (nonatomic, strong) NSMutableArray* keys;
@property (nonatomic, strong) NSMutableArray* items;
@end
@implementation @P@RosettaMap
@end

static NSString* rosetta_kind_name(id value) {
    if (value == nil || value == (id)[NSNull null]) return @"nil";
    if (value == (id)@YES || value == (id)@NO) return @"a BOOL";
    if ([value isKindOfClass:[NSNumber class]]) return [NSString stringWithFormat:@"number %@", value];
    if ([value isKindOfClass:[NSString class]]) return @"an NSString";
    if ([value isKindOfClass:[NSData class]]) return @"NSData";
    if ([value isKindOfClass:[NSArray class]]) return @"an NSArray";
    if ([value isKindOfClass:[NSSet class]]) return @"an NSSet";
    if ([value isKindOfClass:[NSDictionary class]] || [value isKindOfClass:[@P@RosettaMap class]]) return @"an NSDictionary";
    if ([value isKindOfClass:[@P@Object class]]) return rosetta_class_name(((@P@Object*)value)->_rosettaHandle->cls);
    if ([value isKindOfClass:[@P@Function class]]) return @"a function";
    if ([value isKindOfClass:[@P@Complex class]]) return @"a complex number";
    return NSStringFromClass([value class]);
}
)objc",
R"objc(
__attribute__((noreturn)) static void rosetta_mismatch(id value, NSString* wanted) {
    rosetta_fail([NSString stringWithFormat:@"cannot convert %@ to %@", rosetta_kind_name(value), wanted]);
}

// The descriptor after the one at `d`.
static const char* rosetta_skip(const char* d) {
    switch (*d) {
    case 'O':
        while (*d && *d != ';') ++d;
        return *d ? d + 1 : d;
    case 'n': case '[': case '{':
        return rosetta_skip(d + 1);
    case '<': case 'p':
        return rosetta_skip(rosetta_skip(d + 1));
    case '(': {
        const int n = d[1] - '0';
        d += 2;
        for (int i = 0; i < n; ++i) d = rosetta_skip(d);
        return d;
    }
    default:
        return *d ? d + 1 : d;
    }
}

static BOOL rosetta_unsigned_number(NSNumber* n) {
    const char* t = n.objCType;
    return t[0] == 'Q' || t[0] == 'L' || t[0] == 'I' || t[0] == 'S' || t[0] == 'C';
}
static BOOL rosetta_real_number(NSNumber* n) {
    const char* t = n.objCType;
    return t[0] == 'd' || t[0] == 'f';
}

// ---------------------------------------------------------------- encoding (Objective-C -> C++)
@interface @P@RosettaWriter : NSObject
@property (nonatomic, strong) NSMutableData* data;
@property (nonatomic, strong) NSMutableArray<NSNumber*>* functions;   // blocks registered for the call
@end
@implementation @P@RosettaWriter
- (instancetype)init {
    self = [super init];
    if (self) {
        _data = [NSMutableData data];
        _functions = [NSMutableArray array];
    }
    return self;
}
@end

static void rosetta_u8(@P@RosettaWriter* w, uint8_t v) { [w.data appendBytes:&v length:1]; }
static void rosetta_u32(@P@RosettaWriter* w, uint32_t v) { [w.data appendBytes:&v length:4]; }
static void rosetta_u64(@P@RosettaWriter* w, uint64_t v) { [w.data appendBytes:&v length:8]; }
static void rosetta_f64(@P@RosettaWriter* w, double v) { [w.data appendBytes:&v length:8]; }
static void rosetta_bytes(@P@RosettaWriter* w, uint8_t tag, const void* bytes, size_t n) {
    rosetta_u8(w, tag);
    rosetta_u64(w, n);
    if (n) [w.data appendBytes:bytes length:n];
}
static void rosetta_text(@P@RosettaWriter* w, uint8_t tag, NSString* text) {
    const char* utf8 = text.UTF8String;
    rosetta_bytes(w, tag, utf8, strlen(utf8));
}
static void rosetta_handle(@P@RosettaWriter* w, @P@RosettaHandle* handle, uint64_t ident) {
    rosetta_u8(w, 10);
    rosetta_u64(w, handle->holder);
    rosetta_u64(w, handle->holder == 0 ? 0 : ident);
}
static void rosetta_integer(@P@RosettaWriter* w, NSNumber* n) {
    if (rosetta_unsigned_number(n) && n.unsignedLongLongValue > INT64_MAX) {
        rosetta_text(w, 3, [NSString stringWithFormat:@"%llu", n.unsignedLongLongValue]);
    } else if ([n isKindOfClass:[NSDecimalNumber class]]) {
        rosetta_text(w, 3, n.stringValue);
    } else {
        rosetta_u8(w, 2);
        rosetta_u64(w, (uint64_t)n.longLongValue);
    }
}

static void rosetta_encode(@P@RosettaWriter* w, id value, const char* d);

static void rosetta_encode_any(@P@RosettaWriter* w, id value) {
    if (value == nil || value == (id)[NSNull null]) { rosetta_u8(w, 0); return; }
    if (value == (id)@YES) { rosetta_u8(w, 1); return; }
    if (value == (id)@NO) { rosetta_u8(w, 19); return; }
    if ([value isKindOfClass:[NSNumber class]]) {
        NSNumber* n = value;
        if (rosetta_real_number(n)) {
            rosetta_u8(w, 4);
            rosetta_f64(w, n.doubleValue);
        } else {
            rosetta_integer(w, n);
        }
        return;
    }
    if ([value isKindOfClass:[NSString class]]) { rosetta_text(w, 5, value); return; }
    if ([value isKindOfClass:[NSData class]]) { rosetta_bytes(w, 15, [(NSData*)value bytes], [(NSData*)value length]); return; }
    if ([value isKindOfClass:[@P@Complex class]]) {
        rosetta_u8(w, 18);
        rosetta_f64(w, [(@P@Complex*)value real]);
        rosetta_f64(w, [(@P@Complex*)value imaginary]);
        return;
    }
    if ([value isKindOfClass:[@P@Object class]]) {
        @P@Object* object = value;
        rosetta_handle(w, object->_rosettaHandle, object->_rosettaHandle->holder == 0 ? 0 : rosetta_id_of(object));
        return;
    }
    if ([value isKindOfClass:[@P@Function class]]) {
        @P@Function* function = value;
        if (function->_handle) {
            rosetta_handle(w, function->_handle, 0);
        } else {
            const uint64_t ident = rosetta_register(function);
            [w.functions addObject:@(ident)];
            rosetta_u8(w, 12);
            rosetta_u64(w, ident);
        }
        return;
    }
    if ([value isKindOfClass:[NSArray class]] || [value isKindOfClass:[NSSet class]] || [value isKindOfClass:[NSOrderedSet class]]) {
        rosetta_u8(w, 6);
        rosetta_u64(w, [(NSArray*)value count]);
        for (id item in value) rosetta_encode_any(w, item);
        return;
    }
    if ([value isKindOfClass:[NSDictionary class]]) {
        NSDictionary* dictionary = value;
        rosetta_u8(w, 9);
        rosetta_u64(w, dictionary.count);
        for (id key in dictionary) {
            rosetta_encode_any(w, key);
            rosetta_encode_any(w, dictionary[key]);
        }
        return;
    }
    rosetta_mismatch(value, @"a C++ value");
}

static void rosetta_encode(@P@RosettaWriter* w, id value, const char* d) {
    if (value == nil || value == (id)[NSNull null]) { rosetta_u8(w, 0); return; }
    switch (*d) {
    case 'n':
        rosetta_encode(w, value, d + 1);
        return;
    case 'B':
        if ([value isKindOfClass:[NSNumber class]]) { rosetta_u8(w, [(NSNumber*)value boolValue] ? 1 : 19); return; }
        break;
    case 'a': case 'C': case 's': case 'S': case 'i': case 'I': case 'q': case 'Q':
        if ([value isKindOfClass:[NSNumber class]] && value != (id)@YES && value != (id)@NO && !rosetta_real_number(value)) {
            rosetta_integer(w, value);
            return;
        }
        break;
    case 'f': case 'd':
        if ([value isKindOfClass:[NSNumber class]] && value != (id)@YES && value != (id)@NO) {
            rosetta_u8(w, 4);
            rosetta_f64(w, [(NSNumber*)value doubleValue]);
            return;
        }
        break;
    case 'c':
        if ([value isKindOfClass:[NSNumber class]]) {
            const char c = [(NSNumber*)value charValue];
            rosetta_bytes(w, 5, &c, 1);
            return;
        }
        break;
    case '[': case '{': case '(':
        if ([value isKindOfClass:[NSArray class]] || [value isKindOfClass:[NSSet class]] || [value isKindOfClass:[NSOrderedSet class]]) {
            rosetta_u8(w, 6);
            rosetta_u64(w, [(NSArray*)value count]);
            const char* item = *d == '(' ? d + 2 : d + 1;
            for (id element in value) {
                rosetta_encode(w, element, item);
                if (*d == '(') item = rosetta_skip(item);
            }
            return;
        }
        break;
    case '<': case 'p': {
        const char* key = d + 1;
        const char* item = rosetta_skip(key);
        if ([value isKindOfClass:[NSDictionary class]]) {
            NSDictionary* dictionary = value;
            rosetta_u8(w, 9);
            rosetta_u64(w, dictionary.count);
            for (id k in dictionary) {
                rosetta_encode(w, k, key);
                rosetta_encode(w, dictionary[k], item);
            }
            return;
        }
        if ([value isKindOfClass:[NSArray class]]) {
            NSArray* pairs = value;
            for (id pair in pairs)
                if (![pair isKindOfClass:[NSArray class]] || [(NSArray*)pair count] != 2) rosetta_mismatch(value, @"key-value pairs");
            rosetta_u8(w, 9);
            rosetta_u64(w, pairs.count);
            for (NSArray* pair in pairs) {
                rosetta_encode(w, pair[0], key);
                rosetta_encode(w, pair[1], item);
            }
            return;
        }
        break;
    }
    default:
        break;
    }
    rosetta_encode_any(w, value);
}

// ---------------------------------------------------------------- decoding (C++ -> Objective-C)
typedef struct {
    const uint8_t* data;
    size_t size, at;
    __unsafe_unretained NSArray* objects;   // the call's arguments that are objects (NSNull otherwise)
} rosetta_reader;

static const uint8_t* rosetta_take(rosetta_reader* r, size_t n) {
    if (n > r->size - r->at) rosetta_fail(@"malformed native value");
    const uint8_t* at = r->data + r->at;
    r->at += n;
    return at;
}
static uint8_t rosetta_get_u8(rosetta_reader* r) { return *rosetta_take(r, 1); }
static uint32_t rosetta_get_u32(rosetta_reader* r) {
    uint32_t v;
    memcpy(&v, rosetta_take(r, 4), 4);
    return v;
}
static uint64_t rosetta_get_u64(rosetta_reader* r) {
    uint64_t v;
    memcpy(&v, rosetta_take(r, 8), 8);
    return v;
}
static double rosetta_get_f64(rosetta_reader* r) {
    double v;
    memcpy(&v, rosetta_take(r, 8), 8);
    return v;
}
static size_t rosetta_get_count(rosetta_reader* r) {
    const uint64_t n = rosetta_get_u64(r);
    if (n > r->size - r->at) rosetta_fail(@"malformed native value");
    return (size_t)n;
}
static NSString* rosetta_get_text(rosetta_reader* r) {
    const size_t n = rosetta_get_count(r);
    const uint8_t* bytes = rosetta_take(r, n);
    return [[NSString alloc] initWithBytes:bytes length:n encoding:NSUTF8StringEncoding] ?: @"";
}

static NSNumber* rosetta_big_integer(NSString* text) {
    errno = 0;
    if ([text hasPrefix:@"-"]) {
        const long long v = strtoll(text.UTF8String, NULL, 10);
        if (errno == 0) return @(v);
    } else {
        const unsigned long long v = strtoull(text.UTF8String, NULL, 10);
        if (errno == 0) return @(v);
    }
    return [NSDecimalNumber decimalNumberWithString:text];
}

static id rosetta_decode(rosetta_reader* r);
static id rosetta_boxed(id value) { return value ?: [NSNull null]; }

static id rosetta_decode(rosetta_reader* r) {
    const uint8_t tag = rosetta_get_u8(r);
    switch (tag) {
    case 0: return nil;
    case 1: return @YES;
    case 19: return @NO;
    case 2: return @((long long)rosetta_get_u64(r));
    case 3: return rosetta_big_integer(rosetta_get_text(r));
    case 4: return @(rosetta_get_f64(r));
    case 18: {
        const double re = rosetta_get_f64(r);
        return [@P@Complex complexWithReal:re imaginary:rosetta_get_f64(r)];
    }
    case 5: case 14: return rosetta_get_text(r);
    case 15: {
        const size_t n = rosetta_get_count(r);
        return [NSData dataWithBytes:rosetta_take(r, n) length:n];
    }
    case 6: case 16: {
        const size_t n = rosetta_get_count(r);
        NSMutableArray* items = [NSMutableArray arrayWithCapacity:n];
        for (size_t i = 0; i < n; ++i) [items addObject:rosetta_boxed(rosetta_decode(r))];
        return items;
    }
    case 7: {
        const uint64_t n = rosetta_get_u64(r);
        if (n > (r->size - r->at) / 8) rosetta_fail(@"malformed native value");
        NSMutableArray* items = [NSMutableArray arrayWithCapacity:(NSUInteger)n];
        for (uint64_t i = 0; i < n; ++i) [items addObject:@(rosetta_get_f64(r))];
        return items;
    }
    case 9: {
        const size_t n = rosetta_get_count(r);
        @P@RosettaMap* map = [@P@RosettaMap new];
        map.keys = [NSMutableArray arrayWithCapacity:n];
        map.items = [NSMutableArray arrayWithCapacity:n];
        for (size_t i = 0; i < n; ++i) {
            [map.keys addObject:rosetta_boxed(rosetta_decode(r))];
            [map.items addObject:rosetta_boxed(rosetta_decode(r))];
        }
        return map;
    }
    case 8: {
        id key = rosetta_boxed(rosetta_decode(r));
        return [NSMutableArray arrayWithObjects:key, rosetta_boxed(rosetta_decode(r)), nil];
    }
    case 10: {
        const uint64_t holder = rosetta_get_u64(r);
        const uint32_t cls = rosetta_get_u32(r);
        const int64_t parent = (int64_t)rosetta_get_u64(r);
        if (cls >= rosetta_class_count()) {
            @C@_release((void*)(uintptr_t)holder, 0);
            rosetta_fail(@"C++ result type is not exported to Objective-C");
        }
        @P@RosettaHandle* handle = [@P@RosettaHandle new];
        handle->holder = holder;
        handle->cls = cls;
        if (parent >= 0 && (uint64_t)parent < r->objects.count && r->objects[(NSUInteger)parent] != (id)[NSNull null])
            handle.parent = r->objects[(NSUInteger)parent];
        return [[rosetta_class_at(cls) alloc] initRosettaWithHandle:handle];
    }
    case 11:
        return rosetta_lookup(rosetta_get_u64(r));
    case 12: {
        @P@RosettaHandle* handle = [@P@RosettaHandle new];
        handle->holder = rosetta_get_u64(r);
        handle->cls = UINT32_MAX;
        @P@Function* function = [[@P@Function alloc] initRosettaWithHandle:handle];
        return function;
    }
    case 17: {
        const uint32_t index = rosetta_get_u32(r);
        if (index < r->objects.count && r->objects[index] != (id)[NSNull null]) return r->objects[index];
        return nil;
    }
    default:
        rosetta_fail([NSString stringWithFormat:@"malformed native value (tag %u)", tag]);
    }
}
)objc",
R"objc(
// ---------------------------------------------------------------- conversions
static id rosetta_convert(id value, const char* d);

static NSArray* rosetta_items(id value) {
    if ([value isKindOfClass:[NSArray class]]) return value;
    if ([value isKindOfClass:[@P@RosettaMap class]]) {
        @P@RosettaMap* map = value;
        NSMutableArray* pairs = [NSMutableArray arrayWithCapacity:map.keys.count];
        for (NSUInteger i = 0; i < map.keys.count; ++i) [pairs addObject:@[ map.keys[i], map.items[i] ]];
        return pairs;
    }
    if ([value isKindOfClass:[NSSet class]] || [value isKindOfClass:[NSOrderedSet class]]) return [(NSSet*)value allObjects];
    if ([value isKindOfClass:[NSDictionary class]]) {
        NSMutableArray* pairs = [NSMutableArray array];
        for (id key in value) [pairs addObject:@[ key, [(NSDictionary*)value objectForKey:key] ]];
        return pairs;
    }
    if ([value isKindOfClass:[NSData class]]) {
        NSMutableArray* items = [NSMutableArray array];
        const uint8_t* bytes = [(NSData*)value bytes];
        for (NSUInteger i = 0; i < [(NSData*)value length]; ++i) [items addObject:@(bytes[i])];
        return items;
    }
    return nil;
}

static NSNumber* rosetta_integer_in(id value, const char* d) {
    if (![value isKindOfClass:[NSNumber class]]) rosetta_mismatch(value, @"an integer");
    NSNumber* n = value;
    if (value == (id)@YES || value == (id)@NO) return @([n boolValue] ? 1 : 0);
    static const struct { char code; const char* name; long long min; unsigned long long max; } ranges[] = {
        {'a', "int8_t", INT8_MIN, INT8_MAX}, {'C', "uint8_t", 0, UINT8_MAX}, {'s', "int16_t", INT16_MIN, INT16_MAX},
        {'S', "uint16_t", 0, UINT16_MAX}, {'i', "int32_t", INT32_MIN, INT32_MAX}, {'I', "uint32_t", 0, UINT32_MAX},
        {'q', "int64_t", INT64_MIN, INT64_MAX}, {'Q', "uint64_t", 0, UINT64_MAX}};
    for (size_t k = 0; k < sizeof ranges / sizeof ranges[0]; ++k) {
        if (ranges[k].code != *d) continue;
        BOOL fits;
        NSNumber* result;
        if (rosetta_real_number(n)) {
            const double x = n.doubleValue;
            fits = x == (double)(long long)x && x >= (double)ranges[k].min && x <= (double)ranges[k].max;
            result = @((long long)x);
        } else if ([n isKindOfClass:[NSDecimalNumber class]]) {
            fits = NO;
            result = n;
        } else if (rosetta_unsigned_number(n)) {
            fits = n.unsignedLongLongValue <= ranges[k].max;
            result = n;
        } else {
            fits = n.longLongValue >= ranges[k].min && (n.longLongValue < 0 || (unsigned long long)n.longLongValue <= ranges[k].max);
            result = n;
        }
        if (!fits) rosetta_fail([NSString stringWithFormat:@"integer %@ is out of range for %s", n, ranges[k].name]);
        return result;
    }
    return n;
}

static id rosetta_convert(id value, const char* d) {
    if (value == nil || value == (id)[NSNull null]) return nil;
    switch (*d) {
    case 'n':
        return rosetta_convert(value, d + 1);
    case '?': {
        if ([value isKindOfClass:[@P@RosettaMap class]]) {
            @P@RosettaMap* map = value;
            BOOL copyable = YES;
            for (id key in map.keys) copyable = copyable && [key conformsToProtocol:@protocol(NSCopying)] && key != (id)[NSNull null];
            if (!copyable) return rosetta_convert(value, "p??");
            return rosetta_convert(value, "<??");
        }
        if ([value isKindOfClass:[NSArray class]]) return rosetta_convert(value, "[?");
        return value;
    }
    case 'B':
        if ([value isKindOfClass:[NSNumber class]]) return [(NSNumber*)value boolValue] ? @YES : @NO;
        rosetta_mismatch(value, @"BOOL");
    case 'a': case 'C': case 's': case 'S': case 'i': case 'I': case 'q': case 'Q':
        return rosetta_integer_in(value, d);
    case 'f': case 'd':
        if ([value isKindOfClass:[NSNumber class]] && value != (id)@YES && value != (id)@NO) return @([(NSNumber*)value doubleValue]);
        rosetta_mismatch(value, *d == 'f' ? @"float" : @"double");
    case 'c':
        if ([value isKindOfClass:[NSString class]] && strlen([(NSString*)value UTF8String]) == 1) return @([(NSString*)value UTF8String][0]);
        if ([value isKindOfClass:[NSData class]] && [(NSData*)value length] == 1) return @(((const char*)[(NSData*)value bytes])[0]);
        if ([value isKindOfClass:[NSNumber class]]) return @([(NSNumber*)value charValue]);
        rosetta_mismatch(value, @"char");
    case '@':
        if ([value isKindOfClass:[NSString class]]) return value;
        if ([value isKindOfClass:[NSData class]]) return [[NSString alloc] initWithData:value encoding:NSUTF8StringEncoding] ?: @"";
        rosetta_mismatch(value, @"NSString");
    case 'y':
        if ([value isKindOfClass:[NSData class]]) return value;
        if ([value isKindOfClass:[NSArray class]]) {
            NSMutableData* data = [NSMutableData dataWithCapacity:[(NSArray*)value count]];
            for (id item in value) {
                const uint8_t byte = (uint8_t)[rosetta_integer_in(item, "C") unsignedCharValue];
                [data appendBytes:&byte length:1];
            }
            return data;
        }
        rosetta_mismatch(value, @"NSData");
    case 'z':
        if ([value isKindOfClass:[@P@Complex class]]) return value;
        if ([value isKindOfClass:[NSNumber class]]) return [@P@Complex complexWithReal:[(NSNumber*)value doubleValue] imaginary:0];
        rosetta_mismatch(value, @"a complex number");
    case 'F':
        if ([value isKindOfClass:[@P@Function class]]) return value;
        rosetta_mismatch(value, @"a function");
    case 'O': {
        const uint32_t index = (uint32_t)strtoul(d + 1, NULL, 10);
        Class cls = rosetta_class_at(index);
        if (![value isKindOfClass:[@P@Object class]]) rosetta_mismatch(value, rosetta_class_name(index));
        id result = [(@P@Object*)value cast:cls];
        if (!result)
            rosetta_fail([NSString stringWithFormat:@"%@ is not a %@", rosetta_class_name(((@P@Object*)value)->_rosettaHandle->cls),
                                                    rosetta_class_name(index)]);
        return result;
    }
    case '[': case '{': {
        NSArray* items = rosetta_items(value);
        if (!items) rosetta_mismatch(value, *d == '[' ? @"an NSArray" : @"an NSSet");
        NSMutableArray* result = [NSMutableArray arrayWithCapacity:items.count];
        for (id item in items) [result addObject:rosetta_boxed(rosetta_convert(item, d + 1))];
        if (*d == '{') return [NSSet setWithArray:result];
        return result;
    }
    case '(': {
        const int n = d[1] - '0';
        NSArray* items = rosetta_items(value);
        if (!items || (int)items.count != n) rosetta_mismatch(value, [NSString stringWithFormat:@"a tuple of %d", n]);
        NSMutableArray* result = [NSMutableArray arrayWithCapacity:(NSUInteger)n];
        const char* item = d + 2;
        for (int i = 0; i < n; ++i) {
            [result addObject:rosetta_boxed(rosetta_convert(items[(NSUInteger)i], item))];
            item = rosetta_skip(item);
        }
        return result;
    }
    case '<': case 'p': {
        const char* key = d + 1;
        const char* item = rosetta_skip(key);
        NSArray* pairs = rosetta_items(value);
        if (!pairs) rosetta_mismatch(value, @"an NSDictionary");
        NSMutableDictionary* dictionary = *d == '<' ? [NSMutableDictionary dictionaryWithCapacity:pairs.count] : nil;
        NSMutableArray* list = *d == 'p' ? [NSMutableArray arrayWithCapacity:pairs.count] : nil;
        for (id pair in pairs) {
            if (![pair isKindOfClass:[NSArray class]] || [(NSArray*)pair count] != 2) rosetta_mismatch(value, @"key-value pairs");
            id k = rosetta_boxed(rosetta_convert(pair[0], key));
            id v = rosetta_boxed(rosetta_convert(pair[1], item));
            if (dictionary) dictionary[k] = v;
            else [list addObject:@[ k, v ]];
        }
        return dictionary ?: list;
    }
    default:
        return value;
    }
}

static id rosetta_char(char c) {
    return [[NSString alloc] initWithBytes:&c length:1 encoding:NSISOLatin1StringEncoding];
}

// ---------------------------------------------------------------- calls
// The outcome of a call: its result, and the values C++ wrote back to pointer arguments.
@interface @P@RosettaReply : NSObject
@property (nonatomic, strong, nullable) id value;
@property (nonatomic, strong) NSMutableDictionary<NSNumber*, id>* writebacks;
@end
@implementation @P@RosettaReply
- (nullable id)writeback:(uint32_t)slot {
    return _writebacks[@(slot)];
}
@end

// One call into the native library: its arguments, then -run.
@interface @P@RosettaCall : NSObject
@property (nonatomic) uint32_t index;
@property (nonatomic, strong) @P@RosettaWriter* writer;
@property (nonatomic, strong) NSMutableArray* objects;
@property (nonatomic) uint32_t count, slots;
@end

@implementation @P@RosettaCall
+ (instancetype)call:(uint32_t)index {
    @P@RosettaCall* call = [self new];
    call.index = index;
    call.writer = [@P@RosettaWriter new];
    call.objects = [NSMutableArray array];
    rosetta_u32(call.writer, 0);   // the argument count, set by -run
    return call;
}
- (void)note:(id)value {
    _count += 1;
    [_objects addObject:[value isKindOfClass:[@P@Object class]] ? value : [NSNull null]];
}
// Adds the receiver of a method.
- (void)object:(@P@Object*)object {
    [self note:object];
    rosetta_encode_any(_writer, object);
}
// Adds an argument of type `d`.
- (void)arg:(nullable id)value type:(const char*)d {
    [self note:value];
    rosetta_encode(_writer, value, d);
}
// Adds an argument C++ may write back (a pointer argument): see -[@P@RosettaReply writeback:].
- (uint32_t)slot:(nullable id)value type:(const char*)d {
    const uint32_t slot = _slots++;
    rosetta_u8(_writer, 13);
    rosetta_u32(_writer, slot);
    [self note:value];
    rosetta_encode(_writer, value, d);
    return slot;
}
- (@P@RosettaReply*)run {
    rosetta_initialize();
    NSMutableData* data = _writer.data;
    memcpy(data.mutableBytes, &_count, 4);
    unsigned char* out = NULL;
    size_t size = 0;
    const int status = @C@_call(_index, data.bytes, data.length, &out, &size);
    // Blocks passed in are dropped unless C++ retained them.
    for (NSNumber* ident in _writer.functions) rosetta_adjust(ident.unsignedLongLongValue, -1);
    [_writer.functions removeAllObjects];
    NSData* result = out ? [NSData dataWithBytes:out length:size] : [NSData data];
    if (out) @C@_free(out);
    rosetta_reader r = {result.bytes, result.length, 0, _objects};
    if (status == 0) {
        @P@RosettaReply* reply = [@P@RosettaReply new];
        reply.value = rosetta_decode(&r);
        reply.writebacks = [NSMutableDictionary dictionary];
        const uint32_t n = rosetta_get_u32(&r);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t slot = rosetta_get_u32(&r);
            id value = rosetta_decode(&r);
            reply.writebacks[@(slot)] = rosetta_boxed(value);
        }
        return reply;
    }
    if (status == 1 || status == 2) {
        id kind = rosetta_decode(&r);
        id message = rosetta_decode(&r);
        if (![message isKindOfClass:[NSString class]]) message = @"";
        if (status == 2) @throw rosetta_take_cause(message);
        static NSDictionary<NSString*, NSNumber*>* kinds;
        if (!kinds)
            kinds = @{@"binding" : @(@P@ErrorKindBinding), @"invalid-argument" : @(@P@ErrorKindInvalidArgument),
                      @"out-of-range" : @(@P@ErrorKindOutOfRange), @"domain-error" : @(@P@ErrorKindDomainError),
                      @"length-error" : @(@P@ErrorKindLengthError), @"arithmetic-error" : @(@P@ErrorKindArithmeticError),
                      @"allocation" : @(@P@ErrorKindAllocation)};
        NSNumber* found = [kind isKindOfClass:[NSString class]] ? kinds[kind] : nil;
        @throw [@P@Error errorWithKind:found ? (@P@ErrorKind)found.integerValue : @P@ErrorKindNative message:message];
    }
    @throw [@P@Error errorWithKind:@P@ErrorKindAllocation message:@"out of memory in a native call"];
}
@end
)objc",
R"objc(
// The result of a call of a function C++ cannot fail in (noexcept, fields, constants): a failure
// (a closed object, a value that does not convert) is fatal.
static @P@RosettaReply* rosetta_must(@P@RosettaCall* call) {
    @try {
        return [call run];
    } @catch (NSException* exception) {
        NSLog(@"%@", exception.reason);
        abort();
    }
}

// Constructs `object` with C++ constructor number `call.index`; with `director`, as a C++
// subclass whose virtual methods call the Objective-C methods.
static void rosetta_construct(@P@Object* object, @P@RosettaCall* call) {
    @P@RosettaReply* reply = [call run];
    id created = reply.value;
    if (![created isKindOfClass:[@P@Object class]]) rosetta_mismatch(created, @"a new object");
    @P@RosettaHandle* handle = object->_rosettaHandle;
    @P@RosettaHandle* made = ((@P@Object*)created)->_rosettaHandle;
    handle->cls = made->cls;
    handle->holder = made->holder;
    made->holder = 0;
    handle.object = object;
}
static @P@RosettaCall* rosetta_constructor(@P@Object* object, uint32_t index, BOOL director) {
    @P@RosettaCall* call = [@P@RosettaCall call:index];
    [call arg:@(director ? (long long)rosetta_id_of(object) : 0) type:"q"];
    return call;
}

// ---------------------------------------------------------------- functions
@implementation @P@Function
// (__unsafe_unretained: libobjc2 leaks the copy ARC makes of a block parameter at -O0.)
+ (instancetype)functionWithBlock:(__unsafe_unretained id _Nullable (^)(NSArray*))block {
    @P@Function* function = [[self alloc] initRosettaWithHandle:nil];
    function->_block = block;
    return function;
}
- (instancetype)initRosettaWithHandle:(@P@RosettaHandle*)handle {
    self = [super init];
    if (self) _handle = handle;
    return self;
}
- (id)call:(NSArray*)arguments {
    if (_block) return _block(arguments);
    @P@RosettaCall* call = [@P@RosettaCall call:0];
    [call note:nil];
    rosetta_handle(call.writer, _handle, 0);
    for (id argument in arguments) [call arg:argument type:"?"];
    return rosetta_convert([call run].value, "?");
}
- (NSString*)description {
    return @"function";
}
@end

// A block's argument `i` (C++ passes the right count).
static id rosetta_argument(NSArray* arguments, NSUInteger i) {
    if (i >= arguments.count) rosetta_fail(@"a block received too few arguments");
    return arguments[i];
}

// ---------------------------------------------------------------- calls from C++
static id rosetta_invoke(int kind, uint64_t ident, uint32_t generic, const uint8_t* data, size_t size) {
    rosetta_reader r = {data, size, 0, @[]};
    const uint32_t n = rosetta_get_u32(&r);
    NSMutableArray* arguments = [NSMutableArray arrayWithCapacity:n];
    for (uint32_t i = 0; i < n; ++i) [arguments addObject:rosetta_boxed(rosetta_decode(&r))];
    if (kind == 0) {
        id function = rosetta_lookup(ident);
        if (![function isKindOfClass:[@P@Function class]]) rosetta_fail(@"the block of a native reference is gone");
        return [(@P@Function*)function call:arguments];
    }
    if (arguments.count == 0 || ![arguments[0] isKindOfClass:[@P@Object class]]) rosetta_fail(@"virtual method called without an object");
    @P@Object* object = arguments[0];
    [arguments removeObjectAtIndex:0];
    return rosetta_dispatch(object, generic, arguments);
}

static int rosetta_invoke_hook(int kind, unsigned long long ident, unsigned generic, const unsigned char* data, size_t size,
                               unsigned char** result, size_t* result_size) {
    @autoreleasepool {
        @P@RosettaWriter* w = [@P@RosettaWriter new];
        int status = 0;
        @try {
            rosetta_encode_any(w, rosetta_invoke(kind, ident, generic, data, size));
            // Blocks inside the result stay registered until C++ retains or drops them.
            [w.functions removeAllObjects];
        } @catch (id exception) {
            NSException* e = [exception isKindOfClass:[NSException class]]
                                 ? exception
                                 : [NSException exceptionWithName:@"Exception" reason:[exception description] userInfo:nil];
            w = [@P@RosettaWriter new];
            rosetta_text(w, 5, rosetta_save_cause(e));
            status = 2;
        }
        unsigned char* buffer = @C@_alloc(w.data.length ? w.data.length : 1);
        if (!buffer) {
            if (result) *result = NULL;
            if (result_size) *result_size = 0;
            return 3;
        }
        memcpy(buffer, w.data.bytes, w.data.length);
        if (result) *result = buffer;
        if (result_size) *result_size = w.data.length;
        return status;
    }
}

)objc"
};
} // namespace rosetta::detail
