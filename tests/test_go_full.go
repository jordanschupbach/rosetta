// Full Go API (--go-api full) on the library-shaped example shared with the Python, R6, Lua,
// Common Lisp, Julia and C# backends. Arguments: the generated package files features.go and
// data/data.go, for the documentation checks. The native library is found through CGO_LDFLAGS
// (-L and -rpath) when this program is built.
package main

import (
	"bytes"
	"errors"
	"fmt"
	"math"
	"os"
	"reflect"
	"runtime"
	"slices"
	"strings"
	"time"

	"features"
	"features/data"
)

var checks int

func check(ok bool, what string) {
	checks++
	if !ok {
		panic("check failed: " + what)
	}
}

// fails checks that err is not nil and its message contains pattern.
func fails(err error, pattern string) {
	checks++
	if err == nil {
		panic(fmt.Sprintf("expected an error containing %q, got none", pattern))
	}
	if !strings.Contains(err.Error(), pattern) {
		panic(fmt.Sprintf("expected an error containing %q, got: %v", pattern, err))
	}
}

func kind(err error) features.ErrorKind {
	var e *features.Error
	if errors.As(err, &e) {
		return e.Kind
	}
	return ""
}

func must[T any](v T, err error) T { return features.Must(v, err) }

func collect() {
	for i := 0; i < 3; i++ {
		runtime.GC()
		time.Sleep(5 * time.Millisecond)
	}
}

// Directors: Go types implement C++ virtual methods.
type triangle struct {
	*features.Shape
	base, height float64
}

func (t *triangle) Area() float64 { return t.base * t.height / 2 }
func (t *triangle) Name() string  { return "triangle" }

type incomplete struct{ *features.Shape }

type failing struct{ *features.Shape }

func (failing) Area() (float64, error) { return 0, errors.New("from Go") }

// The embedded *features.Circle reaches the C++ implementation, not the override.
type loud struct{ *features.Circle }

func (l *loud) Name() string { return strings.ToUpper(must(l.Circle.Name())) }

type noisy struct{ *features.Circle }

func (n *noisy) Name() string { return strings.ToLower(must(n.Circle.Name())) + "!" }

type louder struct{ noisy }

type unitShape struct {
	*features.Shape
	tag any
}

func (*unitShape) Area() float64 { return 1 }

type summer struct {
	*features.Visitor
	seen []float64
}

func (s *summer) Visit(value float64) float64 { return value * 10 }
func (s *summer) Finished(values []float64)   { s.seen = values }

// Compile-time checks: derived classes and Go implementations pass as their bases.
var (
	_ features.ShapeLike   = (*features.Circle)(nil)
	_ features.ShapeLike   = (*triangle)(nil)
	_ features.VisitorLike = (*summer)(nil)
	_ features.Object      = (*loud)(nil)
)

