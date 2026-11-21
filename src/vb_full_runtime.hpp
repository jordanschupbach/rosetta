#pragma once

// Visual Basic side of the full Visual Basic backend (--language visual-basic): emitted into every
// generated interface. It speaks the value encoding of lisp_full_runtime.hpp (the C++ runtime the
// full C ABI backends share), like the C# runtime it follows (csharp_full_runtime_cs.hpp): VB
// encodes a call's arguments into one buffer, calls the module's single entry point, and decodes
// the result into plain values that Rosetta.Cast converts to the declared VB type.
//
// Visual Basic resolves names in the enclosing namespaces before its Imports, so a generated type
// named like a .NET type (List, Complex, Marshal, ...) would hide it: the runtime names every .NET
// type through Global.System.
//
// @NS@ is the namespace, @C@ the prefix of the native entry points, @LIB@ the shared library's base
// name and @ENV@ the environment variable that may name the library file.
namespace rosetta::detail {
// The public half (namespace @NS@).
inline constexpr const char* vb_full_runtime = R"vb(
''' <summary>
''' A C++ exception, or a failed conversion, in a call to native module @NS@.
''' </summary>
''' <remarks>
''' <see cref="Kind"/> is <c>binding</c> (conversion, overload or disposed-object errors),
''' <c>invalid_argument</c>, <c>out_of_range</c>, <c>domain_error</c>, <c>length_error</c>,
''' <c>arithmetic_error</c>, <c>allocation</c> or <c>native</c>.
''' </remarks>
Public Class NativeException
    Inherits Global.System.Exception
    ''' <summary>What failed: the C++ exception's kind, or <c>binding</c>.</summary>
    Public ReadOnly Property Kind As String
    ''' <summary>A native error of kind <paramref name="errorKind"/>.</summary>
    Public Sub New(errorKind As String, message As String)
        MyBase.New(message)
        Kind = errorKind
    End Sub
End Class

''' <summary>Marks the constructors that wrap an existing C++ object (used by the binding).</summary>
<Global.System.ComponentModel.EditorBrowsable(Global.System.ComponentModel.EditorBrowsableState.Never)>
Public Structure NativeRaw
    Private ReadOnly unused As Byte
End Structure

''' <summary>
''' Base class of the classes of native module @NS@: each instance owns (or borrows) a C++ object.
''' </summary>
''' <remarks>
''' <see cref="Dispose"/> releases the C++ object now; otherwise the garbage collector releases it.
''' A Visual Basic class inheriting a native class overrides its virtual methods, also when C++ calls them.
''' </remarks>
Public MustInherit Class NativeObject
    Implements Global.System.IDisposable
    Friend RosettaHolder As ULong
    Friend RosettaParent As Object
    Friend RosettaId As ULong
    Friend RosettaOwns As Boolean
    Friend Sub New(raw As NativeRaw)
    End Sub
    ''' <summary>Whether the C++ object is released (or was never created).</summary>
    Public ReadOnly Property IsDisposed As Boolean
        Get
            Return RosettaHolder = 0UL
        End Get
    End Property
    ''' <summary>Releases the C++ object now.</summary>
    Public Sub Dispose() Implements Global.System.IDisposable.Dispose
        Global.@NS@.RosettaRuntime.Rosetta.Close(Me)
        Global.System.GC.SuppressFinalize(Me)
    End Sub
    ''' <summary>Releases the C++ object.</summary>
    Protected Overrides Sub Finalize()
        Global.@NS@.RosettaRuntime.Rosetta.ReleaseLater(Me)
    End Sub
    ''' <summary>The class name.</summary>
    Public Overrides Function ToString() As String
        Return [GetType]().Name & If(RosettaHolder = 0UL, "(disposed)", "()")
    End Function
End Class

''' <summary>A C++ <c>std::function</c> returned to Visual Basic; convert it to a delegate type, or call <see cref="Invoke"/>.</summary>
Public NotInheritable Class NativeFunction
    Inherits NativeObject
    Friend Sub New(raw As NativeRaw)
        MyBase.New(raw)
    End Sub
    ''' <summary>Calls the C++ function.</summary>
    Public Function Invoke(ParamArray arguments As Object()) As Object
        Dim all(arguments.Length) As Object
        all(0) = New Global.@NS@.RosettaRuntime.Rosetta.Callee(Me)
        Global.System.Array.Copy(arguments, 0, all, 1, arguments.Length)
        Return Global.@NS@.RosettaRuntime.Rosetta.Run(0, all)
    End Function
End Class
)vb";

