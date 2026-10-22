// Runtime tests for the generated Go packages, built as a module of the
// workspace that lists them.
package rosetta_go_tests

import (
	"errors"
	"math"
	"reflect"
	"runtime"
	"strings"
	"sync"
	"testing"
	"time"

	D "go_demo"
	N "go_names"
	V "go_verify"
)

// result carries a call's two results, since Go passes multiple results
// only as a call's entire argument list.
type result[T any] struct {
	value T
	err   error
}

func r[T any](value T, err error) result[T] { return result[T]{value, err} }

func must[T any](t *testing.T, call result[T]) T {
	t.Helper()
	if call.err != nil {
		t.Fatalf("unexpected error: %v", call.err)
	}
	return call.value
}

func equal[T comparable](t *testing.T, label string, got T, want T) {
	t.Helper()
	if got != want {
		t.Errorf("%s: got %v, want %v", label, got, want)
	}
}

// fails checks that err is a Rosetta error of KIND whose message contains FRAGMENT.
func fails(t *testing.T, label string, err error, kind string, fragment string) {
	t.Helper()
	var d *D.Error
	var v *V.Error
	var n *N.Error
	var actual, message string
	switch {
	case errors.As(err, &d):
		actual, message = d.Kind.String(), d.Message
	case errors.As(err, &v):
		actual, message = v.Kind.String(), v.Message
	case errors.As(err, &n):
		actual, message = n.Kind.String(), n.Message
	default:
		t.Errorf("%s: expected a %s error, got %v", label, kind, err)
		return
	}
	if actual != kind || !strings.Contains(message, fragment) {
		t.Errorf("%s: expected %s error containing %q, got %s %q", label, kind, fragment, actual, message)
	}
}

func second[T any](_ T, err error) error { return err }

// eventually waits for cleanups, which run on their own goroutine after a collection.
func eventually(condition func() bool) bool {
	for i := 0; i < 200; i++ {
		if condition() {
			return true
		}
		runtime.GC()
		time.Sleep(10 * time.Millisecond)
	}
	return false
}

