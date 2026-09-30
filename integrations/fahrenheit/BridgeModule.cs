// Jarvis-HOOK. Uses the public Fahrenheit API; no reflection or private save access.
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using Fahrenheit;
using Fahrenheit.Events;
using Hexa.NET.ImGui;

namespace FfxHooks.FahrenheitBridge;

[FhLoad(FhGameId.FFX)]
public sealed unsafe class BridgeModule : FhModule {
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    private struct Status {
        public uint Size, Abi, Phase, Capabilities, ManagedSaveCompatible, Frames;
    }
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int QueryFn(Status* status, uint size);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int ReadyFn(uint abi, uint capabilities);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int FrameFn(nint swapChain, uint capture);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate uint ResizeBeginFn(nint swapChain);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int ResizeEndFn(uint ticket);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate int CreateFn(nint adapter, int driverType, nint software, uint flags,
        nint levels, uint levelCount, uint sdk, nint description, nint* swapChain,
        nint* device, nint level, nint* context);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate int ResizeFn(nint swapChain, uint count, uint width, uint height, int format, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern nint GetModuleHandleW(string? name);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern uint GetModuleFileNameW(nint module, char* buffer, uint capacity);

    private readonly CreateFn _create;
    private readonly ResizeFn _resize;
    private QueryFn? _query;
    private ReadyFn? _ready;
    private FrameFn? _frame;
    private ResizeBeginFn? _resizeBegin;
    private ResizeEndFn? _resizeEnd;
    private nint _native, _swapChain, _resizeAddress;
    private readonly BridgeLifecycle _lifecycle = new();
    private bool _resizeReady;
#if FFXHOOKS_COOPERATIVE_PROVIDER
    private CooperativeClient? _services;
#endif

    public BridgeModule() { _create = Create; _resize = Resize; }
    private static FhMethodHandle<CreateFn> Creation() => new(new FhMethodLocation("d3d11.dll", "D3D11CreateDeviceAndSwapChain"));
    private FhMethodHandle<ResizeFn> ResizeHandle() => new(new FhMethodLocation(_resizeAddress));
    private T Export<T>(string name) where T : Delegate =>
        Marshal.GetDelegateForFunctionPointer<T>(NativeLibrary.GetExport(_native, name));

    public override bool init(FhModContext context, FileStream globalState) {
        try {
            return _lifecycle.Initialize(Register);
        } catch (Exception error) {
            _lifecycle.Fail(); _logger.Error(error.ToString()); return false;
        }
    }
    private bool Register() {
            _native = GetModuleHandleW("ffx-hooks.dll");
            if (_native == 0) {
                char* path = stackalloc char[32768];
                uint length = GetModuleFileNameW(0, path, 32768);
                if (length == 0 || length >= 32768) return false;
                string root = Path.GetDirectoryName(new string(path, 0, (int)length))!;
                _native = NativeLibrary.Load(Path.Join(root, "modules", "ffx-hooks.dll"));
            }
            // Keep the native module and these delegates alive for process lifetime.
            // Its retained trampolines intentionally do not support hot unload.
            _query = Export<QueryFn>("FfxHooks_FahrenheitQueryV1");
            _ready = Export<ReadyFn>("FfxHooks_FahrenheitReadyV1");
            _frame = Export<FrameFn>("FfxHooks_FahrenheitFrameV1");
            _resizeBegin = Export<ResizeBeginFn>("FfxHooks_FahrenheitResizeBeginV1");
            _resizeEnd = Export<ResizeEndFn>("FfxHooks_FahrenheitResizeEndV1");
            Status status = default;
            if (_query(&status, (uint)sizeof(Status)) != 1 || status.Abi != 1 || status.Capabilities != 3) return false;
            if (!FhApi.Events.Common.GameLoop.PreUpdate.subscribe(Bootstrap)) return false;
            if (!Creation().hook(this, _create)) return false;
#if FFXHOOKS_COOPERATIVE_PROVIDER
            _services = new CooperativeClient(_native);
            FhCooperativeServices.Current.RegisterClient(_services);
            _logger.Info("FFX Hooks bridge registered with cooperative save/resource protocol 2.");
#else
            _logger.Info("FFX Hooks bridge registered. Shared hooks/rendering enabled; managed-save extensions unavailable.");
#endif
            return true;
    }
    private void Bootstrap(UpdateLoopEventArgs args) {
        if (!Volatile.Read(ref _resizeReady)) return;
        // The first real game update occurs after Fahrenheit has committed its
        // initial hook chains. No timeout is used as a substitute for readiness.
        try {
            _lifecycle.Bootstrap(() => {
                if (_ready is not null && _ready(1, 3) == 1) return true;
                _logger.Error("FFX Hooks bootstrap rejected; restart with the matching native bridge build.");
                return false;
            });
        } catch (Exception error) {
            _lifecycle.Fail(); _logger.Error(error.ToString());
        }
    }
    private int Create(nint adapter, int driverType, nint software, uint flags,
        nint levels, uint levelCount, uint sdk, nint description, nint* swapChain,
        nint* device, nint level, nint* context) {
        var next = Creation().chain_from(_create).fnptr;
        if (next is null) return unchecked((int)0x80004005);
        int result = next(adapter, driverType, software, flags, levels, levelCount, sdk, description, swapChain, device, level, context);
        if (result >= 0 && swapChain != null && *swapChain != 0 && _swapChain == 0) {
            _swapChain = *swapChain;
            _resizeAddress = (*(nint**)_swapChain)[13];
            Volatile.Write(ref _resizeReady, ResizeHandle().hook(this, _resize));
            if (!_resizeReady) _logger.Error("Resize ownership unavailable; native frame delivery remains disabled.");
        }
        return result;
    }
    private int Resize(nint swapChain, uint count, uint width, uint height, int format, uint flags) {
        try {
            var next = ResizeHandle().chain_from(_resize).fnptr;
            if (next is null) return unchecked((int)0x80004005);
            if (swapChain != _swapChain) return next(swapChain, count, width, height, format, flags);
            if (_resizeBegin is null || _resizeEnd is null) return unchecked((int)0x887A0001);
            return BridgeResizeScope.Run(() => _resizeBegin(swapChain), ticket => _resizeEnd(ticket),
                () => next(swapChain, count, width, height, format, flags), _lifecycle.Fail);
        } catch (Exception error) {
            _lifecycle.Fail(); _logger.Error(error.ToString());
            return unchecked((int)0x80004005);
        }
    }
    public override void render_imgui() {
        if (!_lifecycle.CanRender || !Volatile.Read(ref _resizeReady) || _swapChain == 0 || _frame is null) return;
        var io = ImGui.GetIO();
        uint capture = (io.WantCaptureKeyboard ? 1u : 0u) | (io.WantCaptureMouse ? 2u : 0u);
        _frame(_swapChain, capture);
    }
}
