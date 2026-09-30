// The runner includes the actual patched save/load/cancel methods. This host
// replaces the game endpoints; FhCooperativeSession and its cleanup are real.
using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;

namespace Fahrenheit {
internal enum FhGameId { FFX, FFX2 }
internal enum FhSaveSystemMode { SAVE, LOAD, ALBD }
internal enum FhSaveSystemState { SAVE, LOAD }
internal enum FhSaveDialogState { CLOSED }
internal static class FhGlobal { internal static FhGameId game_id=FhGameId.FFX; }
internal interface IFhSaveSystemImpl { void load(int slot); }
internal sealed class Fn<T> where T:Delegate { internal T fnptr;internal Fn(T fn){fnptr=fn;} }
internal static class FhCall {
    internal static int Crc=1,Completions;
    internal static Fn<Func<int>> SaveDataCheckCrc=new(()=>Crc);
    internal static Fn<Func<byte,bool>> isNeedRenamePlayer=new(_=>false);
    internal static Fn<Action<FhSaveSystemState>> SaveDataSaveLoadSucceed=new(_=>++Completions);
}
internal static class FhUtil { internal static void set_at(nint address,bool value){} }
internal static unsafe class FhSavePal {
    internal static nint Buffer;internal static int Cancel,Closes;
    internal static byte* pal_addr_buf_save()=>(byte*)Buffer;
    internal static int pal_sz_buf_save()=>26880;
    internal static int pal_header_offset_playerrename()=>0;
    internal static nint pal_addr_force_player_rename()=>0;
    internal static void pal_set_cancel_state(int value){Cancel=value;}
    internal static void pal_set_dialog_state(FhSaveDialogState value){++Closes;}
}
internal static class FhApi {
    internal static class Saves {
        internal static string Root="";
        internal static string get_save_path_for_slot(int slot)=>Path.Join(Root,$"ffx_{slot:D3}");
    }
    internal static class Events { internal static class Common { internal static class GameLoop {
        internal static Emitter PostCloseSaveMenu=new();
    } } }
    internal sealed class Emitter { internal int Count;internal void invoke(EventArgs args){++Count;} }
}
internal unsafe partial class ProviderLoadHost:IFhSaveSystemImpl {
    private FhSaveSystemMode _mode=FhSaveSystemMode.LOAD;
    private int _load_pending_slot=-1;
    internal int Pending=>_load_pending_slot;
}
internal static unsafe class ProviderCompositionTests {
    private static int checks,failures;
    private static void Check(bool ok,string label){++checks;if(!ok){++failures;Console.WriteLine("FAIL: "+label);}}
    private static void Reset(Client client){
        client.Aborts=client.Cancels=client.Reads=client.Transforms=client.Ends=0;
        client.RejectRead=client.RejectEnd=client.RejectBeginRead=client.RejectAbort=false;
        FhCall.Crc=1;FhCall.Completions=FhSavePal.Closes=FhApi.Events.Common.GameLoop.PostCloseSaveMenu.Count=0;
        FhSavePal.Cancel=-1;
        new Span<byte>((void*)FhSavePal.Buffer,26880).Fill(99);
    }
    private static Exception? Load(ProviderLoadHost host){try{((IFhSaveSystemImpl)host).load(1000);return null;}catch(Exception error){return error;}}
    public static int Main(){
        var root=Path.Join(Path.GetTempPath(),"ffx-provider-composition-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
        FhApi.Saves.Root=root;FhSavePal.Buffer=Marshal.AllocHGlobal(26880);
        var client=new Client();var session=FhCooperativeServices.Current;
        session.RegisterSaveProvider(client);session.RegisterResourceProvider(client,_=>false);session.RegisterClient(client);session.CompleteBootstrap(true);
        byte[] disk=Enumerable.Repeat((byte)0x11,26880).ToArray();string path=FhApi.Saves.get_save_path_for_slot(1000);
        try {
            File.WriteAllBytes(path,disk);Reset(client);FhCall.Crc=0;
            var host=new ProviderLoadHost();var error=Load(host);
            Check(error is null,"the game's negative CRC verdict closes the UI without a new unhandled exception");
            Check(client.Aborts==1&&client.Cancels==0,"one failed issued read publishes one retirement, not a second UI cancel");
            Check(FhSavePal.Cancel==1&&FhSavePal.Closes==1&&FhCall.Completions==1&&host.Pending==-1,"CRC rejection signals one failed UI transition and never publishes a loaded slot");
            Check(new ReadOnlySpan<byte>((void*)FhSavePal.Buffer,26880).ToArray().All(v=>v==0),"rejected load clears the actual ref buffer");
            Reset(client);File.WriteAllBytes(path,new byte[3]);error=Load(new());
            Check(error is IOException,"real file I/O failure retains upstream exception policy");
            Check(client.Aborts==1&&client.Cancels==0&&FhSavePal.Closes==1,"short read does not reset observers twice");
            Reset(client);File.WriteAllBytes(path,disk);client.RejectEnd=true;error=Load(new());
            Check(error is IOException&&client.Ends==1&&client.Aborts==0&&client.Cancels==0,"consumed failed EndRead is not followed by another cancellation");
            Reset(client);client.RejectBeginRead=true;error=Load(new());
            Check(error is IOException&&client.Reads==1&&client.Aborts==0&&client.Cancels==1,"unissued read invalidates prior provenance exactly once");
            Reset(client);client.RejectAbort=true;FhCall.Crc=0;error=Load(new());
            Check(error is IOException&&client.Aborts==1&&client.Cancels==0,"failed cleanup is surfaced and never blindly attempted again by the UI");
            Reset(client);host=new();error=Load(host);
            Check(error is null&&host.Pending==1000&&FhSavePal.Cancel==0&&client.Ends==1&&client.Aborts==0&&client.Cancels==0,"fresh successful load recovers with one native completion");
            host.signal_exit_abort();Check(client.Cancels==1&&FhSavePal.Cancel==1,"ordinary explicit UI cancel still revokes pending provenance");
        }finally{Marshal.FreeHGlobal(FhSavePal.Buffer);Directory.Delete(root,true);}
        Console.WriteLine($"Provider save/UI composition: {checks} checks, {failures} failures");return failures==0?0:1;
    }
}
}
