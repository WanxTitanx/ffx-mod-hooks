// Jarvis-HOOK: registration callbacks may outlive a failed Fahrenheit init.
using System;
using System.Threading;

namespace FfxHooks.FahrenheitBridge;

internal sealed class BridgeLifecycle {
    private const int Cold=0, Initializing=1, Registered=2, Bootstrapping=3, Active=4, Failed=5;
    private int _state;
    public bool CanRender => Volatile.Read(ref _state) == Active;
    public void Fail() => Interlocked.Exchange(ref _state, Failed);
    public bool Initialize(Func<bool> register) => Transition(Cold, Initializing, Registered, register);
    public bool Bootstrap(Func<bool> start) => Transition(Registered, Bootstrapping, Active, start);

    private bool Transition(int before, int busy, int after, Func<bool> action) {
        ArgumentNullException.ThrowIfNull(action);
        if (Interlocked.CompareExchange(ref _state, busy, before) != before) return false;
        bool success = false;
        try { success = action(); }
        finally {
            // A concurrent failure dominates a callback which returns success.
            // An exception also closes admission before propagating to its owner.
            if (Interlocked.CompareExchange(ref _state, success ? after : Failed, busy) != busy)
                success = false;
        }
        return success;
    }
}
