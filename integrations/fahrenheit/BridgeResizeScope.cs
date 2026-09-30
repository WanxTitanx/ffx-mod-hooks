// Jarvis-HOOK: hold native render exclusion across the entire DXGI operation.
using System;

namespace FfxHooks.FahrenheitBridge;

internal static class BridgeResizeScope {
    internal static int Run(Func<uint> begin, Func<uint, int> end, Func<int> next, Action fail) {
        uint ticket = begin();
        // Contention must not forward ResizeBuffers while native resources are
        // still bound. This is an ordinary DXGI failure; the caller may retry.
        if (ticket == 0) return unchecked((int)0x887A0001);
        try { return next(); }
        finally { if (end(ticket) != 1) fail(); }
    }
}