func TestScalars(t *testing.T) {
	equal(t, "schar", must(t, r(V.EchoSchar(math.MinInt8))), math.MinInt8)
	equal(t, "schar", must(t, r(V.EchoSchar(math.MaxInt8))), math.MaxInt8)
	equal(t, "uchar", must(t, r(V.EchoUchar(math.MaxUint8))), math.MaxUint8)
	equal(t, "char", must(t, r(V.EchoChar(255))), 255)
	equal(t, "short", must(t, r(V.EchoShort(math.MinInt16))), math.MinInt16)
	equal(t, "ushort", must(t, r(V.EchoUshort(math.MaxUint16))), math.MaxUint16)
	equal(t, "int", must(t, r(V.EchoInt(math.MinInt32))), math.MinInt32)
	equal(t, "int", must(t, r(V.EchoInt(math.MaxInt32))), math.MaxInt32)
	equal(t, "uint", must(t, r(V.EchoUint(math.MaxUint32))), math.MaxUint32)
	equal(t, "long", must(t, r(V.EchoLong(math.MinInt64))), math.MinInt64)
	equal(t, "ll", must(t, r(V.EchoLl(math.MaxInt64))), math.MaxInt64)
	equal(t, "ulong", must(t, r(V.EchoUlong(math.MaxUint64))), math.MaxUint64)
	equal(t, "ull", must(t, r(V.EchoUll(math.MaxUint64))), math.MaxUint64)
	equal(t, "too large", must(t, r(D.TooLarge())), 9007199254740993)
	equal(t, "add", must(t, r(D.Add(20, 22))), 42)
	equal(t, "float", must(t, r(V.EchoFloat(1.25))), 1.25)
	equal(t, "double", must(t, r(V.EchoDouble(math.MaxFloat64))), math.MaxFloat64)
	equal(t, "half", must(t, r(D.Half(3))), 1.5)
	// Floats pass unchanged, including infinities and NaN.
	equal(t, "infinity", must(t, r(V.EchoDouble(math.Inf(1)))), math.Inf(1))
	if r := must(t, r(V.EchoDouble(math.NaN()))); !math.IsNaN(r) {
		t.Errorf("NaN: got %v", r)
	}
	if r := must(t, r(V.NonfiniteResult())); !math.IsInf(r, 0) && !math.IsNaN(r) {
		t.Errorf("nonfinite result: got %v", r)
	}
	equal(t, "bool", must(t, r(V.EchoBool(true))), true)
	equal(t, "negate", must(t, r(D.Negate(true))), false)
	// Strings are bytes in both directions.
	for _, text := range []string{"", "world", "世界😀", "a\x00b", strings.Repeat("long", 10000), "\xff\xfe"} {
		equal(t, "string", must(t, r(V.EchoString(text))), text)
	}
	equal(t, "embedded nul", must(t, r(V.EmbeddedNul())), "a\x00b")
	equal(t, "greet", must(t, r(D.Greet("Go"))), "Hello, Go!")
	equal(t, "defaulted", must(t, r(V.Defaulted())), 42)
	equal(t, "defaulted", must(t, r(V.Defaulted(12))), 12)
	fails(t, "too many", second(V.Defaulted(1, 2)), "argument", "too many optional arguments")
	equal(t, "alias", must(t, r(V.AliasIdentity(42))), 42)
	if err := V.DoNothing(); err != nil {
		t.Errorf("void: %v", err)
	}
	for i := 0; i < 20; i++ {
		fails(t, "fail", D.Fail(), "native", "native failure")
		fails(t, "unknown", V.UnknownError(), "native", "unknown C++ exception")
		fails(t, "allocation", V.AllocationError(), "allocation", "allocation")
	}
	err := D.Fail()
	var e *D.Error
	if !errors.As(err, &e) || e.Kind != D.KindNative || e.Function != "r_demo::fail" {
		t.Errorf("errors.As: %#v", err)
	}
	equal(t, "message", err.Error(), "go_demo: r_demo::fail: native failure (native)")
}

func TestNames(t *testing.T) {
	equal(t, "type", must(t, r(N.Type())), 42)
	equal(t, "type", must(t, r(N.Type(7))), 7)
	equal(t, "snake case", must(t, r(N.SnakeCaseName())), 1)
	equal(t, "labels", must(t, r(N.Labels())), 12)
	equal(t, "labels", must(t, r(N.Labels(3))), 32)
	equal(t, "labels", must(t, r(N.Labels(3, 4))), 34)
	fails(t, "labels", second(N.Labels(1, 2, 3)), "argument", "too many optional arguments")
	equal(t, "join", must(t, r(N.Join("a"))), "a-!")
	equal(t, "join", must(t, r(N.Join("a", "+"))), "a+!")
	equal(t, "join", must(t, r(N.Join("a", "+", "?"))), "a+?")
	equal(t, "mixed", must(t, r(N.Mixed(1, 2.5, 3))), 6.5)
	equal(t, "keywords", must(t, r(N.Keywords(1, 2))), 3)
	equal(t, "shadow", must(t, r(N.Shadow(1, 2, 3))), 6)
	equal(t, "seven", must(t, r(N.Seven(1, 2, 3, 4, 5, 6, 7))), 28)
	equal(t, "depth", must(t, r(N.Depth())), 1)
	p := must(t, r(N.NewPoint(1, 2)))
	equal(t, "sum", must(t, r(p.Sum())), 3)
	equal(t, "sum", must(t, r(p.Sum(2))), 6)
	equal(t, "origin", must(t, r(N.PointOrigin())), 0)
	if err := p.SetTag('z'); err != nil {
		t.Fatal(err)
	}
	equal(t, "tag", must(t, r(p.Tag())), 'z')
	equal(t, "weight", must(t, r(p.Weight())), 1.5)
	equal(t, "close", p.Close(), nil)
	equal(t, "default point", must(t, r(must(t, r(N.NewPoint())).Sum())), 0)
	// The zero value and a nil pointer behave as closed objects.
	fails(t, "zero point", second((&N.Point{}).Sum()), "closed", "native object is closed")
	var none *N.Point
	fails(t, "nil point", second(none.Sum()), "closed", "native object is closed")
	equal(t, "nil close", none.Close(), nil)
}