func main() {
	read := func(path string) string {
		text, err := os.ReadFile(path)
		if err != nil {
			panic(err)
		}
		return string(text)
	}
	source, dataSource := read(os.Args[1]), read(os.Args[2])
	// The doc comment right above the first line containing declaration.
	docIn := func(text, declaration string) string {
		lines := strings.Split(text, "\n")
		at := slices.IndexFunc(lines, func(line string) bool {
			return strings.Contains(line, declaration) && !strings.HasPrefix(strings.TrimSpace(line), "//")
		})
		if at < 0 {
			panic("not declared: " + declaration)
		}
		var block []string
		for i := at - 1; i >= 0 && strings.HasPrefix(strings.TrimSpace(lines[i]), "//"); i-- {
			block = append([]string{strings.TrimPrefix(strings.TrimSpace(lines[i]), "//")}, block...)
		}
		return strings.Join(block, "\n")
	}
	doc := func(declaration string) string { return docIn(source, declaration) }

	// Classes, inheritance and virtual dispatch.
	c1 := must(features.NewCircle(2))
	check(must(c1.Area()) == 12 && must(c1.Name()) == "circle" && must(c1.TwiceArea()) == 24 && must(c1.Radius()) == 2, "circle methods")
	check(strings.Contains(c1.String(), "Circle") && fmt.Sprint(c1) == c1.String(), "String names the class")
	check(c1.AsShape() == &c1.Shape && c1.AsCircle() == c1, "class hierarchy")
	_, err := features.NewCircle(-1)
	fails(err, "negative radius")
	s := must(features.NewSquare())
	check(s.Side() == 1 && must(s.Area()) == 1, "default constructor")
	check(s.SetSide(3) == nil, "field set")
	check(must(s.Area()) == 9 && must(must(features.NewSquareSide(2)).Area()) == 4, "field set and overloaded constructor")
	check(must(features.AreaOf(s)) == 9 && must(features.Describe(c1)) == "circle:0", "functions on classes")
	// Enums.
	check(must(c1.Kind()) == features.KindCircle && must(s.Kind()) == features.KindSquare, "enum results")
	check(must(features.Filled(features.FillSolid)) && !must(features.Filled(features.FillNone)), "enum arguments")
	check(features.KindCustom.String() == "custom" && features.Kind(42).String() == "42" && int(features.KindCustom) == 7, "enum values")
	check(features.ShapeSidesUnknown() == -1 && math.Abs(features.Golden()-1.618) < 1e-12, "constants")

	// Most-derived results, ownership transfer and shared ownership.
	made := must(features.MakeShape(features.KindCircle, 1))
	circle, ok := features.Cast[*features.Circle](made)
	check(ok && must(circle.Radius()) == 1, "most-derived result")
	_, ok = features.Cast[*features.Square](made)
	check(!ok, "cast to another class fails")
	canvas := must(features.NewCanvas())
	check(canvas.Add(must(features.MakeShape(features.KindSquare, 2))) == nil, "unique_ptr argument")
	check(canvas.Share(must(features.SharedSquare(3))) == nil, "shared_ptr argument")
	transferred := must(features.NewCircle(1))
	check(canvas.Add(transferred) == nil, "ownership transfer")
	_, err = transferred.Area()
	fails(err, "closed")
	check(must(canvas.Size()) == 3 && must(canvas.Total()) == 4+9+3, "ownership transfer")
	first, ok := features.Cast[*features.Square](must(canvas.At(0)))
	check(ok && must(first.Area()) == 4, "borrowed element")
	{
		shared := must(features.SharedSquare(2))
		check(canvas.Share(shared) == nil, "share")
		check(must(shared.Area()) == 4 && must(canvas.Total()) == 20, "shared ownership")
		check(must(features.TotalArea([]features.ShapeLike{must(features.NewCircle(1)), shared})) == 7, "slice of objects")
	}

	// Overloads: one Go function per C++ overload, resolved again in C++.
	check(must(features.Pick(1)) == "int" && must(features.PickFloat64(1.5)) == "double" && must(features.PickFloat64(2)) == "double" &&
		must(features.PickString("x")) == "string", "scalar overloads")
	check(must(features.PickFloat64s([]float64{1, 2})) == "vector" && must(features.PickShape(s)) == "shape", "container and class overloads")
	check(must(features.Add(1)) == 111 && must(features.AddWithB(1, 2)) == 103 && must(features.AddWithC(1, 2, 3)) == 6, "default arguments")
	_, err = features.Add(1 << 40)
	fails(err, "outside C++ type range")
	check(kind(err) == features.BindingError, "range check is a binding error")

	// Containers.
	counts := must(features.CountWords([]string{"a", "b", "a"}))
	check(counts["a"] == 2 && counts["b"] == 1 && len(counts) == 2, "map result")
	check(must(features.MinMax([]float64{3, 1, 2})) == features.Pair[float64, float64]{First: 1, Second: 3}, "pair result")
	_, err = features.MinMax(nil)
	fails(err, "empty input")
	check(kind(err) == features.OutOfRange, "out_of_range kind")
	found := must(features.Find([]int{5, 7}, 7))
	check(found != nil && *found == 1 && must(features.Find([]int{5, 7}, 9)) == nil, "optional result")
	tr := must(features.Transpose([][]float64{{1, 2, 3}, {4, 5, 6}}))
	check(len(tr) == 3 && slices.Equal(tr[0], []float64{1, 4}) && slices.Equal(tr[2], []float64{3, 6}), "nested slices")
	check(must(features.ByteSum([]byte{1, 2, 250})) == 253 && bytes.Equal(must(features.BytesOf("AZ")), []byte{65, 90}), "bytes")
	cs := must(features.Circles(3))
	check(len(cs) == 3 && must(cs[2].Radius()) == 3, "slice of class results")
	check(must(features.Repeat('z', 3)) == "zzz" && must(features.Initial("hello")) == 'h', "chars")
	// Non-const reference parameters are pointers written back.
	values := []float64{1, 2}
	check(features.Scale(&values, 3) == nil && slices.Equal(values, []float64{3, 6}), "pointer argument written back")
	_, err = features.Repeat('z', -1)
	fails(err, "negative count")
	check(kind(err) == features.InvalidArgument, "invalid_argument kind")
	check(must(features.Defaults()) == "x*1-1.500000", "all defaults")
	check(must(features.DefaultsWithLoud(features.KindCircle, "y", true)) == "y*0!-1.500000", "some defaults")
	check(strings.Contains(doc("func DefaultsWithLoud("), "weight = 0.5"), "omitted defaults named")

	// Callbacks both ways.
	check(must(features.Apply(func(x float64) float64 { return x * 2 }, 21)) == 42, "callback")
	_, err = features.Apply(func(float64) float64 { panic("inside callback") }, 1)
	fails(err, "inside callback")
	check(kind(err) == features.CallbackError, "callback error kind")
	sentinel := errors.New("sentinel")
	_, err = features.Apply(func(float64) float64 { panic(sentinel) }, 1)
	check(errors.Is(err, sentinel), "the Go error comes back through errors.Is")
	plus := must(features.Adder(40))
	check(plus(2) == 42, "native function as a Go func")
	check(must(features.Apply(func(x float64) float64 { return float64(must(features.Adder(1))(int(x))) }, 3)) == 4, "native function inside a callback")
	done := make(chan float64)
	go func() { done <- must(features.Apply(func(x float64) float64 { return x + 1 }, 1)) }()
	check(<-done == 2, "callback from another goroutine")

	// Value type operators, chaining and borrowed references.
	{
		v := must(features.NewVec2XY(1, 2))
		w := must(v.Add(must(features.NewVec2XY(3, 4))))
		check(w.X() == 4 && w.Y() == 6 && must(v.Mul(2)).Y() == 4 && must(v.Neg()).X() == -1 && must(v.Sub(w)).X() == -3, "operators")
		check(must(v.Equal(must(features.NewVec2XY(1, 2)))) && !must(v.Equal(w)), "equality operator")
		check(must(v.At(1)) == 2 && must(v.Inner(must(features.NewVec2XY(1, 1)))) == 3, "index operator and rename")
		_, err := v.At(2)
		fails(err, "Vec2 index")
		chained := must(must(v.SetX(10)).SetY(20))
		check(v.X() == 10 && v.Y() == 20 && chained == v, "chaining returns the same Go object")
		check(must(v.AddAssign(w)) == v && v.X() == 14, "compound assignment")
		check(v.SetXField(5) == nil && v.X() == 5, "field setter named apart from the set_x method")
	}
	{
		seg := must(features.NewSegment())
		check(seg.SetEnd(must(features.NewVec2XY(3, 4))) == nil && must(seg.Length2()) == 25, "field object set")
		start := must(seg.First())
		check(start.SetXField(3) == nil, "set through a reference")
		check(seg.Start().X() == 3 && must(seg.Length2()) == 16, "borrowed reference writes through")
		check(seg.Start().SetYField(4) == nil && must(seg.Length2()) == 0, "field object borrowed")
	}
	check(must(must(features.NewVec2XY(1, 2)).Clone()).Y() == 2, "clone")
	check(must(features.Sum([]*features.Vec2{must(features.NewVec2XY(1, 2)), must(features.NewVec2XY(3, 4))})) == 10, "slice of values")
	{
		held := must(must(features.NewSegment()).First())
		collect()
		check(held.X() == 0, "borrowed reference keeps its owner alive")
	}

	// Templates, extensions, qualified names and copy types.
	{
		stack := must(features.NewIntStack())
		check(stack.Push(1) == nil && stack.Push(2) == nil, "push")
		check(must(stack.Size()) == 2 && slices.Equal(must(stack.Items()), []int{1, 2}) && must(stack.Pop()) == 2, "template instance")
	}
	check(must(features.ClampInt(42)) == 10 && must(features.ClampIntWithHigh(3, 5)) == 3 && must(features.ClampReal(2.5)) == 2.5, "function templates")
	check(must(c1.Label()) == "#circle" && must(c1.LabelWithPrefix("@")) == "@circle" && must(must(features.CircleUnit()).Radius()) == 1, "extensions")
	check(must(features.FeaturesLegacyVersion()) == 2 && must(features.NewFeaturesLegacyTag()).Id() == 7, "qualified names")
	check(features.FeaturesLegacyModeOff.String() == "off", "qualified enum")
	check(must(must(features.NewCircle(2)).Diameter()) == 4, "extension method")
	{
		ledger := must(features.NewLedger())
		cp := must(ledger.Latest())
		check(cp.SetValue(5) == nil && ledger.Stamp().Value() == 1, "copy type")
	}
	{
		aim := must(features.NewAim())
		check(aim.Origin().SetXField(2) == nil && aim.Target().Point().X() == 2, "nested borrowed fields")
		_, setter := reflect.TypeOf(aim.Target()).MethodByName("SetPoint")
		_, getter := reflect.TypeOf(aim.Target()).MethodByName("Point")
		check(getter && !setter, "read-only field has no setter")
	}

	// Errors, closing and garbage collection.
	_, err = features.NewCircle(-1)
	check(kind(err) == features.InvalidArgument, "exception kind")
	_, err = features.AreaOf(nil)
	fails(err, "argument 'shape'")
	check(kind(err) == features.BindingError, "binding error kind")
	_, err = features.AreaOf((*features.Circle)(nil))
	fails(err, "argument 'shape'")
	{
		x := must(features.NewCircle(1))
		check(must(x.Area()) == 3 && !x.IsClosed(), "before close")
		x.Close()
		check(x.IsClosed(), "closed")
		_, err := x.Area()
		fails(err, "closed")
		x.Close()
	}
	func() {
		defer func() { check(recover() != nil, "Must panics on an error") }()
		features.Must(features.NewCircle(-1))
	}()
	for i := 1; i <= 200; i++ {
		tmp := must(features.MakeShape(features.KindSquare, float64(i)))
		check(must(tmp.Area()) == float64(i*i), "many objects")
		if i%2 == 0 {
			tmp.Close()
		}
	}
	collect()

	// Directors: Go implementations of virtual methods.
	{
		t := &triangle{base: 4, height: 3}
		shape := must(features.NewShapeFrom(t))
		check(t.Shape == shape, "the embedded field is set")
		check(t.Area() == 6 && must(features.AreaOf(t)) == 6 && must(shape.TwiceArea()) == 12 && must(features.Describe(t)) == "triangle:7",
			"Go override called from C++")
		check(must(features.TotalArea([]features.ShapeLike{t, must(features.NewCircle(1)), must(features.NewSquareSide(2))})) == 13 &&
			must(t.LabelWithPrefix("@")) == "@triangle", "mixed overrides")
		impl, ok := features.Cast[*triangle](shape)
		check(ok && impl == t, "Cast returns the Go implementation")
	}
	_, err = features.AreaOf(must(features.NewShapeFrom(&incomplete{})))
	fails(err, "pure virtual method")
	_, err = features.AreaOf(must(features.NewShapeFrom(&failing{})))
	fails(err, "from Go")
	check(kind(err) == features.CallbackError, "an override's error is a callback error")
	_, err = features.NewShapeFrom(nil)
	fails(err, "abstract")
	{
		l := &loud{}
		must(features.NewCircleFrom(l, 2))
		check(must(features.Describe(l)) == "CIRCLE:0" && must(l.Area()) == 12, "the embedded object runs the C++ method")
		lr := &louder{}
		must(features.NewCircleFrom(lr, 1))
		check(lr.Circle != nil && must(features.Describe(lr)) == "circle!:0", "deeper Go embedding")
	}
	canvas = must(features.NewCanvas())
	func() {
		u := &unitShape{tag: "kept"}
		check(canvas.Add(must(features.NewShapeFrom(u))) == nil && u.Area() == 1, "Go-implemented object owned by C++")
	}()
	collect()
	check(canvas.Share(must(features.NewSquareSide(2))) == nil, "share")
	check(must(canvas.Total()) == 5 && must(canvas.Size()) == 2, "C++ keeps the Go object alive")
	{
		back := must(canvas.At(0))
		u, ok := features.Cast[*unitShape](back)
		check(ok && u.tag == "kept" && back == must(canvas.At(0)) && back == u.Shape, "the same Go object comes back")
	}
	check(canvas.Clear() == nil, "clear")
	collect()
	check(canvas.Share(must(features.NewShapeFrom(&unitShape{}))) == nil, "share a Go-implemented object")
	collect()
	check(must(canvas.Total()) == 1, "shared Go-implemented object")
	check(canvas.Clear() == nil, "clear")
	collect()
	{
		sm := &summer{}
		must(features.NewVisitorFrom(sm))
		check(must(features.RunVisitor(sm, []float64{1, 2})) == 30 && slices.Equal(sm.seen, []float64{1, 2}), "callback interface")
	}
	check(must(features.RunVisitor(must(features.NewVisitor()), []float64{1, 2})) == 3, "native default implementation")

	// Godoc carries the C++ documentation.
	{
		d := doc("func Add(a int)")
		check(strings.Contains(d, " Add adds three integers, Socha & Dorigo style."), "summary starts with the name")
		check(strings.Contains(d, "  - a: first term"), "parameter list")
		check(strings.Contains(d, " It returns the sum."), "returns")
		check(strings.Contains(d, "# Example") && strings.Contains(d, "\tint total = add(1);"), "heading and code block")
		check(strings.Contains(d, "See also [Pick] and [Sum]."), "doc links")
		check(strings.Contains(d, "[AddWithB] and [AddWithC]"), "longer variants linked")
	}
	check(strings.Contains(doc("func NewCircle(radius float64)"), "[InvalidArgument] (C++ std::invalid_argument)"), "errors")
	check(strings.Contains(doc("func (v *Vec2) X() float64"), "Horizontal component."), "field documentation")
	check(strings.Contains(doc("type Shape struct"), "[NewShapeFrom]"), "class remarks")
	check(strings.Contains(doc("func NewShapeFrom("), "Area (required: pure virtual in C++)"), "virtual methods listed")

	// Policy modules: features::data functions live in the subpackage data.
	{
		u := must(data.Unit())
		check(u.X() == 1 && u.Y() == 0, "subpackage result")
	}
	check(must(data.Scaled(3)) == 6 && must(data.ScaledFloat64(2.5)) == 6.25, "subpackage overloads")
	check(must(data.Repeat("ab")) == "abab" && must(features.Repeat('x', 3)) == "xxx", "subpackage names are separate")
	check(strings.Contains(docIn(dataSource, "func Unit()"), "Mapped by the policy's modules"), "subpackage docs")
	collect()
	fmt.Printf("Go full API passed (%d checks)\n", checks)
}
