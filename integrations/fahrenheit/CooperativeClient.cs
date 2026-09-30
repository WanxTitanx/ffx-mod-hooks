// Jarvis-HOOK: this client is compiled only against the reviewed optional provider.
#if FFXHOOKS_COOPERATIVE_PROVIDER
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Fahrenheit;

namespace FfxHooks.FahrenheitBridge;

internal sealed unsafe class CooperativeClient : IFhCooperativeClient {
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int ConflictFn(byte* request);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int ActivateFn(uint abi, uint caps, ConflictFn conflict);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate ulong BeginWriteFn(char* path, uint slot, byte* source, uint size, byte* output);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int OpenWriteFn(ulong ticket, nint handle);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int EndFn(ulong ticket, int success);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate ulong BeginReadFn(char* path, uint slot, byte* target, uint size);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int TransformFn(ulong ticket, nint handle, byte* source, uint size);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int AbortFn(ulong ticket);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int CancelFn(byte* target, uint size);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate nint ResourceFn(byte* request);

    private readonly ActivateFn _activate;
    private readonly BeginWriteFn _beginWrite;
    private readonly OpenWriteFn _openWrite;
    private readonly EndFn _endWrite, _endRead;
    private readonly BeginReadFn _beginRead;
    private readonly TransformFn _transform;
    private readonly AbortFn _abort;
    private readonly CancelFn _cancel;
    private readonly ResourceFn _resource;
    private readonly ConflictFn _conflict;
    private Func<string, bool>? _hasReplacement;

    public CooperativeClient(nint library) {
        T Export<T>(string name) where T : Delegate => Marshal.GetDelegateForFunctionPointer<T>(NativeLibrary.GetExport(library, name));
        _activate = Export<ActivateFn>("FfxHooks_FahrenheitActivateServicesV2");
        _beginWrite = Export<BeginWriteFn>("FfxHooks_FahrenheitBeginWriteV2");
        _openWrite = Export<OpenWriteFn>("FfxHooks_FahrenheitOpenWriteV2");
        _endWrite = Export<EndFn>("FfxHooks_FahrenheitEndWriteV2");
        _beginRead = Export<BeginReadFn>("FfxHooks_FahrenheitBeginReadV2");
        _transform = Export<TransformFn>("FfxHooks_FahrenheitTransformReadV2");
        _endRead = Export<EndFn>("FfxHooks_FahrenheitEndReadV2");
        _abort = Export<AbortFn>("FfxHooks_FahrenheitAbortIoV2");
        _cancel = Export<CancelFn>("FfxHooks_FahrenheitCancelReadV2");
        _resource = Export<ResourceFn>("FfxHooks_FahrenheitOpenResourceV2");
        _conflict = request => {
            try { return request == null || _hasReplacement is null || _hasReplacement(Marshal.PtrToStringUTF8((nint)request)!) ? 1 : 0; }
            catch { return 1; }
        };
    }
    public uint Start(uint protocol, uint providers, Func<string, bool> hasReplacement) {
        if (protocol != 2 || providers != 12) throw new IOException("Both cooperative providers must be installed before native startup.");
        _hasReplacement = hasReplacement;
        if (_activate(protocol, providers, _conflict) != 1)
            throw new IOException("Native cooperative startup failed. The game must not continue with a partially initialized save/resource client.");
        return providers;
    }
    public ulong BeginWrite(string path, int slot, ReadOnlySpan<byte> source, Span<byte> output) {
        if (slot < 0 || source.Length != output.Length) return 0;
        fixed (char* name = path) fixed (byte* input = source) fixed (byte* result = output)
            return _beginWrite(name, (uint)slot, input, (uint)source.Length, result);
    }
    public bool OpenWrite(ulong ticket, nint handle) => _openWrite(ticket, handle) == 1;
    public bool EndWrite(ulong ticket, bool completed) => _endWrite(ticket, completed ? 1 : 0) == 1;
    public ulong BeginRead(string path, int slot, nint target, int size) {
        if (slot < 0 || size <= 0) return 0;
        fixed (char* name = path) return _beginRead(name, (uint)slot, (byte*)target, (uint)size);
    }
    public bool TransformRead(ulong ticket, nint handle, ReadOnlySpan<byte> disk) {
        fixed (byte* data = disk) return _transform(ticket, handle, data, (uint)disk.Length) == 1;
    }
    public bool EndRead(ulong ticket, bool accepted) => _endRead(ticket, accepted ? 1 : 0) == 1;
    public void Abort(ulong ticket) {
        if (_abort(ticket) != 1) throw new IOException("Native cooperative abort was not accepted.");
    }
    public void CancelRead(nint target, int size) {
        if (size <= 0 || _cancel((byte*)target, (uint)size) != 1)
            throw new IOException("Native read cancellation was not accepted; provenance was not confirmed retired.");
    }
    public nint OpenResource(string normalizedPath) {
        if (normalizedPath.Length >= 256) return 0;
        byte[] request = Encoding.UTF8.GetBytes(normalizedPath + '\0');
        fixed (byte* data = request) return _resource(data);
    }
}
#endif