func TestObjects(t *testing.T) {
	live := func() int32 { return must(t, r(D.CounterLive())) }
	equal(t, "none live", live(), 0)
	fails(t, "negative", second(D.NewCounter(-1)), "native", "negative initial")
	equal(t, "failed constructor", live(), 0)
	c := must(t, r(D.NewCounter(20)))
	equal(t, "live", live(), 1)
	equal(t, "add", must(t, r(c.Add(22))), 42)
	equal(t, "value", must(t, r(c.Value())), 42)
	equal(t, "count", must(t, r(c.Count())), 42)
	equal(t, "initial", must(t, r(c.Initial())), 20)
	equal(t, "label", must(t, r(c.Label())), "counter")
	equal(t, "answer", must(t, r(D.CounterAnswer())), 42)
	equal(t, "set count", c.SetCount(7), nil)
	equal(t, "value", must(t, r(c.Value())), 7)
	label := "世界😀\x00"
	equal(t, "set label", c.SetLabel(label), nil)
	equal(t, "label", must(t, r(c.Label())), label)
	// Const fields have no setter.
	if _, ok := reflect.TypeOf(c).MethodByName("SetInitial"); ok {
		t.Error("const field has a setter")
	}
	equal(t, "close", c.Close(), nil)
	equal(t, "closed", live(), 0)
	fails(t, "value closed", second(c.Value()), "closed", "native object is closed")
	fails(t, "set closed", c.SetCount(1), "closed", "native object is closed")
	equal(t, "close twice", c.Close(), nil)
	// An unreachable object is destroyed by its cleanup.
	func() { must(t, r(D.NewCounter(1))) }()
	if !eventually(func() bool { return live() == 0 }) {
		t.Error("cleanup did not run")
	}
	s, w, task := must(t, r(V.NewSeries())), must(t, r(V.NewWorklist(2))), must(t, r(V.NewAsyncTask(1, 60000, false)))
	equal(t, "series live", must(t, r(V.SeriesLive())), 1)
	equal(t, "task live", must(t, r(V.AsyncTaskLive())), 1)
	s.Close()
	w.Close()
	task.Close()
	equal(t, "series closed", must(t, r(V.SeriesLive())), 0)
	equal(t, "workers", must(t, r(V.AsyncTaskWorkers())), 0)
	// Calls from several goroutines are serialized.
	shared := must(t, r(D.NewCounter(0)))
	var group sync.WaitGroup
	for i := 0; i < 8; i++ {
		group.Add(1)
		go func() {
			defer group.Done()
			for k := 0; k < 1000; k++ {
				if _, err := shared.Add(1); err != nil {
					t.Error(err)
					return
				}
			}
		}()
	}
	group.Wait()
	equal(t, "goroutines", must(t, r(shared.Value())), 8000)
	shared.Close()
	// Cleanups run while other goroutines call in.
	for i := 0; i < 4; i++ {
		group.Add(1)
		go func() {
			defer group.Done()
			for k := 0; k < 300; k++ {
				o, err := D.NewCounter(1)
				if err != nil {
					t.Error(err)
					return
				}
				o.Add(1)
				if k%50 == 0 {
					runtime.GC()
				}
			}
		}()
	}
	group.Wait()
	if !eventually(func() bool { return live() == 0 }) {
		t.Errorf("%d objects not cleaned up", live())
	}
}

