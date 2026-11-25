// The Objective-C API for tests/objc_full_names.hpp: classes named like the runtime's types or the
// module; enumerator constants named like classes; keywords, NSObject's selectors and the ARC
// method families as names; a method taking a property's setter; a renamed virtual method;
// multiple inheritance; C++ hiding; operators; 64-bit values, characters, optionals, booleans,
// pointer parameters and blocks. Argument: the generated Names.h.
#import "Names.h"
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

@interface Shouting : NamesNamed
@end
@implementation Shouting
- (NSString*)label {
    return [[super label] uppercaseString];
}
@end

@interface Pipe : NamesResource
@end
@implementation Pipe
- (NSString*)close_ {
    return [@"pipe " stringByAppendingString:[super close_]];
}
@end

@interface Deep : NamesLeaf
@end
@implementation Deep
- (NSString*)describe {
    return [@"deep " stringByAppendingString:[super describe]];
}
@end

@interface Loud : NamesThing
@end
@implementation Loud
- (NSString*)label {
    return [@"loud " stringByAppendingString:[super label]];
}
@end

static void test(NSString* source) {
    // Classes named like the runtime's types or the module, or alike but for case.
    check([NamesObject_ new].native == 2 && [[NamesError_ new].what isEqual:@"not an exception"] && [NamesComplex_ new].re == 1.5 &&
              [NamesFunction_ new].arity == 0,
          "the runtime's names");
    check([NamesRosettaCall_ new].id_ == 3 && [NamesNames new].self_ == 6, "the runtime's internal and the module's names");
    check([NamesData new].value == 4 && [NamesData_ twice:4] == 8, "a class before a policy module");
    check([Names pointX:[NamesPoint new]] == 1 && [Names pointY:[NamesPoint_ new]] == 2, "classes alike but for case");
    check([[NamesObject_ new] cast:[NamesError_ class]] == nil && [[NamesObject_ new] isKindOfClass:[NamesObject class]], "class hierarchy");
    check([NamesShapeCircle new].r == 8 && [Names shapeValue:NamesShapeCircle_] == 0 && [Names shapeValue:NamesShapeSquare] == 1,
          "an enumerator constant named like a class");

    // NSObject's selectors take an underscore, keywords too; method families keep their names.
    check([Names description_:4] == 4 && [Names alloc_:1] == 2 && [Names in_:1 id_:2] == 3 && [Names count:@[ @1, @2 ]] == 2, "function names");
    check([Names newValue] == 11 && [[Names copyText:@"ab"] isEqual:@"abab"], "method families");
    check([source containsString:@"+ (int)newValue __attribute__((objc_method_family(none)));"], "no new family");

    // Enum cases.
    check([Names modeValue:NamesModeOn] == 0 && [Names modeValue:NamesModeOn_] == 1, "cases alike but for case");
    check([Names modeValue:NamesModeYes] == 2 && [Names modeValue:NamesModeDefault] == 3 && [Names modeValue:NamesModeSelf] == 4 &&
              [Names modeValue:NamesModeOff] == 5,
          "keywords as cases");
    check([Names bigValue:NamesBigLow] == INT64_MIN && [Names bigValue:NamesBigHigh] == INT64_MAX && NamesBigLow == INT64_MIN, "64-bit enumerators");
    check([Names isTop:NamesWideTop] && NamesWideTop == UINT64_MAX, "an unsigned 64-bit enumerator");

    // Members.
    NamesCursor* cursor = [NamesCursor new];
    check([cursor count] == 20 && cursor.count2 == 2 && cursor.id_ == 9, "a method before a field, and keywords");
    check([cursor close_] && !cursor.isClosed && [[cursor cast_] isEqual:@"cast"] && [cursor cast:[NamesCursor class]] == cursor,
          "the runtime's members first");
    check([[cursor description_] isEqual:@"described"] && [cursor.description isEqual:@"NamesCursor"] && ![cursor isClosed_] && [cursor hash_] == 42,
          "NSObject's description and hash stay theirs");
    check([cursor init_] == 7 && [cursor copy_] == 8 && [cursor initialize_:3] == 3 && [cursor self_] == cursor, "NSObject's selectors");
    check([cursor newObject] == 12 && [cursor initWith:5] == 5 && cursor.newCount == 4, "method families");
    check([[cursor getName] isEqual:@"camel"] && [[cursor getName2:2] isEqual:@"snakesnake"], "names alike in camelCase");
    check([cursor print:3] == 6 && [cursor move:4] == 4 && cursor.position == 4, "fine method names");
    check([cursor setPosition:9] == cursor && cursor.position == 9, "a method named like the setter");
    cursor.position = 2;
    check(cursor.position == 2 && [cursor move:1] == 3, "the property's own setter");
    check([NamesCursor create] == 5, "class method");
    [cursor close];
    check(cursor.isClosed, "the generated close");

    // A virtual method renamed in Objective-C: the override has the Objective-C name, and C++ calls it.
    check([[Names closeResource:[NamesResource new]] isEqual:@"C++ close"] && [[Names closeResource:[NamesFile new]] isEqual:@"file close"],
          "renamed virtual method");
    check([[Names closeResource:[Pipe new]] isEqual:@"pipe C++ close"] && [[[NamesFile new] close_] isEqual:@"file close"],
          "Objective-C override of a renamed method");

    // Multiple inheritance: Counted is the superclass; Named's members are declared again.
    NamesThing* thing = [NamesThing new];
    check([thing.asNamed cast:[NamesThing class]] == thing, "both bases");
    check([thing counted] == 3 && [Names countedOf:thing] == 3 && [thing.tag isEqual:@"t"], "the further base's members");
    check([[thing label] isEqual:@"thing"] && [[Names labelOf:thing.asNamed] isEqual:@"thing"], "a further base argument");
    NamesNamed* named = [Names asNamed:thing];
    check([named cast:[NamesThing class]] != nil && [[Names labelOf:named] isEqual:@"thing"], "a pointer to the further base");
    check([[Names labelOf:[Shouting new]] isEqual:@"NAMED"] && [[Names labelOf:[Loud new].asNamed] isEqual:@"loud thing"],
          "Objective-C subclasses of either base");
    check([[Names who:nil] isEqual:@"nobody"] && [[Names who:named] isEqual:@"thing"], "a nullable pointer parameter");

    // C++ hiding: Leaf has Base's selector and its own; Leaf::size takes another name.
    NamesLeaf* leaf = [NamesLeaf new];
    check([leaf scale:2 extra:3] == 6 && [leaf scale:2] == 2, "selectors across classes");
    check([[leaf size2] isEqual:@"big"] && [leaf size] == 1 && [[NamesBase new] size] == 1, "a method with another result takes its own name");
    check([[Names describeBase:[Deep new]] isEqual:@"deep leaf"] && [[Names describeBase:leaf] isEqual:@"leaf"], "override two levels deep");

    // Operators, description and call:.
    NamesMoney* money = [[NamesMoney alloc] initWithC:250];
    check([money.description isEqual:@"2.50"] && [[NSString stringWithFormat:@"%@", money] isEqual:@"2.50"], "to_string is the description");
    NamesPadded* padded = [[NamesPadded alloc] initWithV:7];
    check([padded.description isEqual:@"007"] && [[NSString stringWithFormat:@"%@", padded] isEqual:@"007"] && [[padded toString] isEqual:@"007"] &&
              [[padded toString:5] isEqual:@"00007"],
          "to_string with default arguments is the description");
    check([money add:[[NamesMoney alloc] initWithC:5]].cents == 255 && [money multiply:2].cents == 500, "operators");
    check([money less:[[NamesMoney alloc] initWithC:300]] && ![money less:[[NamesMoney alloc] initWithC:1]] && ![money not] && [money call:5] == 255,
          "comparison, prefix operator and call");

    // A block property.
    NamesHook* hook = [NamesHook new];
    check([hook fire:1] == 101 && hook.fn(2) == 102, "a std::function property");
    __block int seen = 0;
    hook.fn = ^int(int x) {
        seen = x;
        return x * 3;
    };
    check([hook fire:4] == 12 && seen == 4, "a block set as the property");

    // Values.
    check([Names biggest] == UINT64_MAX && [Names echoU64:UINT64_MAX] == UINT64_MAX, "unsigned 64-bit values");
    check([Names echoI64:INT64_MIN] == INT64_MIN && [Names echoI8:-128] == -128, "signed values at the limits");
    check(![Names negate:YES] && [Names negate:NO], "booleans");
    check([Names nextChar:'a'] == 'b', "characters");
    check([[Names kindOf:@YES] isEqual:@"bool"] && [[Names kindOf:@5] isEqual:@"int"] && [[Names kindOf:@"5"] isEqual:@"string"],
          "booleans, numbers and strings stay apart");
    int n = 1;
    NSString* text = @"a";
    [Names bump:&n text:&text];
    check(n == 2 && [text isEqual:@"a!"], "pointer parameters written back");
    check([Names maybe:@4] == 8 && [Names maybe:nil] == -1, "optional parameter");
    check([Names callTwice:^int(int x) { return x + 1; } x:1] == 3, "block");
    NSMutableArray* items = [NSMutableArray array];
    [Names each:@[ @"x", @"y" ] f:^(NSString* item) { [items addObject:item]; }];
    check([items isEqual:@[ @"x", @"y" ]], "a block returning void");
    NSArray* pairs = [Names ranked:@[ @"a", @"b" ]];
    check(pairs.count == 2 && [pairs[1] isEqual:@[ @"b", @2 ]], "array of pairs");
    NSException* error = fails(@"kind", ^{ [Names kindOf:@[]]; });
    check([error isKindOfClass:[NamesError class]] && ((NamesError*)error).kind == NamesErrorKindBinding, "no matching overload");
    fails(@"expected an NSArray, got an NSString", ^{ [Names count:(NSArray*)@"no"]; });
}

int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc < 2) return 2;
        NSString* source = [NSString stringWithContentsOfFile:@(argv[1]) encoding:NSUTF8StringEncoding error:NULL];
        if (!source || ![@(argv[1]) hasSuffix:@"Names.h"]) {
            fprintf(stderr, "cannot open the generated API\n");
            return 1;
        }
        @autoreleasepool {
            test(source);
        }
        // C++ deletes the objects released last at the next call (for leak checkers).
        [Names newValue];
        printf("Objective-C names passed (%d checks)\n", checks);
    }
    return 0;
}
