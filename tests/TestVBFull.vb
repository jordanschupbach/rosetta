' Visual Basic API (--language visual-basic) on the library-shaped example shared with the Python,
' R6, Lua, Common Lisp, Julia and C# backends. Argument: the generated interface (Features.vb), for
' the documentation checks; the library is found through FEATURES_VB_LIBRARY.
Option Strict On
Option Explicit On
Option Infer On
Imports System
Imports System.Collections.Generic
Imports System.IO
Imports System.Linq
Imports System.Threading.Tasks
Imports Features
Imports Features.Functions

' Directors: VB subclasses implement C++ virtual methods.
Class Triangle
    Inherits Shape
    Public Base, Height As Double
    Public Sub New(b As Double, h As Double)
        Base = b
        Height = h
    End Sub
    Public Overrides Function Area() As Double
        Return Base * Height / 2
    End Function
    Public Overrides Function Name() As String
        Return "triangle"
    End Function
End Class
Class Incomplete
    Inherits Shape
End Class
Class Failing
    Inherits Shape
    Public Overrides Function Area() As Double
        Throw New InvalidOperationException("from VB")
    End Function
End Class
' MyBase.Name() runs the native implementation rather than dispatching back to the override.
Class Loud
    Inherits Circle
    Public Sub New(radius As Double)
        MyBase.New(radius)
    End Sub
    Public Overrides Function Name() As String
        Return MyBase.Name().ToUpperInvariant()
    End Function
End Class
Class Noisy
    Inherits Circle
    Public Sub New(radius As Double)
        MyBase.New(radius)
    End Sub
    Public Overrides Function Name() As String
        Return MyBase.Name().ToLowerInvariant() & "!"
    End Function
End Class
Class Louder
    Inherits Noisy
    Public Sub New(radius As Double)
        MyBase.New(radius)
    End Sub
End Class
Class UnitShape
    Inherits Shape
    Public Tag As Object
    ' VB names are case-insensitive: C++ finds this override although it is spelled differently.
    Public Overrides Function AREA() As Double
        Return 1
    End Function
End Class
Class Summer
    Inherits Visitor
    Public Seen As List(Of Double)
    Public Overrides Function Visit(value As Double) As Double
        Return value * 10
    End Function
    Public Overrides Sub Finished(values As IEnumerable(Of Double))
        Seen = values.ToList()
    End Sub
End Class

