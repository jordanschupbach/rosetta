// Checks the Go spellings the full Go backend gives to C++ names that are Go keywords, predeclared
// identifiers, or that collide once exported, plus the less common type mappings.
package main

import (
	"errors"
	"fmt"
	"os"

	"gonames"
)

var checks, failures int

func check(name string, ok bool) {
	checks++
	if !ok {
		failures++
		fmt.Fprintln(os.Stderr, "FAILED:", name)
	}
}

func eq[T comparable](name string, got T, err error, want T) {
	check(fmt.Sprintf("%s: got %v (%v), want %v", name, got, err, want), err == nil && got == want)
}

// gauge overrides Gauge::read.
type gauge struct{}

func (gauge) Read(x float64) float64 { return 10 * x }

// embeddedGauge calls the C++ read of the object it was created as (offset 5, scale 3), minus 1.
type embeddedGauge struct{ *gonames.Gauge }

func (e *embeddedGauge) Read(x float64) float64 { return gonames.Must(e.Gauge.Read(x)) - 1 }

func main() {
	v, err := gonames.Func(1)
	eq("Func", v, err, 2)
	v, err = gonames.Range(3, 4)
	eq("Range", v, err, 12)
	v, err = gonames.Go(1)
	eq("Go", v, err, 11)
	v, err = gonames.Select(2)
	eq("Select", v, err, 6)
	v, err = gonames.Chan(5, 2)
	eq("Chan", v, err, 3)
	s, err := gonames.Default("x")
	eq("Default", s, err, "default:x")

	v, err = gonames.Len(2)
	eq("Len", v, err, 4)
	v, err = gonames.Cap(2)
	eq("Cap", v, err, 6)
	s, err = gonames.Error3("e")
	eq("Error3", s, err, "error:e")
	v, err = gonames.Make(2, 3)
	eq("Make", v, err, 5)
	v, err = gonames.Close(4)
	eq("Close", v, err, -4)
	v, err = gonames.Copy(9)
	eq("Copy", v, err, 9)
	v, err = gonames.Append(1)
	eq("Append", v, err, 101)
	v, err = gonames.New(2)
	eq("New", v, err, 8)
	v, err = gonames.Rosetta(1)
	eq("Rosetta", v, err, 8)

	v, err = gonames.Foo()
	eq("Foo", v, err, 1)
	v, err = gonames.Foo2()
	eq("Foo2", v, err, 2)
	v, err = gonames.SnakeCaseName()
	eq("SnakeCaseName", v, err, 3)
	v, err = gonames.SnakeCaseName2()
	eq("SnakeCaseName2", v, err, 4)
	v, err = gonames.Leading()
	eq("Leading", v, err, 5)
	v, err = gonames.Trailing()
	eq("Trailing", v, err, 6)

	t, err := gonames.NewTypeWithRange(4)
	check("NewTypeWithRange", err == nil)
	check("Type.Func", t.Func() == 5)
	check("Type.SetFunc", t.SetFunc(7) == nil && t.Func() == 7)
	check("Type.Len", t.Len() == 2)
	v, err = t.Range(3)
	eq("Type.Range", v, err, 6)
	v, err = t.Range2()
	eq("Type.Range2", v, err, 99)
	v, err = t.Close2()
	eq("Type.Close2", v, err, 11)
	s, err = t.String2()
	eq("Type.String2", s, err, "type")
	v, err = gonames.TypeMake()
	eq("TypeMake", v, err, 42)
	t.Close()
	check("Type.Close", t.IsClosed())
	_, err = t.Range(1)
	var e *gonames.Error
	check("closed object error", errors.As(err, &e))
	t2, err := gonames.NewType()
	check("NewType", err == nil && t2.Func() == 1)

	e2, err := gonames.NewError2()
	check("Error2", err == nil && e2.Code() == 3 && e2.String() == "Error(3)")
	p, err := gonames.NewPoint()
	check("Point", err == nil && p.X() == 1 && p.Y() == 2)

	k, err := gonames.NextKeyword(gonames.KeywordRange)
	eq("NextKeyword", k, err, gonames.KeywordType)
	check("Keyword.String", gonames.KeywordFunc.String() == "func")
	b, err := gonames.Biggest()
	eq("Biggest", b, err, gonames.BigHuge)
	check("Big.String", gonames.BigHuge.String() == "huge" && gonames.Big(7).String() == "7")

	z, err := gonames.Conjugate(complex(1, 2))
	eq("Conjugate", z, err, complex(1, -2))
	words, err := gonames.UniqueWords([]string{"a", "b", "a"})
	_, hasA := words["a"]
	check("UniqueWords", err == nil && len(words) == 2 && hasA)
	v, err = gonames.CountSet(map[int]struct{}{1: {}, 2: {}, 3: {}})
	eq("CountSet", v, err, 3)
	tr, err := gonames.Triple(5)
	eq("Triple", tr, err, gonames.Tuple3[int, string, float64]{V0: 5, V1: "5", V2: 2.5})
	bk, err := gonames.Buckets([]float64{1.5, 1.25, 2})
	check("Buckets", err == nil && len(bk) == 2 && len(bk[1]) == 2 && bk[2][0] == 2)
	u, err := gonames.Big2()
	eq("Big2", u, err, uint64(18446744073709551615))
	n, err := gonames.Negate(-9007199254740993)
	eq("Negate", n, err, int64(9007199254740993))
	by, err := gonames.LowByte(0x1234)
	eq("LowByte", by, err, uint8(0x34))

	v, err = gonames.Checked(5)
	eq("Checked", v, err, 5)
	_, err = gonames.Checked(500)
	check("Checked length", errors.As(err, &e) && e.Kind == gonames.LengthError && e.Message == "too long")
	_, err = gonames.Checked(-1)
	check("Checked domain", errors.As(err, &e) && e.Kind == gonames.DomainError)
	check("Quiet", gonames.Quiet(1) == 2)
	check("Must", gonames.Must(gonames.Func(2)) == 3)

	// Every director constructor variant takes impl: defaults and overloads included.
	g := gonames.Must(gonames.NewGaugeFrom(&gauge{}))
	eq("NewGaugeFrom", gonames.Must(gonames.ReadGauge(g, 3)), nil, 30.0)
	g = gonames.Must(gonames.NewGaugeFromWithOffset(&gauge{}, 5))
	eq("NewGaugeFromWithOffset", gonames.Must(gonames.ReadGauge(g, 3)), nil, 30.0)
	g = gonames.Must(gonames.NewGaugeFromWithScale(&gauge{}, 5, 3))
	eq("NewGaugeFromWithScale", gonames.Must(gonames.ReadGauge(g, 3)), nil, 30.0)
	g = gonames.Must(gonames.NewGaugeFromLabel(&gauge{}, "abc"))
	eq("NewGaugeFromLabel", gonames.Must(gonames.ReadGauge(g, 3)), nil, 30.0)
	base := &embeddedGauge{}
	g = gonames.Must(gonames.NewGaugeFromWithScale(base, 5, 3))
	eq("NewGaugeFromWithScale base", gonames.Must(gonames.ReadGauge(g, 3)), nil, 13.0)
	g = gonames.Must(gonames.NewGaugeWithScale(5, 3))
	eq("NewGaugeWithScale", gonames.Must(gonames.ReadGauge(g, 3)), nil, 14.0)

	if failures > 0 {
		fmt.Fprintf(os.Stderr, "%d of %d checks failed\n", failures, checks)
		os.Exit(1)
	}
	fmt.Printf("Go full names passed (%d checks)\n", checks)
}
