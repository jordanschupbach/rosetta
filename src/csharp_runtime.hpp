#pragma once
#include "cabi_runtime.hpp"

namespace rosetta::detail {
inline constexpr const char* csharp_runtime = R"cs(
[global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Sequential)]
private struct _RosettaValue {
    internal ulong Tag;
    internal long Integer;
    internal ulong Unsigned;
    internal double Real;
    internal global::System.IntPtr Data;
    internal global::System.UIntPtr Size;
}
[global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Sequential)]
private struct _RosettaResult { internal _RosettaValue Value; internal ulong Error; }
[global::System.Runtime.InteropServices.DllImport("MODULE", EntryPoint="rosetta_MODULE_abi", CallingConvention=global::System.Runtime.InteropServices.CallingConvention.Cdecl)]
private static extern ulong _RosettaAbi();
[global::System.Runtime.InteropServices.DllImport("MODULE", EntryPoint="rosetta_MODULE_call", CallingConvention=global::System.Runtime.InteropServices.CallingConvention.Cdecl)]
private static extern void _RosettaCall([global::System.Runtime.InteropServices.MarshalAs(global::System.Runtime.InteropServices.UnmanagedType.LPUTF8Str)] string operation,
    ulong id, [global::System.Runtime.InteropServices.In] _RosettaValue[] arguments, global::System.UIntPtr count, out _RosettaResult result);
[global::System.Runtime.InteropServices.DllImport("MODULE", EntryPoint="rosetta_MODULE_free", CallingConvention=global::System.Runtime.InteropServices.CallingConvention.Cdecl)]
private static extern void _RosettaFree(global::System.IntPtr data);
[global::System.Runtime.InteropServices.DllImport("MODULE", EntryPoint="rosetta_MODULE_release", CallingConvention=global::System.Runtime.InteropServices.CallingConvention.Cdecl)]
private static extern void _RosettaRelease(ulong id);
[global::System.Runtime.InteropServices.DllImport("MODULE", EntryPoint="rosetta_MODULE_shutdown", CallingConvention=global::System.Runtime.InteropServices.CallingConvention.Cdecl)]
private static extern void _RosettaShutdown();
static @MODULE() {
    if (_RosettaAbi() != 1) throw new global::System.InvalidOperationException("Rosetta C# ABI mismatch");
    global::System.AppDomain.CurrentDomain.ProcessExit += (_, _) => _RosettaShutdown();
}
// The SafeHandle stores a sentinel; the full-width registry ID has its own field.
private sealed class _RosettaHandle : global::System.Runtime.InteropServices.SafeHandle {
    internal ulong Id;
    internal _RosettaHandle() : base(global::System.IntPtr.Zero, true) { }
    public override bool IsInvalid => handle == global::System.IntPtr.Zero;
    internal void Initialize(ulong id) { Id = id; SetHandle(new global::System.IntPtr(1)); }
    protected override bool ReleaseHandle() { _RosettaRelease(Id); return true; }
}
private static object? _RosettaInvoke(string operation, _RosettaHandle? owner, params object[] arguments) =>
    _RosettaInvokeCore(operation, owner, null, arguments);
private static void _RosettaCreate(_RosettaHandle target, string operation, params object[] arguments) =>
    _RosettaInvokeCore(operation, null, target, arguments);
private static object? _RosettaInvokeCore(string operation, _RosettaHandle? owner, _RosettaHandle? created, object[] arguments) {
    var packed = new _RosettaValue[arguments.Length];
    var pins = new global::System.Runtime.InteropServices.GCHandle[arguments.Length];
    var result = new _RosettaResult();
    bool acquired = false;
    try {
        if (owner != null) owner.DangerousAddRef(ref acquired);
        for (int i = 0; i < arguments.Length; ++i) {
            switch (arguments[i]) {
                case long n: packed[i].Tag = 1; packed[i].Integer = n; break;
                case ulong n: packed[i].Tag = 2; packed[i].Unsigned = n; break;
                case double n: packed[i].Tag = 3; packed[i].Real = n; break;
                case float n: packed[i].Tag = 6; packed[i].Real = n; break;
                case bool n: packed[i].Tag = 4; packed[i].Unsigned = n ? 1UL : 0UL; break;
                case byte[] bytes:
                    pins[i] = global::System.Runtime.InteropServices.GCHandle.Alloc(bytes, global::System.Runtime.InteropServices.GCHandleType.Pinned);
                    packed[i].Tag = 5; packed[i].Data = pins[i].AddrOfPinnedObject(); packed[i].Size = (global::System.UIntPtr)(uint)bytes.Length;
                    break;
                default: throw new global::System.ArgumentException("expected a non-null native scalar or byte array");
            }
        }
        _RosettaCall(operation, owner?.Id ?? 0, packed, (global::System.UIntPtr)(uint)packed.Length, out result);
        if (result.Error != 0) {
            string message = result.Value.Data == global::System.IntPtr.Zero ? "native allocation failed" :
                global::System.Text.Encoding.UTF8.GetString(_RosettaBytes(result.Value));
            throw result.Error switch {
                1 => new global::System.OutOfMemoryException(message),
                2 => new global::System.OverflowException(message),
                3 => new global::System.IndexOutOfRangeException(message),
                4 or 6 => new global::System.ArgumentException(message),
                _ => new global::System.InvalidOperationException(message)
            };
        }
        // Publish ownership before any allocation (including boxing the ID).
        if (created != null) { created.Initialize(result.Value.Unsigned); return null; }
        return result.Value.Tag switch {
            0 => null,
            1 => result.Value.Integer,
            2 => result.Value.Unsigned,
            3 => result.Value.Real,
            4 => result.Value.Unsigned != 0,
            5 => _RosettaBytes(result.Value),
            6 => (float)result.Value.Real,
            _ => throw new global::System.InvalidOperationException("invalid native result tag")
        };
    } finally {
        _RosettaFree(result.Value.Data);
        foreach (var pin in pins) if (pin.IsAllocated) pin.Free();
        if (acquired) owner!.DangerousRelease();
    }
}
private static byte[] _RosettaBytes(_RosettaValue value) {
    var bytes = new byte[checked((int)value.Size.ToUInt64())];
    if (bytes.Length != 0) global::System.Runtime.InteropServices.Marshal.Copy(value.Data, bytes, 0, bytes.Length);
    return bytes;
}
)cs";
} // namespace rosetta::detail
