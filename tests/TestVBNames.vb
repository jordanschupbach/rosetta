' The Visual Basic API for tests/vb_full_names.hpp: classes named like the .NET types generated code
' and the runtime use, VB keywords, names that differ only in case, and members a base class declares.
Option Strict On
Option Explicit On
Option Infer On
Imports System
Imports System.Linq
Imports Shadow

Module TestVBNames
    Private checks As Integer
    Sub Check(ok As Boolean, what As String)
        checks += 1
        If Not ok Then Throw New InvalidOperationException("check failed: " & what)
    End Sub

    Function Main() As Integer
        ' .NET names the module's classes take.
        Check(Functions.Twice(New System.Numerics.Complex(1, 2)) = New System.Numerics.Complex(2, 4), "std::complex is System.Numerics.Complex")
        Using c As New Complex()
            Check(c.Norm() = 25 AndAlso Functions.NormOf(c) = 25, "the module's own Complex")
        End Using
        Using l As New List()
            Check(l.Size() = 3, "the module's own List")
        End Using
        Check(Functions.Range(3).SequenceEqual(New Double() {0, 1, 2}), "std::vector is System.Collections.Generic.List")
        Dim counts = Functions.Counts(New String() {"a", "b", "a"})
        Check(counts("a") = 2 AndAlso counts("b") = 1, "std::map is a Dictionary")
        Check(Functions.Unique(New Integer() {3, 1, 3}).SetEquals(New Integer() {1, 3}), "std::set is a HashSet")
        Check(Functions.Apply(Function(x) x * 10, 1.5) = 15, "std::function is System.Func")
        Dim seen = 0
        Functions.Each(New Integer() {1, 2, 3}, Sub(v) seen += v)
        Check(seen = 6, "a void std::function is System.Action")
        Check(Functions.Pair().Equals((1, 2.5)), "std::pair is a tuple")
        Try
            Functions.Fail(-1)
            Check(False, "no exception")
        Catch e As NativeException
            Check(e.Kind = "invalid_argument", "NativeException is a System.Exception")
        End Try
        Using f As New Func()
            f.Calls = 2
            Check(f.Calls = 2, "the module's own Func")
        End Using
        Using e As New [Error]()
            Check(e.Code = 7 AndAlso Functions.CodeOf(e) = 7, "a class named like a keyword")
        End Using

        ' Enumerators that differ only in case are distinct values.
        Check(Functions.ModeValue(Mode.on) = 0 AndAlso Functions.ModeValue(Mode.On_) = 1 AndAlso Functions.ModeValue(Mode.Next) = 2, "enum labels by value")

        ' Keywords and case-insensitive collisions inside a class.
        Using cursor As New Cursor()
            Check(cursor.Next() = 1 AndAlso cursor.Next() = 2, "a method named Next")
            Check(cursor.Step(2, 3) = 8, "keyword parameter names")
            Check(cursor.Count() = 20 AndAlso cursor.CountField = 2, "a field and a method named alike")
            cursor.Stop = True
            Check(cursor.Stop, "a property named Stop")
            Check(cursor(1) = 9 AndAlso cursor.Element(2) = 10 AndAlso cursor.Item = 5, "the indexer next to a member Item")
            Check(cursor.Size(4) = 8 AndAlso cursor.Lookup(1) = 2, "parameters named like their function and the runtime")
            cursor.Value = 3
            Check(cursor.Value = 3, "a property named Value")
            Check(cursor.ToString() = "cursor@8" AndAlso cursor.Equals(cursor) AndAlso Not cursor.Equals(New Cursor()), "ToString and Equals")
            Check(Not cursor.Equals(CObj("text")), "Object.Equals still applies")
        End Using

        ' Members a base class also declares.
        Using d As New Derived()
            Dim b As Base = d
            Check(d.Describe() = "derived" AndAlso b.Describe() = "derived", "Overrides")
            Check(d.Name() = "derived" AndAlso b.Name() = "base", "Overloads hides by signature")
            Check(d.Scale(2.0) = 4.0 AndAlso d.Scale(2) = 2, "Overloads keeps the base overloads")
            Check(d.Weight = 2 AndAlso b.Weight() = 1, "Shadows across member kinds")
        End Using
        Console.WriteLine("Visual Basic names passed (" & checks & " checks)")
        Return 0
    End Function
End Module
