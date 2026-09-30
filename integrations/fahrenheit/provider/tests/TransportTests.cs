// Jarvis-HOOK: CLR x86 -> production native DLL -> real filesystem.
// The Fahrenheit marker is a test DLL; the original game entrypoint is never run.
using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using Fahrenheit;
using FfxHooks.FahrenheitBridge;

internal static unsafe class TransportTests {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern nint LoadLibraryExW(string path, nint file, uint flags);
    [StructLayout(LayoutKind.Sequential)] private struct Status {public uint Size,Abi,Phase,Caps,Save,Frames;}
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Query(Status* output,uint size);
    private static int _checks,_failed;
    private static void Check(bool value,string label){++_checks;if(!value){++_failed;Console.WriteLine("FAIL: "+label);}}
    public static int Main(string[] args){
        if(args.Length!=4||IntPtr.Size!=4){Console.Error.WriteLine("TransportTests.dll NATIVE_DLL FFX_FIXTURE SAVE_FIXTURE OWNED_ROOT (x86 CLR)");return 2;}
        string root=Path.GetFullPath(args[3]);Directory.CreateDirectory(root);
        Environment.SetEnvironmentVariable("TEMP",root);Environment.SetEnvironmentVariable("TMP",root);
        Environment.CurrentDirectory=root;
        nint marker=NativeLibrary.Load(Path.Join(root,"fhstage1.dll"));
        nint provider=NativeLibrary.Load(Path.Join(root,"minhook.x32.dll"));
        Check(marker!=0&&provider!=0,"owned marker and independent real MinHook provider load");
        nint game=LoadLibraryExW(Path.GetFullPath(args[1]),0,1); // DONT_RESOLVE_DLL_REFERENCES
        Check(game!=0,"private FFX image maps without invoking its entrypoint");
        if(game==0)return 1;
        nint library=NativeLibrary.Load(Path.GetFullPath(args[0]));
        var query=Marshal.GetDelegateForFunctionPointer<Query>(NativeLibrary.GetExport(library,"FfxHooks_FahrenheitQueryV1"));
        Status status=default;
        for(int attempt=0;attempt<200;++attempt){query(&status,(uint)sizeof(Status));if(status.Phase!=0)break;Thread.Sleep(10);}
        Check(status.Phase==2&&status.Save==0,"native initial worker waits for explicit provider bootstrap");
        var client=new CooperativeClient(library);
        var session=new FhCooperativeSession();
        session.RegisterSaveProvider(new object());session.RegisterResourceProvider(new object(),_=>false);session.RegisterClient(client);
        try{session.CompleteBootstrap(true);}
        catch(Exception error){Console.Error.WriteLine(error);return 1;}
        Check(query(&status,(uint)sizeof(Status))==1&&status.Phase==7&&status.Caps==3&&status.Save==1,"V2 activates the production save owner without changing the V1 frame ABI");
        byte[] original=File.ReadAllBytes(args[2]),target=new byte[26880];
        string path=Path.Join(root,"ffx_1000"),other=Path.Join(root,"ffx_1001");
        File.WriteAllBytes(path,original);
        Check(session.TryRead(path,1000,target,()=>true)&&target.SequenceEqual(original),"actual managed read crosses x86 ABI and returns exact vanilla bytes");
        Check(session.TryWrite(other,1001,original)&&File.ReadAllBytes(other).SequenceEqual(original),"actual managed Save As crosses native serialization and verified-close publication");
        Check(!Directory.EnumerateFiles(root,".ffx-hooks-save-*").Any(),"successful round trip leaves no temporary primary");
        Check(!session.TryOpenResource("/unclaimed/resource",true,out nint handle)&&handle==0,"unclaimed resources return to the Fahrenheit loader");
        Check(!session.TryOpenResource("/unclaimed/resource",false,out handle),"writes are never intercepted by resource delivery");
        bool rejected=false;
        try{session.TryRead(path,1000,target,()=>false);}catch(InvalidDataException){rejected=true;}
        Check(rejected&&target.All(v=>v==0),"failed native CRC admission clears the ref buffer and aborts provenance");
        Check(session.TryRead(path,1000,target,()=>true),"a fresh read recovers from a canceled generation");
        bool invalidCancel=false;
        try{client.CancelRead(0,26880);}catch(IOException){invalidCancel=true;}
        Check(invalidCancel,"managed caller observes native cancellation refusal");
        fixed(byte* buffer=target){
            nint address=(nint)buffer;
            ulong pending=client.BeginRead(path,1000,address,target.Length);
            Check(pending!=0,"direct ABI test holds one real read transaction");
            bool foreignRejected=false;
            var foreign=new Thread(()=>{try{client.CancelRead(address,target.Length);}catch(IOException){foreignRejected=true;}});
            foreign.Start();foreign.Join();
            Check(foreignRejected,"foreign-thread cancellation cannot retire the current owner's read");
            client.CancelRead(address,target.Length);
            Check(!client.EndRead(pending,true),"owning cancellation consumes the actual native ticket");
            bool staleAbort=false;
            try{client.Abort(pending);}catch(IOException){staleAbort=true;}
            Check(staleAbort,"managed caller observes stale abort rejection");
        }
        Check(session.TryRead(path,1000,target,()=>true),"valid read remains usable after refused cancellation attempts");
        Console.WriteLine($"Fahrenheit CLR/native transport RT1: {_checks} checks, {_failed} failures");
        return _failed==0?0:1;
    }
}
