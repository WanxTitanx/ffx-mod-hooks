// Jarvis-HOOK: tests the actual managed lifecycle without loading a game or DLL.
using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
#if HAS_BRIDGE_LIFECYCLE
using FfxHooks.FahrenheitBridge;
#endif

internal static class BridgeLifecycleTests {
    private static int _checks, _failures;
    private static void Check(bool result, string message) {
        ++_checks;
        if (!result) { ++_failures; Console.WriteLine("FAIL: " + message); }
    }
    public static int Main() {
#if HAS_BRIDGE_LIFECYCLE
        var failed = new BridgeLifecycle();
        int started = 0;
        Check(!failed.Initialize(() => false), "partial initialization fails");
        Check(!failed.Bootstrap(() => { ++started; return true; }) && started == 0,
            "a callback left registered by failed init never starts native code");
        Check(!failed.CanRender && !failed.Initialize(() => true), "failed init is terminal");

        var exceptional = new BridgeLifecycle();
        try { exceptional.Initialize(() => throw new InvalidOperationException("fixture")); }
        catch (InvalidOperationException) { }
        Check(!exceptional.Bootstrap(() => true) && !exceptional.CanRender,
            "an initialization exception closes admission before propagation");

        var ready = new BridgeLifecycle();
        Check(!ready.Bootstrap(() => true), "bootstrap cannot run before init");
        Check(ready.Initialize(() => true) && !ready.CanRender, "registration does not grant frame ownership");
        Parallel.For(0, 32, _ => ready.Bootstrap(() => { Interlocked.Increment(ref started); return true; }));
        Check(started == 1 && ready.CanRender, "competing update callbacks start exactly one native bootstrap");
        ready.Fail(); ready.Fail();
        Check(!ready.CanRender && !ready.Bootstrap(() => true), "a failed active bridge cannot restart");

        var rejected = new BridgeLifecycle(); rejected.Initialize(() => true);
        Check(!rejected.Bootstrap(() => false) && !rejected.CanRender, "native rejection remains terminal");
        Check(!rejected.Bootstrap(() => { ++started; return true; }) && started == 1, "native rejection is not retried");

        var interrupted = new BridgeLifecycle(); interrupted.Initialize(() => true);
        Check(!interrupted.Bootstrap(() => { interrupted.Fail(); return true; }) && !interrupted.CanRender,
            "stop racing with successful bootstrap wins");
        var interruptedInit = new BridgeLifecycle();
        Check(!interruptedInit.Initialize(() => { interruptedInit.Fail(); return true; }) &&
            !interruptedInit.Bootstrap(() => true), "stop racing with registration wins");
#else
        Check(false, "managed bridge has no fail-closed lifecycle controller");
#endif
        TestResizeScope();
        Console.WriteLine($"Bridge lifecycle: {_checks} checks, {_failures} failures");
        return _failures == 0 ? 0 : 1;
    }
    private static void TestResizeScope() {
#if HAS_RESIZE_SCOPE
        var calls = new List<string>();
        const int dxgiFailure = unchecked((int)0x887A0001);
        int failureSignals = 0;
        int result = BridgeResizeScope.Run(() => { calls.Add("begin"); return 42; },
            ticket => { calls.Add("end"); Check(ticket == 42, "resize ends its exact ticket"); return 1; },
            () => { calls.Add("resize"); return 0; }, () => ++failureSignals);
        Check(result == 0 && string.Join(",", calls) == "begin,resize,end",
            "native exclusion covers the entire downstream ResizeBuffers call");
        calls.Clear();
        result = BridgeResizeScope.Run(() => 0, _ => { calls.Add("end"); return 1; },
            () => { calls.Add("resize"); return 0; }, () => ++failureSignals);
        Check(result == dxgiFailure && calls.Count == 0 && failureSignals == 0,
            "busy resize never forwards without owning the scope and remains retryable");
        result = BridgeResizeScope.Run(() => 7, _ => { calls.Add("end"); return 1; },
            () => dxgiFailure, () => ++failureSignals);
        Check(result == dxgiFailure && calls.Count == 1,
            "downstream failure is preserved and still releases the scope");
        calls.Clear();
        try {
            BridgeResizeScope.Run(() => 9, _ => { calls.Add("end"); return 1; },
                () => throw new InvalidOperationException("downstream"), () => ++failureSignals);
            Check(false, "downstream exception must reach the callback boundary");
        } catch (InvalidOperationException) { Check(calls.Count == 1, "exception releases its resize scope exactly once"); }
        result = BridgeResizeScope.Run(() => 11, _ => 0, () => 0, () => ++failureSignals);
        Check(result == 0 && failureSignals == 1,
            "failed scope release disables bridge delivery without inventing a downstream result");
#else
        Check(false, "managed resize lacks exclusion spanning the downstream call");
#endif
    }
}