// The internal half (namespace @NS@.RosettaRuntime).
inline constexpr const char* vb_full_runtime_internal = R"vb(
Partial Friend NotInheritable Class Rosetta
    Private Sub New()
    End Sub

    Friend NotInheritable Class Symbol
        Friend ReadOnly Name As String
        Friend Sub New(text As String)
            Name = text
        End Sub
        Public Overrides Function ToString() As String
            Return Name
        End Function
    End Class
    Friend NotInheritable Class Callee
        Friend ReadOnly Target As Global.@NS@.NativeFunction
        Friend Sub New(f As Global.@NS@.NativeFunction)
            Target = f
        End Sub
    End Class
    ' A ByRef argument: C++ writes its new value back into the slot.
    Friend NotInheritable Class Slot
        Friend Value As Object
        Friend Sub New(initial As Object)
            Value = initial
        End Sub
    End Class

    ' ------------------------------------------------------------ native library
    <Global.System.Runtime.InteropServices.UnmanagedFunctionPointer(Global.System.Runtime.InteropServices.CallingConvention.Cdecl)>
    Private Delegate Function CallFn(index As UInteger, input As Global.System.IntPtr, size As Global.System.UIntPtr, ByRef output As Global.System.IntPtr, ByRef outputSize As Global.System.UIntPtr) As Integer
    <Global.System.Runtime.InteropServices.UnmanagedFunctionPointer(Global.System.Runtime.InteropServices.CallingConvention.Cdecl)>
    Private Delegate Sub FreeFn(pointer As Global.System.IntPtr)
    <Global.System.Runtime.InteropServices.UnmanagedFunctionPointer(Global.System.Runtime.InteropServices.CallingConvention.Cdecl)>
    Private Delegate Function AllocFn(size As Global.System.UIntPtr) As Global.System.IntPtr
    <Global.System.Runtime.InteropServices.UnmanagedFunctionPointer(Global.System.Runtime.InteropServices.CallingConvention.Cdecl)>
    Private Delegate Sub ReleaseFn(holder As Global.System.IntPtr, now As Integer)
    <Global.System.Runtime.InteropServices.UnmanagedFunctionPointer(Global.System.Runtime.InteropServices.CallingConvention.Cdecl)>
    Private Delegate Function InitFn(version As Integer, invoke As Global.System.IntPtr, retain As Global.System.IntPtr) As Integer
    <Global.System.Runtime.InteropServices.UnmanagedFunctionPointer(Global.System.Runtime.InteropServices.CallingConvention.Cdecl)>
    Private Delegate Function InvokeFn(kind As Integer, id As ULong, generic As UInteger, data As Global.System.IntPtr, size As Global.System.UIntPtr, result As Global.System.IntPtr, resultSize As Global.System.IntPtr) As Integer
    <Global.System.Runtime.InteropServices.UnmanagedFunctionPointer(Global.System.Runtime.InteropServices.CallingConvention.Cdecl)>
    Private Delegate Sub RetainFn(id As ULong, delta As Integer)
    Private Shared nativeCall As CallFn
    Private Shared nativeFree As FreeFn
    Private Shared nativeAlloc As AllocFn
    Private Shared nativeRelease As ReleaseFn
    Private Shared ReadOnly invokeHook As InvokeFn = AddressOf InvokeCallback
    Private Shared ReadOnly retainHook As RetainFn = AddressOf Adjust

    ' C# values C++ refers to (delegates passed as callbacks, subclass objects): ids index these
    ' tables. C++ references keep entries strong (retain/release); the rest are weak.
    Private Shared ReadOnly gate As New Object()
    Private Shared nextId As ULong
    Private Shared ReadOnly weak As New Global.System.Collections.Generic.Dictionary(Of ULong, Global.System.WeakReference)()
    Private Shared ReadOnly strong As New Global.System.Collections.Generic.Dictionary(Of ULong, Global.System.Collections.Generic.KeyValuePair(Of Object, Integer))()

    Shared Sub New()
        Dim library = Global.System.Runtime.InteropServices.NativeLibrary.Load(LibraryFile(), GetType(Rosetta).Assembly, Nothing)
        nativeCall = Export(Of CallFn)(library, "@C@_call")
        nativeFree = Export(Of FreeFn)(library, "@C@_free")
        nativeAlloc = Export(Of AllocFn)(library, "@C@_alloc")
        nativeRelease = Export(Of ReleaseFn)(library, "@C@_release")
        Dim init = Export(Of InitFn)(library, "@C@_init")
        If init(1, Global.System.Runtime.InteropServices.Marshal.GetFunctionPointerForDelegate(invokeHook),
                Global.System.Runtime.InteropServices.Marshal.GetFunctionPointerForDelegate(retainHook)) <> 0 Then
            Throw New Global.System.InvalidOperationException("native module @NS@ was generated by a different Rosetta version")
        End If
    End Sub
    Private Shared Function Export(Of T)(library As Global.System.IntPtr, name As String) As T
        Return DirectCast(DirectCast(Global.System.Runtime.InteropServices.Marshal.GetDelegateForFunctionPointer(
            Global.System.Runtime.InteropServices.NativeLibrary.GetExport(library, name), GetType(T)), Object), T)
    End Function
    ' @ENV@, else @LIB@ beside the assembly (or in lib/, ../lib), else the system's search.
    Private Shared Function LibraryFile() As String
        Dim variable = Global.System.Environment.GetEnvironmentVariable("@ENV@")
        If Not String.IsNullOrEmpty(variable) Then Return variable
        Dim names As String()
        If Global.System.OperatingSystem.IsWindows() Then
            names = New String() {"@LIB@.dll"}
        ElseIf Global.System.OperatingSystem.IsMacOS() Then
            names = New String() {"@LIB@.dylib", "lib@LIB@.dylib", "@LIB@.so"}
        Else
            names = New String() {"@LIB@.so", "lib@LIB@.so"}
        End If
        Dim directories As New Global.System.Collections.Generic.List(Of String)()
        Dim assembly = Global.System.IO.Path.GetDirectoryName(GetType(Rosetta).Assembly.Location)
        For Each directory In New String() {Global.System.AppContext.BaseDirectory, assembly}
            If Not String.IsNullOrEmpty(directory) Then
                directories.Add(directory)
                directories.Add(Global.System.IO.Path.Combine(directory, "lib"))
                directories.Add(Global.System.IO.Path.Combine(directory, "..", "lib"))
            End If
        Next
        For Each directory In directories
            For Each name In names
                Dim file = Global.System.IO.Path.Combine(directory, name)
                If Global.System.IO.File.Exists(file) Then Return file
            Next
        Next
        Return "@LIB@"
    End Function

    Private Shared Function NewId() As ULong
        SyncLock gate
            nextId += 1UL
            If nextId Mod 4096UL = 0UL Then
                Dim dead As New Global.System.Collections.Generic.List(Of ULong)()
                For Each entry In weak
                    If Not entry.Value.IsAlive Then dead.Add(entry.Key)
                Next
                For Each key In dead
                    weak.Remove(key)
                Next
            End If
            Return nextId
        End SyncLock
    End Function
    Friend Shared Function ObjectId(o As Global.@NS@.NativeObject) As ULong
        If o.RosettaId <> 0UL Then Return o.RosettaId
        Dim id = NewId()
        SyncLock gate
            weak(id) = New Global.System.WeakReference(o)
        End SyncLock
        o.RosettaId = id
        Return id
    End Function
    Private Shared Function Lookup(id As ULong) As Object
        SyncLock gate
            Dim entry As Global.System.Collections.Generic.KeyValuePair(Of Object, Integer) = Nothing
            If strong.TryGetValue(id, entry) Then Return entry.Key
            Dim reference As Global.System.WeakReference = Nothing
            If weak.TryGetValue(id, reference) Then
                Dim target = reference.Target
                If target IsNot Nothing Then Return target
            End If
        End SyncLock
        Throw New Global.@NS@.NativeException("binding", "the Visual Basic object of a native reference is gone")
    End Function
    Private Shared Sub Adjust(id As ULong, delta As Integer)
        SyncLock gate
            Dim entry As Global.System.Collections.Generic.KeyValuePair(Of Object, Integer) = Nothing
            Dim found = strong.TryGetValue(id, entry)
            If delta > 0 Then
                If found Then
                    strong(id) = New Global.System.Collections.Generic.KeyValuePair(Of Object, Integer)(entry.Key, entry.Value + delta)
                Else
                    Dim reference As Global.System.WeakReference = Nothing
                    If weak.TryGetValue(id, reference) Then
                        Dim target = reference.Target
                        If target IsNot Nothing Then strong(id) = New Global.System.Collections.Generic.KeyValuePair(Of Object, Integer)(target, delta)
                    End If
                End If
            ElseIf found Then
                Dim count = entry.Value + delta
                If count <= 0 Then
                    strong.Remove(id)
                Else
                    strong(id) = New Global.System.Collections.Generic.KeyValuePair(Of Object, Integer)(entry.Key, count)
                End If
            End If
        End SyncLock
    End Sub

    ' ------------------------------------------------------------ encoding (VB -> C++)
    Private NotInheritable Class Writer
        Friend ReadOnly Buffer As New Global.System.IO.MemoryStream()
        Friend ReadOnly W As Global.System.IO.BinaryWriter
        Friend ReadOnly Functions As New Global.System.Collections.Generic.List(Of ULong)()
        Friend ReadOnly Slots As New Global.System.Collections.Generic.List(Of Slot)()
        Friend Sub New()
            W = New Global.System.IO.BinaryWriter(Buffer)
        End Sub
        Friend Sub Text(s As String)
            Dim bytes = Global.System.Text.Encoding.UTF8.GetBytes(s)
            W.Write(CULng(bytes.Length))
            W.Write(bytes)
        End Sub
    End Class
    Private Shared Sub Encode(o As Writer, x As Object, Optional depth As Integer = 0)
        If depth > 10000 Then Throw New Global.@NS@.NativeException("binding", "value nesting is too deep")
        Dim w = o.W
        Dim invariant = Global.System.Globalization.CultureInfo.InvariantCulture
        If x Is Nothing Then
            w.Write(CByte(0))
        ElseIf TypeOf x Is Boolean Then
            w.Write(If(DirectCast(x, Boolean), CByte(1), CByte(19)))
        ElseIf TypeOf x Is SByte OrElse TypeOf x Is Byte OrElse TypeOf x Is Short OrElse TypeOf x Is UShort OrElse
               TypeOf x Is Integer OrElse TypeOf x Is UInteger OrElse TypeOf x Is Long Then
            w.Write(CByte(2))
            w.Write(Global.System.Convert.ToInt64(x, invariant))
        ElseIf TypeOf x Is ULong Then
            Dim u = DirectCast(x, ULong)
            If u <= CULng(Long.MaxValue) Then
                w.Write(CByte(2))
                w.Write(CLng(u))
            Else
                w.Write(CByte(3))
                o.Text(u.ToString(invariant))
            End If
        ElseIf TypeOf x Is Global.System.Numerics.BigInteger Then
            Dim big = DirectCast(x, Global.System.Numerics.BigInteger)
            If big >= New Global.System.Numerics.BigInteger(Long.MinValue) AndAlso big <= New Global.System.Numerics.BigInteger(Long.MaxValue) Then
                w.Write(CByte(2))
                w.Write(CLng(big))
            Else
                w.Write(CByte(3))
                o.Text(big.ToString(invariant))
            End If
        ElseIf TypeOf x Is Single OrElse TypeOf x Is Double OrElse TypeOf x Is Decimal Then
            w.Write(CByte(4))
            w.Write(Global.System.Convert.ToDouble(x, invariant))
        ElseIf TypeOf x Is Global.System.Numerics.Complex Then
            Dim c = DirectCast(x, Global.System.Numerics.Complex)
            w.Write(CByte(18))
            w.Write(c.Real)
            w.Write(c.Imaginary)
        ElseIf TypeOf x Is String Then
            w.Write(CByte(5))
            o.Text(DirectCast(x, String))
        ElseIf TypeOf x Is Char Then
            w.Write(CByte(5))
            o.Text(DirectCast(x, Char).ToString())
        ElseIf TypeOf x Is Symbol Then
            w.Write(CByte(14))
            o.Text(DirectCast(x, Symbol).Name)
        ElseIf TypeOf x Is Global.System.Enum Then
            ' By value: VB enum labels may differ from the C++ names (case-insensitive VB, renames).
            Encode(o, Global.System.Convert.ChangeType(x, Global.System.Enum.GetUnderlyingType(x.GetType()), invariant), depth + 1)
        ElseIf TypeOf x Is Callee Then
            EncodeObject(o, DirectCast(x, Callee).Target)
        ElseIf TypeOf x Is Global.@NS@.NativeObject Then
            EncodeObject(o, DirectCast(x, Global.@NS@.NativeObject))
        ElseIf TypeOf x Is Slot Then
            Dim held = DirectCast(x, Slot)
            o.Slots.Add(held)
            w.Write(CByte(13))
            w.Write(CUInt(o.Slots.Count - 1))
            Encode(o, held.Value, depth + 1)
        ElseIf TypeOf x Is Global.System.Delegate Then
            Dim id = NewId()
            SyncLock gate
                strong(id) = New Global.System.Collections.Generic.KeyValuePair(Of Object, Integer)(x, 1)
            End SyncLock
            o.Functions.Add(id)
            w.Write(CByte(12))
            w.Write(id)
        ElseIf TypeOf x Is Byte() Then
            Dim bytes = DirectCast(x, Byte())
            w.Write(CByte(15))
            w.Write(CULng(bytes.Length))
            w.Write(bytes)
        ElseIf TypeOf x Is Double() Then
            Dim reals = DirectCast(x, Double())
            w.Write(CByte(7))
            w.Write(CULng(reals.Length))
            For Each v In reals
                w.Write(v)
            Next
        ElseIf TypeOf x Is Global.System.Collections.Generic.List(Of Double) Then
            Dim reals = DirectCast(x, Global.System.Collections.Generic.List(Of Double))
            w.Write(CByte(7))
            w.Write(CULng(reals.Count))
            For Each v In reals
                w.Write(v)
            Next
        ElseIf TypeOf x Is Global.System.Collections.IDictionary Then
            Dim dictionary = DirectCast(x, Global.System.Collections.IDictionary)
            w.Write(CByte(9))
            w.Write(CULng(dictionary.Count))
            For Each entry As Global.System.Collections.DictionaryEntry In dictionary
                Encode(o, entry.Key, depth + 1)
                Encode(o, entry.Value, depth + 1)
            Next
        ElseIf TypeOf x Is Global.System.Runtime.CompilerServices.ITuple Then
            Dim tuple = DirectCast(x, Global.System.Runtime.CompilerServices.ITuple)
            w.Write(CByte(6))
            w.Write(CULng(tuple.Length))
            For i = 0 To tuple.Length - 1
                Encode(o, tuple(i), depth + 1)
            Next
        Else
            Dim type = x.GetType()
            If type.IsGenericType AndAlso type.GetGenericTypeDefinition() Is GetType(Global.System.Collections.Generic.KeyValuePair(Of ,)) Then
                w.Write(CByte(8))
                Encode(o, type.GetProperty("Key").GetValue(x), depth + 1)
                Encode(o, type.GetProperty("Value").GetValue(x), depth + 1)
            ElseIf TypeOf x Is Global.System.Collections.IEnumerable Then
                Dim items As New Global.System.Collections.Generic.List(Of Object)()
                Dim allReal = True
                For Each item In DirectCast(x, Global.System.Collections.IEnumerable)
                    items.Add(item)
                    If Not TypeOf item Is Double Then allReal = False
                Next
                If items.Count > 0 AndAlso allReal Then
                    w.Write(CByte(7))
                    w.Write(CULng(items.Count))
                    For Each item In items
                        w.Write(DirectCast(item, Double))
                    Next
                Else
                    w.Write(CByte(6))
                    w.Write(CULng(items.Count))
                    For Each item In items
                        Encode(o, item, depth + 1)
                    Next
                End If
            Else
                Throw New Global.@NS@.NativeException("binding", "cannot pass a value of type " & type.Name & " to C++")
            End If
        End If
    End Sub
    Private Shared Sub EncodeObject(o As Writer, x As Global.@NS@.NativeObject)
        o.W.Write(CByte(10))
        o.W.Write(x.RosettaHolder)
        o.W.Write(If(x.RosettaHolder = 0UL, 0UL, ObjectId(x)))
    End Sub

    ' ------------------------------------------------------------ decoding (C++ -> VB)
    Private NotInheritable Class Reader
        Private ReadOnly data As Byte()
        Private at As Integer
        Friend Sub New(bytes As Byte())
            data = bytes
        End Sub
        Friend ReadOnly Property Empty As Boolean
            Get
                Return data.Length = 0
            End Get
        End Property
        Friend Function Peek() As Byte
            Return data(at)
        End Function
        Private Sub Need(n As Integer)
            If at + n > data.Length Then Throw New Global.@NS@.NativeException("binding", "malformed native value")
        End Sub
        Friend Function U8() As Byte
            Need(1)
            Dim v = data(at)
            at += 1
            Return v
        End Function
        Friend Function U32() As UInteger
            Need(4)
            Dim v = Global.System.BitConverter.ToUInt32(data, at)
            at += 4
            Return v
        End Function
        Friend Function U64() As ULong
            Need(8)
            Dim v = Global.System.BitConverter.ToUInt64(data, at)
            at += 8
            Return v
        End Function
        Friend Function I64() As Long
            Need(8)
            Dim v = Global.System.BitConverter.ToInt64(data, at)
            at += 8
            Return v
        End Function
        Friend Function F64() As Double
            Need(8)
            Dim v = Global.System.BitConverter.ToDouble(data, at)
            at += 8
            Return v
        End Function
        Friend Function Count() As Integer
            Dim n = U64()
            If n > CULng(data.Length - at) Then Throw New Global.@NS@.NativeException("binding", "malformed native value")
            Return CInt(n)
        End Function
        Friend Function Bytes(n As Integer) As Byte()
            Need(n)
            Dim v(n - 1) As Byte
            Global.System.Array.Copy(data, at, v, 0, n)
            at += n
            Return v
        End Function
        Friend Function Text() As String
            Dim n = Count()
            Dim s = Global.System.Text.Encoding.UTF8.GetString(data, at, n)
            at += n
            Return s
        End Function
    End Class
    Private Shared Function Wrap(address As ULong, klass As UInteger, parent As Object) As Global.@NS@.NativeObject
        If klass >= CUInt(Classes.Length) Then Throw New Global.@NS@.NativeException("binding", "C++ result type is not exported to Visual Basic")
        Dim o = Classes(CInt(klass)).Invoke()
        Adopt(o, address, parent)
        Return o
    End Function
    Private Shared Sub Adopt(o As Global.@NS@.NativeObject, address As ULong, parent As Object)
        o.RosettaHolder = address
        o.RosettaParent = parent
        o.RosettaOwns = True
    End Sub
    Private Shared Function Decode(r As Reader, arguments As Object()) As Object
        Dim tag = r.U8()
        Select Case tag
            Case 0 : Return Nothing
            Case 1 : Return True
            Case 19 : Return False
            Case 2 : Return r.I64()
            Case 3
                Dim n = Global.System.Numerics.BigInteger.Parse(r.Text(), Global.System.Globalization.CultureInfo.InvariantCulture)
                If n >= Global.System.Numerics.BigInteger.Zero AndAlso n <= New Global.System.Numerics.BigInteger(ULong.MaxValue) Then Return CULng(n)
                Return n
            Case 4 : Return r.F64()
            Case 18
                Dim real = r.F64()
                Return New Global.System.Numerics.Complex(real, r.F64())
            Case 5 : Return r.Text()
            Case 14 : Return New Symbol(r.Text())
            Case 15 : Return r.Bytes(r.Count())
            Case 6
                Dim n = r.Count()
                Dim list As New Global.System.Collections.Generic.List(Of Object)(n)
                For i = 1 To n
                    list.Add(Decode(r, arguments))
                Next
                Return list
            Case 16
                Dim n = r.Count()
                Dim items(n - 1) As Object
                For i = 0 To n - 1
                    items(i) = Decode(r, arguments)
                Next
                Return items
            Case 7
                Dim n = CInt(r.U64())
                Dim v(n - 1) As Double
                For i = 0 To n - 1
                    v(i) = r.F64()
                Next
                Return v
            Case 9
                Dim n = r.Count()
                Dim d As New Global.System.Collections.Generic.Dictionary(Of Object, Object)(n)
                For i = 1 To n
                    Dim key = Decode(r, arguments)
                    If key Is Nothing Then Throw New Global.@NS@.NativeException("binding", "null dictionary key")
                    d(key) = Decode(r, arguments)
                Next
                Return d
            Case 8
                Dim key = Decode(r, arguments)
                Return New Global.System.Collections.Generic.KeyValuePair(Of Object, Object)(key, Decode(r, arguments))
            Case 10
                Dim address = r.U64()
                Dim klass = r.U32()
                Dim parent = r.I64()
                Return Wrap(address, klass, If(parent >= 0 AndAlso arguments IsNot Nothing AndAlso parent < arguments.Length, arguments(CInt(parent)), Nothing))
            Case 11 : Return Lookup(r.U64())
            Case 12
                Dim f As New Global.@NS@.NativeFunction(New Global.@NS@.NativeRaw())
                Adopt(f, r.U64(), Nothing)
                Return f
            Case 17
                Dim index = r.U32()
                Return If(arguments IsNot Nothing AndAlso index < CUInt(arguments.Length), arguments(CInt(index)), Nothing)
        End Select
        Throw New Global.@NS@.NativeException("binding", "malformed native value (tag " & tag.ToString(Global.System.Globalization.CultureInfo.InvariantCulture) & ")")
    End Function

    ' ------------------------------------------------------------ conversions to declared types
    Friend Shared Function Cast(Of T)(value As Object) As T
        Return DirectCast(Cast(GetType(T), value), T)
    End Function
    Friend Shared Function Cast(type As Global.System.Type, value As Object) As Object
        Dim invariant = Global.System.Globalization.CultureInfo.InvariantCulture
        If type Is GetType(Global.System.Void) Then Return Nothing
        If type Is GetType(Object) Then Return If(TypeOf value Is Symbol, DirectCast(value, Symbol).Name, value)
        Dim underlying = Global.System.Nullable.GetUnderlyingType(type)
        If value Is Nothing Then
            If type.IsValueType AndAlso underlying Is Nothing Then
                Throw New Global.@NS@.NativeException("binding", "C++ returned Nothing where Visual Basic expects " & type.Name)
            End If
            Return Nothing
        End If
        If underlying IsNot Nothing Then Return Cast(underlying, value)
        If type.IsEnum Then
            If TypeOf value Is Symbol Then Return Global.System.Enum.Parse(type, DirectCast(value, Symbol).Name, True)
            Return Global.System.Enum.ToObject(type, Global.System.Convert.ToInt64(value, invariant))
        End If
        If type Is GetType(String) Then
            If TypeOf value Is Symbol Then Return DirectCast(value, Symbol).Name
            If TypeOf value Is String Then Return value
            Return Global.System.Convert.ToString(value, invariant)
        End If
        If type Is GetType(Char) Then
            If TypeOf value Is String AndAlso DirectCast(value, String).Length > 0 Then Return DirectCast(value, String)(0)
            Return Global.System.Convert.ToChar(value, invariant)
        End If
        If type Is GetType(Boolean) Then
            If TypeOf value Is Boolean Then Return value
            Return Global.System.Convert.ToBoolean(value, invariant)
        End If
        If type Is GetType(Global.System.Numerics.Complex) Then
            If TypeOf value Is Global.System.Numerics.Complex Then Return value
            Return New Global.System.Numerics.Complex(Global.System.Convert.ToDouble(value, invariant), 0)
        End If
        If type.IsPrimitive OrElse type Is GetType(Decimal) Then
            If TypeOf value Is Global.System.Numerics.BigInteger Then
                Return Global.System.Convert.ChangeType(CDec(DirectCast(value, Global.System.Numerics.BigInteger)), type, invariant)
            End If
            Return Global.System.Convert.ChangeType(value, type, invariant)
        End If
        If type Is GetType(Global.System.Numerics.BigInteger) Then
            If TypeOf value Is Global.System.Numerics.BigInteger Then Return value
            Return New Global.System.Numerics.BigInteger(Global.System.Convert.ToDecimal(value, invariant))
        End If
        If GetType(Global.System.Delegate).IsAssignableFrom(type) Then
            If type.IsInstanceOfType(value) Then Return value
            If TypeOf value Is Global.@NS@.NativeFunction Then Return MakeDelegate(type, DirectCast(value, Global.@NS@.NativeFunction))
        End If
        If type.IsArray Then
            Dim element = type.GetElementType()
            If TypeOf value Is Double() AndAlso element Is GetType(Double) Then Return value
            Dim items = ItemsOf(value)
            Dim array = Global.System.Array.CreateInstance(element, items.Count)
            For i = 0 To items.Count - 1
                array.SetValue(Cast(element, items(i)), i)
            Next
            Return array
        End If
        If type.IsGenericType Then
            Dim definition = type.GetGenericTypeDefinition()
            Dim arguments = type.GetGenericArguments()
            If definition Is GetType(Global.System.Collections.Generic.List(Of )) OrElse definition Is GetType(Global.System.Collections.Generic.IEnumerable(Of )) OrElse
               definition Is GetType(Global.System.Collections.Generic.IReadOnlyList(Of )) OrElse definition Is GetType(Global.System.Collections.Generic.IList(Of )) OrElse
               definition Is GetType(Global.System.Collections.Generic.ICollection(Of )) OrElse definition Is GetType(Global.System.Collections.Generic.IReadOnlyCollection(Of )) Then
                If TypeOf value Is Double() AndAlso arguments(0) Is GetType(Double) Then
                    Return New Global.System.Collections.Generic.List(Of Double)(DirectCast(value, Double()))
                End If
                Dim list = DirectCast(Global.System.Activator.CreateInstance(GetType(Global.System.Collections.Generic.List(Of )).MakeGenericType(arguments(0))), Global.System.Collections.IList)
                For Each item In ItemsOf(value)
                    list.Add(Cast(arguments(0), item))
                Next
                Return list
            End If
            If definition Is GetType(Global.System.Collections.Generic.HashSet(Of )) OrElse definition Is GetType(Global.System.Collections.Generic.ISet(Of )) OrElse
               definition Is GetType(Global.System.Collections.Generic.IReadOnlySet(Of )) Then
                Dim made = Global.System.Activator.CreateInstance(GetType(Global.System.Collections.Generic.HashSet(Of )).MakeGenericType(arguments(0)))
                Dim add = made.GetType().GetMethod("Add")
                For Each item In ItemsOf(value)
                    add.Invoke(made, New Object() {Cast(arguments(0), item)})
                Next
                Return made
            End If
            If definition Is GetType(Global.System.Collections.Generic.Dictionary(Of ,)) OrElse definition Is GetType(Global.System.Collections.Generic.IDictionary(Of ,)) OrElse
               definition Is GetType(Global.System.Collections.Generic.IReadOnlyDictionary(Of ,)) Then
                Dim dictionary = DirectCast(Global.System.Activator.CreateInstance(GetType(Global.System.Collections.Generic.Dictionary(Of ,)).MakeGenericType(arguments)), Global.System.Collections.IDictionary)
                If TypeOf value Is Global.System.Collections.IDictionary Then
                    For Each entry As Global.System.Collections.DictionaryEntry In DirectCast(value, Global.System.Collections.IDictionary)
                        dictionary(Cast(arguments(0), entry.Key)) = Cast(arguments(1), entry.Value)
                    Next
                Else
                    For Each item In ItemsOf(value)
                        If TypeOf item Is Global.System.Collections.Generic.KeyValuePair(Of Object, Object) Then
                            Dim pair = DirectCast(item, Global.System.Collections.Generic.KeyValuePair(Of Object, Object))
                            dictionary(Cast(arguments(0), pair.Key)) = Cast(arguments(1), pair.Value)
                        End If
                    Next
                End If
                Return dictionary
            End If
            If definition Is GetType(Global.System.Collections.Generic.KeyValuePair(Of ,)) AndAlso TypeOf value Is Global.System.Collections.Generic.KeyValuePair(Of Object, Object) Then
                Dim kv = DirectCast(value, Global.System.Collections.Generic.KeyValuePair(Of Object, Object))
                Return Global.System.Activator.CreateInstance(type, Cast(arguments(0), kv.Key), Cast(arguments(1), kv.Value))
            End If
            If type.FullName.StartsWith("System.ValueTuple`", Global.System.StringComparison.Ordinal) OrElse type.FullName.StartsWith("System.Tuple`", Global.System.StringComparison.Ordinal) Then
                Dim items As Global.System.Collections.Generic.List(Of Object)
                If TypeOf value Is Global.System.Collections.Generic.KeyValuePair(Of Object, Object) Then
                    Dim p = DirectCast(value, Global.System.Collections.Generic.KeyValuePair(Of Object, Object))
                    items = New Global.System.Collections.Generic.List(Of Object) From {p.Key, p.Value}
                Else
                    items = ItemsOf(value)
                End If
                If items.Count <> arguments.Length Then
                    Throw New Global.@NS@.NativeException("binding", "expected a tuple of " & arguments.Length.ToString(invariant) & " elements")
                End If
                Dim converted(items.Count - 1) As Object
                For i = 0 To items.Count - 1
                    converted(i) = Cast(arguments(i), items(i))
                Next
                Return Global.System.Activator.CreateInstance(type, converted)
            End If
        End If
        If type.IsInstanceOfType(value) Then Return value
        If TypeOf value Is Global.@NS@.NativeObject AndAlso GetType(Global.@NS@.NativeObject).IsAssignableFrom(type) Then
            Throw New Global.@NS@.NativeException("binding", value.GetType().Name & " is not a " & type.Name)
        End If
        Throw New Global.@NS@.NativeException("binding", "cannot convert " & value.GetType().Name & " to " & type.Name)
    End Function
    Private Shared Function ItemsOf(value As Object) As Global.System.Collections.Generic.List(Of Object)
        If TypeOf value Is Global.System.Collections.Generic.List(Of Object) Then Return DirectCast(value, Global.System.Collections.Generic.List(Of Object))
        If TypeOf value Is Global.System.Collections.IEnumerable AndAlso Not TypeOf value Is String Then
            Dim items As New Global.System.Collections.Generic.List(Of Object)()
            For Each item In DirectCast(value, Global.System.Collections.IEnumerable)
                items.Add(item)
            Next
            Return items
        End If
        Throw New Global.@NS@.NativeException("binding", "expected a sequence, got " & value.GetType().Name)
    End Function
    Private Shared ReadOnly castMethod As Global.System.Reflection.MethodInfo = GetType(Rosetta).GetMethod("Cast",
        Global.System.Reflection.BindingFlags.NonPublic Or Global.System.Reflection.BindingFlags.Static, New Global.System.Type() {GetType(Global.System.Type), GetType(Object)})
    Private Shared Function MakeDelegate(type As Global.System.Type, native As Global.@NS@.NativeFunction) As Global.System.Delegate
        Dim invoke = type.GetMethod("Invoke")
        Dim declared = invoke.GetParameters()
        Dim parameters(declared.Length - 1) As Global.System.Linq.Expressions.ParameterExpression
        Dim boxed(declared.Length - 1) As Global.System.Linq.Expressions.Expression
        For i = 0 To declared.Length - 1
            parameters(i) = Global.System.Linq.Expressions.Expression.Parameter(declared(i).ParameterType, declared(i).Name)
            boxed(i) = Global.System.Linq.Expressions.Expression.Convert(parameters(i), GetType(Object))
        Next
        Dim body As Global.System.Linq.Expressions.Expression = Global.System.Linq.Expressions.Expression.Call(
            Global.System.Linq.Expressions.Expression.Constant(native), GetType(Global.@NS@.NativeFunction).GetMethod("Invoke"),
            Global.System.Linq.Expressions.Expression.NewArrayInit(GetType(Object), boxed))
        If invoke.ReturnType IsNot GetType(Global.System.Void) Then
            body = Global.System.Linq.Expressions.Expression.Convert(
                Global.System.Linq.Expressions.Expression.Call(castMethod, Global.System.Linq.Expressions.Expression.Constant(invoke.ReturnType), body), invoke.ReturnType)
        End If
        Return Global.System.Linq.Expressions.Expression.Lambda(type, body, parameters).Compile()
    End Function

    ' ------------------------------------------------------------ calls
    <Global.System.ThreadStatic> Private Shared callbackException As Global.System.Exception
    Friend Shared Function Run(index As Integer, ParamArray arguments As Object()) As Object
        Return RunInto(CUInt(index), arguments, Nothing)
    End Function
    Friend Shared Function RunInto(index As UInteger, arguments As Object(), into As Global.@NS@.NativeObject) As Object
        Dim o As New Writer()
        Try
            ' .NET and C++ buffer standard output separately: flush before C++ writes.
            Global.System.Console.Out.Flush()
            o.W.Write(CUInt(arguments.Length))
            For Each argument In arguments
                Encode(o, argument)
            Next
            o.W.Flush()
            Dim data = o.Buffer.ToArray()
            Dim handle = Global.System.Runtime.InteropServices.GCHandle.Alloc(data, Global.System.Runtime.InteropServices.GCHandleType.Pinned)
            Dim status As Integer
            Dim output As Global.System.IntPtr = Global.System.IntPtr.Zero
            Dim outputSize As Global.System.UIntPtr = Global.System.UIntPtr.Zero
            Try
                status = nativeCall(index, handle.AddrOfPinnedObject(), New Global.System.UIntPtr(CULng(data.Length)), output, outputSize)
            Finally
                handle.Free()
            End Try
            Dim bytes As Byte()
            Try
                bytes = New Byte(CInt(outputSize.ToUInt64()) - 1) {}
                If output <> Global.System.IntPtr.Zero AndAlso bytes.Length > 0 Then Global.System.Runtime.InteropServices.Marshal.Copy(output, bytes, 0, bytes.Length)
            Finally
                If output <> Global.System.IntPtr.Zero Then nativeFree(output)
            End Try
            Dim r As New Reader(bytes)
            Select Case status
                Case 0
                    Dim value As Object
                    If into IsNot Nothing AndAlso Not r.Empty AndAlso r.Peek() = 10 Then
                        r.U8()
                        Dim address = r.U64()
                        r.U32()
                        r.I64()
                        Adopt(into, address, Nothing)
                        value = into
                    Else
                        value = Decode(r, arguments)
                    End If
                    Dim writebacks = r.U32()
                    For i = 1UI To writebacks
                        Dim slot = r.U32()
                        o.Slots(CInt(slot)).Value = Decode(r, arguments)
                    Next
                    Global.System.GC.KeepAlive(arguments)
                    Return value
                Case 2
                    Dim pending = callbackException
                    callbackException = Nothing
                    Decode(r, Nothing)
                    Dim message = TryCast(Decode(r, Nothing), String)
                    If pending IsNot Nothing Then Global.System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(pending).Throw()
                    Throw New Global.@NS@.NativeException("visual_basic", If(message, "error in a Visual Basic callback"))
                Case 1
                    Dim kind = Decode(r, Nothing)
                    Dim message = TryCast(Decode(r, Nothing), String)
                    Throw New Global.@NS@.NativeException(If(kind Is Nothing, "native", kind.ToString().Replace("-"c, "_"c)), If(message, "native error"))
                Case Else
                    Throw New Global.@NS@.NativeException("allocation", "out of memory in a native call")
            End Select
        Finally
            For Each id In o.Functions
                Adjust(id, -1)
            Next
        End Try
    End Function
    Friend Shared Sub Construct(self As Global.@NS@.NativeObject, declared As Global.System.Type, director As Boolean, index As Integer, ParamArray arguments As Object())
        Dim all(arguments.Length) As Object
        all(0) = If(director AndAlso self.GetType() IsNot declared, CLng(ObjectId(self)), 0L)
        Global.System.Array.Copy(arguments, 0, all, 1, arguments.Length)
        RunInto(CUInt(index), all, self)
    End Sub
    ' A secondary base class's view of an object (.NET has single inheritance): same C++ object.
    Friend Shared Function View(Of T As Global.@NS@.NativeObject)(source As Global.@NS@.NativeObject, make As Global.System.Func(Of T)) As T
        If source Is Nothing Then Return Nothing
        Dim made = make()
        made.RosettaHolder = source.RosettaHolder
        made.RosettaParent = source
        made.RosettaOwns = False
        Return made
    End Function
    Friend Shared Sub Close(o As Global.@NS@.NativeObject)
        Dim address = o.RosettaHolder
        If address = 0UL Then Return
        o.RosettaHolder = 0UL
        If o.RosettaOwns AndAlso nativeRelease IsNot Nothing Then nativeRelease(New Global.System.IntPtr(CLng(address)), 1)
    End Sub
    Friend Shared Sub ReleaseLater(o As Global.@NS@.NativeObject)
        Dim address = o.RosettaHolder
        If address <> 0UL AndAlso o.RosettaOwns AndAlso nativeRelease IsNot Nothing Then nativeRelease(New Global.System.IntPtr(CLng(address)), 0)
    End Sub

    ' Calls from C++: callbacks (kind 0) and overrides of virtual methods (kind 1).
    Private Shared Function InvokeCallback(kind As Integer, id As ULong, generic As UInteger, data As Global.System.IntPtr, size As Global.System.UIntPtr, result As Global.System.IntPtr, resultSize As Global.System.IntPtr) As Integer
        Dim status = 0
        Dim o As New Writer()
        Try
            Dim bytes(CInt(size.ToUInt64()) - 1) As Byte
            If bytes.Length > 0 Then Global.System.Runtime.InteropServices.Marshal.Copy(data, bytes, 0, bytes.Length)
            Dim r As New Reader(bytes)
            Dim count = r.U32()
            Dim arguments(CInt(count) - 1) As Object
            For i = 0 To CInt(count) - 1
                arguments(i) = Decode(r, Nothing)
            Next
            Dim value = If(kind = 0, CallDelegate(DirectCast(Lookup(id), Global.System.Delegate), arguments), CallOverride(generic, arguments))
            Try
                Encode(o, value)
            Catch ex As Global.@NS@.NativeException
                o = New Writer()
                Encode(o, Nothing)
            End Try
        Catch ex As Global.System.Exception
            Dim failure = ex
            If TypeOf failure Is Global.System.Reflection.TargetInvocationException AndAlso failure.InnerException IsNot Nothing Then failure = failure.InnerException
            callbackException = failure
            o = New Writer()
            o.W.Write(CByte(5))
            o.Text(failure.Message)
            status = 2
        End Try
        Global.System.Console.Out.Flush()
        o.W.Flush()
        Dim output = o.Buffer.ToArray()
        Dim buffer = nativeAlloc(New Global.System.UIntPtr(CULng(Global.System.Math.Max(1, output.Length))))
        If output.Length > 0 Then Global.System.Runtime.InteropServices.Marshal.Copy(output, 0, buffer, output.Length)
        Global.System.Runtime.InteropServices.Marshal.WriteIntPtr(result, buffer)
        Global.System.Runtime.InteropServices.Marshal.WriteIntPtr(resultSize, New Global.System.IntPtr(output.Length))
        Return status
    End Function
    Private Shared Function CallDelegate(f As Global.System.Delegate, arguments As Object()) As Object
        Dim parameters = f.GetType().GetMethod("Invoke").GetParameters()
        If parameters.Length <> arguments.Length Then
            Throw New Global.@NS@.NativeException("binding", "callback takes " & parameters.Length.ToString(Global.System.Globalization.CultureInfo.InvariantCulture) &
                " arguments, C++ passed " & arguments.Length.ToString(Global.System.Globalization.CultureInfo.InvariantCulture))
        End If
        Dim converted(arguments.Length - 1) As Object
        For i = 0 To arguments.Length - 1
            converted(i) = Cast(parameters(i).ParameterType, arguments(i))
        Next
        Try
            Return f.DynamicInvoke(converted)
        Catch ex As Global.System.Reflection.TargetInvocationException When ex.InnerException IsNot Nothing
            Global.System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(ex.InnerException).Throw()
            Throw
        End Try
    End Function
    ' The VB method an override call names, on the object's own (most derived) type. Visual Basic
    ' names are case-insensitive, so the match is too.
    Private Shared Function CallOverride(generic As UInteger, arguments As Object()) As Object
        Dim self = arguments(0)
        If self Is Nothing Then Throw New Global.@NS@.NativeException("binding", "override called without an object")
        Dim name = Generics(CInt(generic))
        Dim rest(arguments.Length - 2) As Object
        Global.System.Array.Copy(arguments, 1, rest, 0, rest.Length)
        Dim last As Global.System.Exception = Nothing
        For Each method In self.GetType().GetMethods(Global.System.Reflection.BindingFlags.Public Or Global.System.Reflection.BindingFlags.Instance)
            If Not String.Equals(method.Name, name, Global.System.StringComparison.OrdinalIgnoreCase) OrElse method.GetParameters().Length <> rest.Length Then Continue For
            Dim converted(rest.Length - 1) As Object
            Try
                Dim parameters = method.GetParameters()
                For i = 0 To rest.Length - 1
                    Dim type = parameters(i).ParameterType
                    converted(i) = Cast(If(type.IsByRef, type.GetElementType(), type), rest(i))
                Next
            Catch ex As Global.System.Exception
                last = ex
                Continue For
            End Try
            Try
                Return method.Invoke(self, converted)
            Catch ex As Global.System.Reflection.TargetInvocationException When ex.InnerException IsNot Nothing
                Global.System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(ex.InnerException).Throw()
                Throw
            End Try
        Next
        If last IsNot Nothing Then Global.System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(last).Throw()
        Throw New Global.@NS@.NativeException("binding", self.GetType().Name & " has no method " & name & " taking " &
            rest.Length.ToString(Global.System.Globalization.CultureInfo.InvariantCulture) & " arguments")
    End Function
End Class
)vb";
} // namespace rosetta::detail