func TestFeatures(t *testing.T) {
	equal(t, "maximum", must(t, r(V.Maximum(20, 42))), 42)
	equal(t, "template", must(t, r(V.TemplateBox(42))), 42)
	equal(t, "variadic", must(t, r(V.VariadicSum(10, 20, 12))), 42)
	equal(t, "constant", must(t, r(V.ConstantAnswer())), 42)
	equal(t, "constrained", must(t, r(V.ConstrainedSum(20, 22))), 42)
	equal(t, "hierarchy", must(t, r(V.HierarchyValue(42))), 42)
	equal(t, "crosscast", must(t, r(V.HierarchyCrosscast())), true)
	equal(t, "C identity", must(t, r(V.RosettaRCIdentity(42))), 42)
	fails(t, "overflow", second(V.ConstrainedSum(math.MaxInt32, 1)), "range", "overflow")
	fails(t, "series count", second(V.NewSeries(0, -1)), "index", "series count")
	s := must(t, r(V.NewSeries()))
	equal(t, "size", must(t, r(s.Size())), 2)
	equal(t, "get", must(t, r(s.Get())), 20)
	equal(t, "scaled", must(t, r(s.ScaledSum(2))), 82)
	equal(t, "text", must(t, r(s.Text())), "[20, 21]")
	equal(t, "set", s.Set(1, 22), nil)
	equal(t, "scaled", must(t, r(s.ScaledSum(1))), 42)
	equal(t, "get", must(t, r(s.Get(1))), 22)
	fails(t, "get range", second(s.Get(2)), "index", "out of range")
	fails(t, "set range", s.Set(-1, 1), "index", "out of range")
	s.Close()
	s = must(t, r(V.NewSeries(1, 3)))
	equal(t, "text", must(t, r(s.Text())), "[1, 2, 3]")
	s.Close()
	for order, expected := range map[int32][]string{1: {"m", "z", "a"}, 2: {"a", "z", "m"}, 3: {"z", "m", "a"}, 4: {"a", "m", "z"}} {
		q := must(t, r(V.NewWorklist(order)))
		for _, x := range []string{"a", "z", "m"} {
			equal(t, "push", q.Push(x), nil)
		}
		equal(t, "size", must(t, r(q.Size())), 3)
		got := []string{must(t, r(q.Pop())), must(t, r(q.Pop())), must(t, r(q.Pop()))}
		if !reflect.DeepEqual(got, expected) {
			t.Errorf("order %d: got %v, want %v", order, got, expected)
		}
		fails(t, "empty", second(q.Pop()), "index", "empty")
		q.Close()
	}
	fails(t, "worklist order", second(V.NewWorklist(0)), "argument", "unknown worklist order")
	task := must(t, r(V.NewAsyncTask(21, 0, false)))
	equal(t, "wait", must(t, r(task.Wait(2000))), true)
	equal(t, "result", must(t, r(task.Result())), 42)
	task.Close()
	task = must(t, r(V.NewAsyncTask(1, 0, true)))
	equal(t, "wait", must(t, r(task.Wait(2000))), true)
	fails(t, "task failure", second(task.Result()), "native", "async worker failure")
	task.Close()
	task = must(t, r(V.NewAsyncTask(1, 60000, false)))
	equal(t, "ready", must(t, r(task.Ready())), false)
	equal(t, "wait", must(t, r(task.Wait(0))), false)
	equal(t, "cancel", must(t, r(task.Cancel())), true)
	equal(t, "wait", must(t, r(task.Wait(2000))), true)
	fails(t, "cancelled", second(task.Result()), "native", "cancelled")
	task.Close()
	fails(t, "delay", second(V.NewAsyncTask(1, -1, false)), "index", "delay")
	equal(t, "workers", must(t, r(V.AsyncTaskWorkers())), 0)
	// Leave a 60s worker running at exit, bounded by the test timeout.
	must(t, r(V.NewAsyncTask(1, 60000, false)))
}
