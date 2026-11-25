// Objective-C API (--language objective-c) on the library-shaped example shared with the Python,
// Lua, Common Lisp, Julia, C#, VB, Go, Rust, PHP, Perl, Swift, Dart and Ada backends. Argument: the
// generated Features.h, for the documentation checks.
#import "Features.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static int checks = 0;

static void check(BOOL ok, const char* what) {
    checks += 1;
    if (!ok) {
        fprintf(stderr, "check failed: %s\n", what);
        exit(1);
    }
}

// The exception `body` raises, whose reason must mention `pattern`.
static NSException* fails(NSString* pattern, __unsafe_unretained void (^body)(void)) {
    checks += 1;
    @try {
        body();
    } @catch (NSException* exception) {
        if (![exception.reason containsString:pattern]) {
            fprintf(stderr, "expected an exception containing %s, got: %s\n", pattern.UTF8String, exception.reason.UTF8String);
            exit(1);
        }
        return exception;
    }
    fprintf(stderr, "expected an exception containing %s, got none\n", pattern.UTF8String);
    exit(1);
}

static NSInteger kind(NSException* exception) {
    return [exception isKindOfClass:[FeaturesError class]] ? ((FeaturesError*)exception).kind : -1;
}

static NSException* sentinel(void) {
    return [NSException exceptionWithName:@"Sentinel" reason:@"from Objective-C" userInfo:nil];
}

// Objective-C subclasses override C++ virtual methods.
@interface Triangle : FeaturesShape
@property (nonatomic) double base, height;
- (instancetype)initWithBase:(double)base height:(double)height;
@end
@implementation Triangle
- (instancetype)initWithBase:(double)base height:(double)height {
    self = [super init];
    if (self) {
        _base = base;
        _height = height;
    }
    return self;
}
- (double)area {
    return _base * _height / 2;
}
- (NSString*)name {
    return @"triangle";
}
@end

@interface Incomplete : FeaturesShape
@end
@implementation Incomplete
@end

static NSException* thrown;
@interface Failing : FeaturesShape
@end
@implementation Failing
- (double)area {
    thrown = sentinel();
    @throw thrown;
}
@end

// super runs the C++ implementation: an override builds on it.
@interface Loud : FeaturesCircle
@end
@implementation Loud
- (NSString*)name {
    return [[super name] uppercaseString];
}
@end

@interface Noisy : FeaturesCircle
@end
@implementation Noisy
- (NSString*)name {
    return [[[super name] lowercaseString] stringByAppendingString:@"!"];
}
@end

@interface Louder : Noisy
@end
@implementation Louder
@end

@interface UnitShape : FeaturesShape
@property (nonatomic, copy) NSString* tag;
@end
@implementation UnitShape
- (double)area {
    return 1;
}
@end

@interface Summer : FeaturesVisitor
@property (nonatomic, strong) NSArray<NSNumber*>* seen;
@end
@implementation Summer
- (double)visit:(double)value {
    return value * 10;
}
- (void)finished:(NSArray<NSNumber*>*)values {
    _seen = values;
}
@end

// A block called from a thread C++ did not start on.
static double thread_result = 0;
static void* thread_main(void* unused) {
    (void)unused;
    @autoreleasepool {
        thread_result = [Features apply:^double(double x) { return x + 1; } x:1];
    }
    return NULL;
}

static NSArray<NSString*>* lines;

// The doc comment right above the first line containing `declaration`.
static NSString* doc(NSString* declaration) {
    NSUInteger at = NSNotFound;
    for (NSUInteger i = 0; i < lines.count; ++i) {
        NSString* line = [lines[i] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        if ([line containsString:declaration] && ![line hasPrefix:@"///"]) {
            at = i;
            break;
        }
    }
    if (at == NSNotFound) {
        fprintf(stderr, "not declared: %s\n", declaration.UTF8String);
        exit(1);
    }
    NSMutableArray* block = [NSMutableArray array];
    NSUInteger i = at;
    while (i > 0 && [lines[i - 1] hasPrefix:@"__attribute__"]) --i;
    while (i > 0 && [[lines[i - 1] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]] hasPrefix:@"///"]) {
        --i;
        NSString* text = [[lines[i] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]] substringFromIndex:3];
        [block insertObject:[text hasPrefix:@" "] ? [text substringFromIndex:1] : text atIndex:0];
    }
    return [block componentsJoinedByString:@"\n"];
}