Module Program
    Private checks As Integer
    Private source As String

    Sub Check(ok As Boolean, what As String)
        checks += 1
        If Not ok Then Throw New Exception("check failed: " & what)
    End Sub
    Sub Fails(action As Action, pattern As String)
        checks += 1
        Dim message As String = Nothing
        Try
            action()
        Catch ex As Exception
            message = ex.Message
        End Try
        If message Is Nothing Then Throw New Exception("expected an error containing """ & pattern & """, got none")
        If Not message.Contains(pattern) Then Throw New Exception("expected an error containing """ & pattern & """, got: " & message)
    End Sub
    Sub Collect()
        For i = 1 To 2
            GC.Collect()
            GC.WaitForPendingFinalizers()
        Next
    End Sub
    ' The ''' block right above the first line containing `declaration`.
    Function Doc(declaration As String) As String
        Dim lines = source.Split(ControlChars.Lf)
        Dim at = Array.FindIndex(lines, Function(line) line.Contains(declaration) AndAlso Not line.TrimStart().StartsWith("'''"))
        If at < 0 Then Throw New Exception("not declared: " & declaration)
        Dim block As New List(Of String)()
        Dim i = at - 1
        While i >= 0 AndAlso lines(i).TrimStart().StartsWith("'''")
            block.Insert(0, lines(i).TrimStart().Substring(3).Trim())
            i -= 1
        End While
        Return String.Join(ControlChars.Lf, block)
    End Function

    Function Main(args As String()) As Integer
        source = File.ReadAllText(args(0))

        ' Classes, inheritance and virtual dispatch.
        Dim c1 As New Circle(2)
        Check(c1.Area() = 12 AndAlso c1.Name() = "circle" AndAlso c1.TwiceArea() = 24 AndAlso c1.Radius() = 2, "circle methods")
        Check(c1.ToString().Contains("Circle"), "ToString names the class")
        Check(TypeOf c1 Is Shape AndAlso TypeOf c1 Is NativeObject, "class hierarchy")
        Fails(Sub()
                  Dim unused As New Circle(-1)
              End Sub, "negative radius")
        Dim s As New Square()
        Check(s.Side = 1 AndAlso s.Area() = 1, "default constructor")
        s.Side = 3
        Check(s.Area() = 9 AndAlso New Square(2).Area() = 4, "field set")
        Check(AreaOf(s) = 9 AndAlso Describe(c1) = "circle:0", "functions on classes")
        ' Enums (and names that differ only in case: the enum Kind and the method Kind).
        Check(c1.Kind() = Kind.circle AndAlso s.Kind() = Kind.square, "enum results")
        Check(Filled(Fill.solid) AndAlso Not Filled(Fill.None), "enum arguments")
        Check([Enum].GetNames(GetType(Kind)).Contains("custom"), "enum members")
        Check(Shape.SidesUnknown = -1 AndAlso Math.Abs(Golden - 1.618) < 0.000000000001, "constants")

        ' Most-derived results, ownership transfer and shared ownership.
        Dim made = MakeShape(Kind.circle, 1)
        Check(TypeOf made Is Circle AndAlso DirectCast(made, Circle).Radius() = 1, "most-derived result")
        Dim canvas As New Canvas()
        canvas.Add(MakeShape(Kind.square, 2))
        canvas.Share(SharedSquare(3))
        Dim transferred As New Circle(1)
        canvas.Add(transferred)
        Fails(Sub() transferred.Area(), "closed")
        Check(canvas.Size() = 3UL AndAlso canvas.Total() = 4 + 9 + 3, "ownership transfer")
        Dim first = canvas.At(0)
        Check(TypeOf first Is Square AndAlso first.Area() = 4, "borrowed element")
        Dim sharedOne = SharedSquare(2)
        canvas.Share(sharedOne)
        Check(sharedOne.Area() = 4 AndAlso canvas.Total() = 20, "shared ownership")
        Check(TotalArea(New Shape() {New Circle(1), sharedOne}) = 7, "vector of objects")

        ' Overload resolution by VB type, checked again in C++.
        Check(Pick(1) = "int" AndAlso Pick(1.5) = "double" AndAlso Pick(2.0) = "double" AndAlso Pick("x") = "string", "scalar overloads")
        Check(Pick(New Double() {1, 2}) = "vector" AndAlso Pick(s) = "shape", "container and class overloads")
        Check(Add(1) = 111 AndAlso Add(1, 2) = 103 AndAlso Add(1, 2, 3) = 6, "default arguments")

        ' Containers.
        Dim counts = CountWords(New String() {"a", "b", "a"})
        Check(counts("a") = 2 AndAlso counts("b") = 1, "dictionary result")
        Check(MinMax(New Double() {3, 1, 2}).Equals((1.0, 3.0)), "tuple result")
        Fails(Sub() MinMax(Array.Empty(Of Double)()), "empty input")
        Check(Find(New Integer() {5, 7}, 7).Value = 1 AndAlso Not Find(New Integer() {5, 7}, 9).HasValue, "optional result")
        Dim tr = Transpose(New Double()() {New Double() {1, 2, 3}, New Double() {4, 5, 6}})
        Check(tr.Count = 3 AndAlso tr(0).SequenceEqual(New Double() {1, 4}), "nested vectors")
        Check(ByteSum(New Byte() {1, 2, 250}) = 253 AndAlso BytesOf("AZ").SequenceEqual(New Byte() {65, 90}), "bytes")
        Dim cs = Circles(3)
        Check(cs.Count = 3 AndAlso cs(2).Radius() = 3, "vector of class results")
        Check(Repeat("z"c, 3) = "zzz" AndAlso Initial("hello") = "h"c, "chars")
        ' Mutable reference parameters are ByRef arguments.
        Dim values As New List(Of Double) From {1, 2}
        Scale(values, 3)
        Check(values.SequenceEqual(New Double() {3, 6}), "ByRef argument written back")
        Fails(Sub() Repeat("z"c, -1), "negative count")
        Check(Defaults() = "x*1-1.500000", "all defaults")
        Check(Doc("Public Shared Function Defaults(kind As Kind, label As String, loud As Boolean) As String").Contains("<c>weight = 0.5</c>"), "omitted defaults named")

        ' Callbacks both ways.
        Check(Apply(Function(x) x * 2, 21) = 42, "callback")
        Fails(Sub() Apply(Function(x As Double) As Double
                              Throw New InvalidOperationException("inside callback")
                          End Function, 1), "inside callback")
        ' The VB exception comes back as itself.
        Try
            Apply(Function(x As Double) As Double
                      Throw New DivideByZeroException()
                  End Function, 1)
            Check(False, "callback exception")
        Catch ex As DivideByZeroException
            Check(True, "callback exception type")
        End Try
        Dim plus = Adder(40)
        Check(plus(2) = 42, "native function as delegate")
        Check(Apply(Function(x) Adder(1)(CInt(x)), 3) = 4, "native function inside a callback")
        Check(Task.Run(Function() Apply(Function(x) x + 1, 1)).Result = 2, "callback from another thread")

        ' Value type operators, chaining and borrowed references.
        Dim v As New Vec2(1, 2)
        Dim w = v + New Vec2(3, 4)
        Check(w.X = 4 AndAlso w.Y = 6 AndAlso (v * 2).Y = 4 AndAlso (-v).X = -1 AndAlso (v - w).X = -3, "operators")
        Check(v = New Vec2(1, 2) AndAlso Not (v = w) AndAlso v <> w, "equality operators")
        Check(v(1) = 2 AndAlso v.Item(0) = 1 AndAlso v.Inner(New Vec2(1, 1)) = 3, "default property")
        Fails(Sub()
                  Dim unused = v(2)
              End Sub, "Vec2 index")
        Dim chained = v.SetX(10).SetY(20)
        Check(v.X = 10 AndAlso v.Y = 20 AndAlso chained.X = 10, "chaining")
        Dim nothingVec As Vec2 = Nothing
        Check(Not (v = Nothing) AndAlso nothingVec = Nothing AndAlso v <> Nothing, "Nothing equality")
        Dim seg As New Segment()
        seg.End = New Vec2(3, 4)
        Check(seg.Length2() = 25, "field object set (a keyword-named property)")
        Dim start = seg.First()
        start.X = 3
        Check(seg.Start.X = 3 AndAlso seg.Length2() = 16, "borrowed reference writes through")
        seg.Start.Y = 4
        Check(seg.Length2() = 0, "field object borrowed")
        Check(New Vec2(1, 2).Clone().Y = 2, "clone")
        Check(Sum(New Vec2() {New Vec2(1, 2), New Vec2(3, 4)}) = 10, "vector of values")
        Dim held = New Segment().First()
        Collect()
        Check(held.X = 0, "borrowed reference keeps its owner alive")

        ' Templates, extensions, qualified names and copy types.
        Dim stack As New IntStack()
        stack.Push(1)
        stack.Push(2)
        Check(stack.Size() = 2UL AndAlso stack.Items().SequenceEqual(New Integer() {1, 2}) AndAlso stack.Pop() = 2, "template instance")
        Check(ClampInt(42) = 10 AndAlso ClampInt(3, 5) = 3 AndAlso ClampReal(2.5) = 2.5, "function templates")
        Check(c1.Label() = "#circle" AndAlso c1.Label("@") = "@circle" AndAlso Circle.Unit().Radius() = 1, "extensions")
        Check(FeaturesLegacyVersion() = 2 AndAlso New FeaturesLegacyTag().Id = 7, "qualified names")
        Check([Enum].GetNames(GetType(FeaturesLegacyMode)).Contains("off"), "qualified enum")
        Check(New Circle(2).Diameter() = 4, "extension method")
        Dim ledger As New Ledger()
        Dim copy = ledger.Latest()
        copy.Value = 5
        Check(ledger.Stamp.Value = 1, "copy type")
        Dim aim As New Aim()
        aim.Origin.X = 2
        Check(aim.Target.Point.X = 2, "nested borrowed fields")
        Check(Not GetType(Target).GetProperty("Point").CanWrite, "read-only field has no setter")

        ' Errors, disposing and garbage collection.
        Try
            Dim unused As New Circle(-1)
        Catch ex As NativeException
            Check(ex.Kind = "invalid_argument", "exception kind")
        End Try
        Fails(Sub() AreaOf(Nothing), "argument 'shape'")
        Try
            AreaOf(Nothing)
        Catch ex As NativeException
            Check(ex.Kind = "binding", "binding error kind")
        End Try
        Dim x1 As New Circle(1)
        Check(x1.Area() = 3, "before dispose")
        x1.Dispose()
        Check(x1.IsDisposed, "disposed")
        Fails(Sub() x1.Area(), "closed")
        x1.Dispose()
        For i = 1 To 200
            Using tmp = MakeShape(Kind.square, i)
                Check(tmp.Area() = i * i, "many objects")
            End Using
        Next
        Collect()

        ' Directors.
        Dim t As New Triangle(4, 3)
        Check(t.Area() = 6 AndAlso AreaOf(t) = 6 AndAlso t.TwiceArea() = 12 AndAlso Describe(t) = "triangle:7", "VB override called from C++")
        Check(TotalArea(New Shape() {t, New Circle(1), New Square(2)}) = 13 AndAlso t.Label("@") = "@triangle", "mixed overrides")
        Fails(Sub() AreaOf(New Incomplete()), "pure virtual method")
        Fails(Sub() AreaOf(New Failing()), "from VB")
        Check(Describe(New Loud(1)) = "CIRCLE:0" AndAlso New Loud(2).Area() = 12, "base call runs the C++ method")
        Check(Describe(New Louder(1)) = "circle!:0", "deeper subclasses")
        canvas = New Canvas()
        AddUnit(canvas)
        Collect()
        canvas.Share(New Square(2))
        Check(canvas.Total() = 5 AndAlso canvas.Size() = 2UL, "C++ keeps the VB object alive")
        Dim back = canvas.At(0)
        Check(TypeOf back Is UnitShape AndAlso CStr(DirectCast(back, UnitShape).Tag) = "kept" AndAlso back Is canvas.At(0), "the same VB object comes back")
        back = Nothing
        canvas.Clear()
        Collect()
        canvas.Share(New UnitShape())
        Collect()
        Check(canvas.Total() = 1, "shared subclass object")
        canvas.Clear()
        Collect()
        Dim summer As New Summer()
        Check(RunVisitor(summer, New Double() {1, 2}) = 30 AndAlso summer.Seen.SequenceEqual(New Double() {1, 2}), "callback interface")
        Check(RunVisitor(New Visitor(), New Double() {1, 2}) = 3, "native default implementation")

        ' XML documentation carries the C++ documentation.
        Dim d = Doc("Public Shared Function Add(a As Integer, b As Integer, c As Integer) As Integer")
        Check(d.Contains("Adds three integers, Socha &amp; Dorigo style."), "summary")
        Check(d.Contains("<param name=""a"">first term</param>"), "parameter")
        Check(d.Contains("<returns>the sum</returns>"), "returns")
        Check(Doc("Public Sub New(radius As Double)").Contains("<exception cref=""NativeException"">"), "exceptions")
        Check(Doc("Public Property X As Double").Contains("Horizontal component."), "field documentation")
        Check(Doc("Partial Public Class Shape").Contains("inherit it in a Visual Basic class"), "class remarks")

        ' Policy modules: features::data functions live in the class data.
        Dim u = data.Unit()
        Check(u.X = 1 AndAlso u.Y = 0, "submodule result")
        Check(data.Scaled(3) = 6 AndAlso data.Scaled(2.5) = 6.25, "submodule overloads")
        Check(data.Repeat("ab") = "abab" AndAlso Repeat("x"c, 3) = "xxx", "submodule names are separate")
        Check(Doc("Public Shared Function Unit() As Vec2").Contains("Mapped by the policy's modules"), "submodule docs")
        Collect()
        Console.WriteLine("Visual Basic API passed (" & checks & " checks)")
        Return 0
    End Function

    ' In its own method, so that no local keeps the VB object alive.
    Sub AddUnit(canvas As Canvas)
        Dim unit As New UnitShape With {.Tag = "kept"}
        canvas.Add(unit)
        Check(unit.Area() = 1, "subclass object owned by C++")
    End Sub
End Module