static void test(void) {
    // Classes, inheritance and virtual dispatch.
    FeaturesCircle* c1 = [[FeaturesCircle alloc] initWithRadius:2];
    check([c1 area] == 12 && [[c1 name] isEqual:@"circle"] && [c1 twiceArea] == 24 && [c1 radius] == 2, "circle methods");
    check([c1.description isEqual:@"FeaturesCircle"], "description names the class");
    FeaturesShape* asShape = c1;
    check(asShape == c1 && [asShape isKindOfClass:[FeaturesCircle class]], "class hierarchy");
    NSException* error = fails(@"negative radius", ^{ (void)[[FeaturesCircle alloc] initWithRadius:-1]; });
    check(kind(error) == FeaturesErrorKindInvalidArgument, "exception kind");
    FeaturesSquare* s = [FeaturesSquare new];
    check(s.side == 1 && [s area] == 1, "default constructor");
    s.side = 3;
    check([s area] == 9 && [[[FeaturesSquare alloc] initWithSide:2] area] == 4, "property set and overloaded initializer");
    check([Features areaOf:s] == 9 && [[Features describe:c1] isEqual:@"circle:0"], "functions on classes");
    // Enums.
    check([c1 kind] == FeaturesKindCircle && [s kind] == FeaturesKindSquare, "enum results");
    check([Features filled:FeaturesFillSolid] && ![Features filled:FeaturesFillNone], "enum arguments");
    check(FeaturesKindCustom == 7 && [Features filled:(FeaturesFill)1], "enum values");
    check(FeaturesShape.sidesUnknown == -1 && fabs(Features.golden - 1.618) < 1e-12, "constants");

    // Most-derived results, ownership transfer and shared ownership.
    FeaturesShape* made = [Features makeShape:FeaturesKindCircle size:1];
    check([[made cast:[FeaturesCircle class]] radius] == 1 && [made isKindOfClass:[FeaturesCircle class]], "most-derived result");
    check([made cast:[FeaturesSquare class]] == nil, "cast to another class fails");
    FeaturesCanvas* canvas = [FeaturesCanvas new];
    [canvas add:[Features makeShape:FeaturesKindSquare size:2]];
    [canvas share:[Features sharedSquare:3]];
    FeaturesCircle* transferred = [[FeaturesCircle alloc] initWithRadius:1];
    [canvas add:transferred];
    fails(@"closed", ^{ [transferred area]; });
    check([canvas size] == 3 && [canvas total] == 4 + 9 + 3, "ownership transfer");
    check([[[canvas at:0] cast:[FeaturesSquare class]] area] == 4, "borrowed element");
    @autoreleasepool {
        FeaturesShape* shared = [Features sharedSquare:2];
        [canvas share:shared];
        check([shared area] == 4 && [canvas total] == 20, "shared ownership");
        check([Features totalArea:@[ [[FeaturesCircle alloc] initWithRadius:1], shared ]] == 7, "array of objects");
    }

    // Overloads: one selector per argument count, resolved in C++.
    check([[Features pick:@1] isEqual:@"int"] && [[Features pick:@1.5] isEqual:@"double"] && [[Features pick:@2.0] isEqual:@"double"] &&
              [[Features pick:@"x"] isEqual:@"string"],
          "scalar overloads");
    check([[Features pick:@[ @1.0, @2.0 ]] isEqual:@"vector"] && [[Features pick:s] isEqual:@"shape"], "container and class overloads");
    check([Features add:1] == 111 && [Features add:1 b:2] == 103 && [Features add:1 b:2 c:3] == 6, "default arguments");

    // Containers.
    check([[Features countWords:@[ @"a", @"b", @"a" ]] isEqual:@{@"a" : @2, @"b" : @1}], "dictionary result");
    check([[Features minMax:@[ @3, @1, @2 ]] isEqual:@[ @1, @3 ]], "pair result");
    error = fails(@"empty input", ^{ [Features minMax:@[]]; });
    check(kind(error) == FeaturesErrorKindOutOfRange, "out_of_range kind");
    check([[Features find:@[ @5, @7 ] wanted:7] isEqual:@1] && [Features find:@[ @5, @7 ] wanted:9] == nil, "optional result");
    NSArray* tr = [Features transpose:@[ @[ @1, @2, @3 ], @[ @4, @5, @6 ] ]];
    check(tr.count == 3 && [tr[0] isEqual:@[ @1, @4 ]] && [tr[2] isEqual:@[ @3, @6 ]], "nested arrays");
    const uint8_t raw[] = {1, 2, 250};
    check([Features byteSum:[NSData dataWithBytes:raw length:3]] == 253 && [[Features bytesOf:@"AZ"] isEqual:[@"AZ" dataUsingEncoding:NSUTF8StringEncoding]],
          "bytes");
    NSArray<FeaturesCircle*>* cs = [Features circles:3];
    check(cs.count == 3 && [cs[2] radius] == 3, "array of class results");
    check([[Features repeat:'z' count:3] isEqual:@"zzz"] && [Features initial:@"hello"] == 'h', "characters");
    // Non-const reference parameters are pointers, written back.
    NSArray<NSNumber*>* values = @[ @1.0, @2.0 ];
    [Features scale:&values factor:3];
    check([values isEqual:@[ @3, @6 ]], "pointer argument written back");
    error = fails(@"negative count", ^{ [Features repeat:'z' count:-1]; });
    check(kind(error) == FeaturesErrorKindInvalidArgument || kind(error) == FeaturesErrorKindNative, "invalid_argument kind");
    check([[Features defaults] isEqual:@"x*1-1.500000"], "all defaults");
    check([[Features defaults:FeaturesKindCircle label:@"y" loud:YES] isEqual:@"y*0!-1.500000"], "some defaults");
    check([doc(@"+ (NSString *)defaults:(FeaturesKind)kind label:(NSString *)label loud:(BOOL)loud;") containsString:@"after the first 3"],
          "omitted defaults named");

    // Blocks both ways.
    check([Features apply:^double(double x) { return x * 2; } x:21] == 42, "block");
    error = fails(@"from Objective-C", ^{ [Features apply:^double(double x) { (void)x; @throw sentinel(); } x:1]; });
    check([error.name isEqual:@"Sentinel"] && ![error isKindOfClass:[FeaturesError class]], "a block's exception comes back unchanged");
    int (^plus)(int) = [Features adder:40];
    check(plus(2) == 42, "C++ std::function as a block");
    check([Features apply:^double(double x) { return [Features adder:1]((int)x); } x:3] == 4, "C++ function inside a block");
    __block int calls = 0;
    check([Features apply:^double(double x) { calls += 1; return x; } x:5] == 5 && calls == 1, "capturing block");
    pthread_t thread;
    pthread_create(&thread, NULL, thread_main, NULL);
    pthread_join(thread, NULL);
    check(thread_result == 2, "block from another thread");

    // Value type operators, chaining and borrowed references.
    @autoreleasepool {
        FeaturesVec2* v = [[FeaturesVec2 alloc] initWithX:1 y:2];
        FeaturesVec2* w = [v add:[[FeaturesVec2 alloc] initWithX:3 y:4]];
        check(w.x == 4 && w.y == 6 && [v multiply:2].y == 4 && [v neg].x == -1 && [v subtract:w].x == -3, "operators");
        check([v equals:[[FeaturesVec2 alloc] initWithX:1 y:2]] && ![v equals:w], "equality operator");
        check([v at:1] == 2 && [v inner:[[FeaturesVec2 alloc] initWithX:1 y:1]] == 3, "index operator and rename");
        fails(@"Vec2 index", ^{ [v at:2]; });
        FeaturesVec2* chained = [[v setX:10] setY:20];
        check(v.x == 10 && v.y == 20 && chained == v, "chaining returns the same Objective-C object");
        check([v addAssign:w] == v && v.x == 14, "compound assignment");
        v.x = 5;
        check(v.x == 5, "property setter");
        check([doc(@"@property (nonatomic, setter=setXValue:) double x;") containsString:@"Horizontal component."],
              "a method named like the setter leaves the property its name");
    }
    @autoreleasepool {
        FeaturesSegment* seg = [FeaturesSegment new];
        seg.end = [[FeaturesVec2 alloc] initWithX:3 y:4];
        check([seg length2] == 25, "object property set");
        FeaturesVec2* start = [seg first];
        start.x = 3;
        check(seg.start.x == 3 && [seg length2] == 16, "borrowed reference writes through");
        seg.start.y = 4;
        check([seg length2] == 0, "object property borrowed");
    }
    check([[[FeaturesVec2 alloc] initWithX:1 y:2] clone].y == 2, "C++ clone");
    check([Features sum:@[ [[FeaturesVec2 alloc] initWithX:1 y:2], [[FeaturesVec2 alloc] initWithX:3 y:4] ]] == 10, "array of values");
    @autoreleasepool {
        FeaturesVec2* held;
        @autoreleasepool {
            held = [[FeaturesSegment new] first];
        }
        check(held.x == 0, "borrowed reference keeps its owner alive");
    }

    // Templates, extensions, qualified names and copy types.
    @autoreleasepool {
        FeaturesIntStack* stack = [FeaturesIntStack new];
        [stack push:1];
        [stack push:2];
        check([stack size] == 2 && [[stack items] isEqual:@[ @1, @2 ]] && [stack pop] == 2, "template instance");
    }
    check([Features clampInt:42] == 10 && [Features clampInt:3 high:5] == 3 && [Features clampReal:2.5] == 2.5, "function templates");
    check([[c1 label] isEqual:@"#circle"] && [[c1 label:@"@"] isEqual:@"@circle"] && [[FeaturesCircle unit] radius] == 1, "extensions");
    check([Features featuresLegacyVersion] == 2 && [FeaturesLegacyTag new].id_ == 7, "qualified names");
    check(FeaturesLegacyModeOff == 1, "qualified enum");
    check([[[FeaturesCircle alloc] initWithRadius:2] diameter] == 4, "extension method");
    @autoreleasepool {
        FeaturesLedger* ledger = [FeaturesLedger new];
        FeaturesStamp* copy = [ledger latest];
        copy.value = 5;
        check(ledger.stamp.value == 1, "copy type");
    }
    @autoreleasepool {
        FeaturesAim* aim = [FeaturesAim new];
        aim.origin.x = 2;
        check(aim.target.point.x == 2, "nested borrowed properties");
        check([lines containsObject:@"@property (nonatomic, readonly) FeaturesVec2 *point;"], "read-only property");
    }

    // Errors, closing and releasing.
    @autoreleasepool {
        FeaturesCircle* x = [[FeaturesCircle alloc] initWithRadius:1];
        check([x area] == 3 && !x.isClosed, "before close");
        [x close];
        check(x.isClosed && [x.description isEqual:@"FeaturesCircle (closed)"], "closed");
        error = fails(@"closed", ^{ [x area]; });
        check(kind(error) == FeaturesErrorKindBinding, "binding error kind");
        [x close];
    }
    for (int i = 1; i <= 200; ++i) {
        @autoreleasepool {
            FeaturesShape* tmp = [Features makeShape:FeaturesKindSquare size:i];
            check([tmp area] == i * i, "many objects");
            if (i % 2 == 0) [tmp close];
        }
    }

    // Objective-C subclasses override virtual methods.
    @autoreleasepool {
        Triangle* t = [[Triangle alloc] initWithBase:4 height:3];
        check([t area] == 6 && [Features areaOf:t] == 6 && [t twiceArea] == 12 && [[Features describe:t] isEqual:@"triangle:7"],
              "override called from C++ and from Objective-C");
        check([Features totalArea:@[ t, [[FeaturesCircle alloc] initWithRadius:1], [[FeaturesSquare alloc] initWithSide:2] ]] == 13 &&
                  [[t label:@"@"] isEqual:@"@triangle"],
              "mixed overrides");
    }
    fails(@"pure virtual", ^{ [Features areaOf:[Incomplete new]]; });
    error = fails(@"from Objective-C", ^{ [Features areaOf:[Failing new]]; });
    check(error == thrown, "an override's exception comes back unchanged");
    error = fails(@"abstract", ^{ (void)[FeaturesShape new]; });
    check(kind(error) == FeaturesErrorKindBinding, "constructing an abstract class");
    @autoreleasepool {
        Loud* l = [[Loud alloc] initWithRadius:2];
        check([[Features describe:l] isEqual:@"CIRCLE:0"] && [[l name] isEqual:@"CIRCLE"] && [l area] == 12, "super runs the C++ method");
        check([[Features describe:[[Louder alloc] initWithRadius:1]] isEqual:@"circle!:0"], "inherited override");
    }
    FeaturesCanvas* canvas2 = [FeaturesCanvas new];
    @autoreleasepool {
        UnitShape* u = [UnitShape new];
        u.tag = @"kept";
        [canvas2 add:u];
        check([u area] == 1, "Objective-C object owned by C++");
    }
    [canvas2 share:[[FeaturesSquare alloc] initWithSide:2]];
    check([canvas2 total] == 5 && [canvas2 size] == 2, "C++ keeps the Objective-C object alive");
    @autoreleasepool {
        FeaturesShape* back = [canvas2 at:0];
        check([back isKindOfClass:[UnitShape class]] && [((UnitShape*)back).tag isEqual:@"kept"] && back == [canvas2 at:0],
              "the same Objective-C object comes back");
    }
    [canvas2 clear];
    [canvas2 share:[UnitShape new]];
    check([canvas2 total] == 1, "shared Objective-C object");
    [canvas2 clear];
    @autoreleasepool {
        Summer* sm = [Summer new];
        check([Features runVisitor:sm values:@[ @1, @2 ]] == 30, "callback interface");
        check([sm.seen isEqual:@[ @1, @2 ]], "subclass state");
        check([sm visit:4] == 40, "a direct call runs the override");
    }
    check([Features runVisitor:[FeaturesVisitor new] values:@[ @1, @2 ]] == 3, "C++ default implementation");

    // Doc comments carry the C++ documentation.
    NSString* d = doc(@"+ (int)add:(int)a;");
    check([d containsString:@"Adds three integers, Socha & Dorigo style."], "summary");
    check([d containsString:@"@param a first term"], "parameter list");
    check([d containsString:@"@return the sum"], "returns");
    check([d containsString:@"Example"] && [d containsString:@"@code"] && [d containsString:@"int total = add(1);"], "heading and code block");
    check([d containsString:@"@see pick"] && [d containsString:@"@see sum"], "doc links");
    check([d containsString:@"@throws FeaturesError of kind FeaturesErrorKindOutOfRange (C++ `std::out_of_range`)"], "errors");
    check([d containsString:@"after the first 1"], "omitted defaults");
    check([doc(@"- (instancetype)initWithRadius:(double)radius;") containsString:@"FeaturesErrorKindInvalidArgument (C++ `std::invalid_argument`)"],
          "initializer errors");
    check([doc(@"@interface FeaturesShape") containsString:@"Abstract in C++: subclass it in Objective-C"], "class remarks");
    check([doc(@"- (double)area;") containsString:@"Pure virtual in C++"], "pure virtual methods marked");

    // Policy modules: features::data functions are class methods of FeaturesData.
    FeaturesVec2* u = [FeaturesData unit];
    check(u.x == 1 && u.y == 0, "submodule result");
    check([[FeaturesData scaled:@3] isEqual:@6] && [[FeaturesData scaled:@2.5] isEqual:@6.25], "submodule overloads");
    check([[FeaturesData repeat:@"ab"] isEqual:@"abab"] && [[Features repeat:'x' count:3] isEqual:@"xxx"], "submodule names are separate");
    check([doc(@"+ (FeaturesVec2 *)unit;") containsString:@"Mapped by the policy's modules"], "submodule docs");
}

int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc < 2) {
            fprintf(stderr, "usage: %s Features.h\n", argv[0]);
            return 2;
        }
        NSString* source = [NSString stringWithContentsOfFile:@(argv[1]) encoding:NSUTF8StringEncoding error:NULL];
        if (!source) {
            fprintf(stderr, "cannot open the generated API\n");
            return 1;
        }
        lines = [source componentsSeparatedByString:@"\n"];
        @autoreleasepool {
            test();
        }
        // C++ deletes the objects released last at the next call (for leak checkers).
        [Features add:0];
        printf("Objective-C API passed (%d checks)\n", checks);
    }
    return 0;
}
