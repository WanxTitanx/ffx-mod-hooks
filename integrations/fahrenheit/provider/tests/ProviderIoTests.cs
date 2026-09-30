using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
#if HAS_PROVIDER
using Fahrenheit;

internal sealed class Client : IFhCooperativeClient {
    public bool RejectPrepare,RejectOpen,RejectRead,RejectEnd;
    public bool RejectBeginRead=false,RejectAbort=false;
    public int Prepares,Opens,Ends,Aborts,Reads,Transforms,Cancels;
    public string? Path;public byte[]? Before;public nint Target;public int Size;
    public Func<bool>? Reenter;public bool ReentryRejected;
    public uint Start(uint protocol,uint providers,Func<string,bool> conflict) => protocol==2?providers:0;
    public ulong BeginWrite(string path,int slot,ReadOnlySpan<byte> source,Span<byte> output){
        ++Prepares;Path=path;
        if(Before is not null&&!File.ReadAllBytes(path).SequenceEqual(Before))throw new Exception("primary changed before projection");
        if(Reenter is not null){try{Reenter();}catch(IOException){ReentryRejected=true;}}
        if(RejectPrepare)return 0;source.CopyTo(output);output[17]=0xA5;return 7;
    }
    public bool OpenWrite(ulong ticket,nint handle){++Opens;return ticket==7&&handle!=0&&!RejectOpen;}
    public bool EndWrite(ulong ticket,bool success){++Ends;return ticket==7&&success&&!RejectEnd&&File.ReadAllBytes(Path!)[17]==0xA5;}
    public ulong BeginRead(string path,int slot,nint target,int size){++Reads;Path=path;Target=target;Size=size;return RejectBeginRead?0u:8u;}
    public bool TransformRead(ulong ticket,nint handle,ReadOnlySpan<byte> disk){
        ++Transforms;if(RejectRead)return false;byte[] bytes=disk.ToArray();bytes[19]=0xCC;Marshal.Copy(bytes,0,Target,Size);return ticket==8&&handle!=0;
    }
    public bool EndRead(ulong ticket,bool accepted){++Ends;return ticket==8&&accepted&&!RejectEnd;}
    public void Abort(ulong ticket){++Aborts;if(RejectAbort)throw new IOException("Rejected native abort fixture");}
    public void CancelRead(nint target,int size){++Cancels;}
    public nint OpenResource(string request)=>request=="/font"?(nint)42:0;
}
internal static class ProviderIoTests {
    private static int _checks,_failed;
    private static void Check(bool value,string label){++_checks;if(!value){++_failed;Console.WriteLine("FAIL: "+label);}}
    private static FhCooperativeSession Session(Client client){var session=new FhCooperativeSession();session.RegisterSaveProvider(client);session.RegisterResourceProvider(client,p=>p=="/conflict");session.RegisterClient(client);session.CompleteBootstrap(true);return session;}
    private static bool Throws(Action action){try{action();return false;}catch(IOException){return true;}catch(InvalidDataException){return true;}catch(InvalidOperationException){return true;}catch(ArgumentException){return true;}}
    public static int Main(){
        string root=System.IO.Path.Combine(System.IO.Path.GetTempPath(),"ffx-provider-test-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
        try{
            string path=System.IO.Path.Combine(root,"ffx_1000");
            byte[] original=Enumerable.Repeat((byte)0x11,26880).ToArray();File.WriteAllBytes(path,original);
            var client=new Client{Before=original};var session=Session(client);byte[] source=(byte[])original.Clone();
            Check(session.TryWrite(path,1000,source),"negotiated provider handles the actual final slot");
            Check(client.Prepares==1&&client.Opens==1&&client.Ends==1&&client.Aborts==0,"one preparation/open/completion and no abort on success");
            Check(source.SequenceEqual(original)&&File.ReadAllBytes(path)[17]==0xA5,"source RAM is untouched and projected bytes reach the primary");
            Check(Directory.GetFiles(root).Length==1,"successful transaction leaves no temporary file");
            Check(session.HasFileReplacement("/conflict")&&!session.HasFileReplacement("/clean"),"source conflicts come from the actual loader index");
            Check(session.TryOpenResource("/font",true,out nint handle)&&handle==42,"read-only resource route transfers the selected handle");
            Check(!session.TryOpenResource("/font",false,out _),"write access never redirects a language resource");
            Check(Throws(()=>session.RegisterClient(new Client())),"bootstrap seals client registration");
            foreach(int failure in new[]{0,1}){
                File.WriteAllBytes(path,original);var bad=new Client{Before=original,RejectPrepare=failure==0,RejectOpen=failure==1};var rejected=Session(bad);
                Check(Throws(()=>rejected.TryWrite(path,1000,source)),"projection/open rejection is surfaced, not retried as a raw save");
                Check(File.ReadAllBytes(path).SequenceEqual(original),"rejected preparation preserves the complete prior primary");
                Check(bad.Ends==0&&bad.Aborts==(failure==0?0:1)&&Directory.GetFiles(root).Length==1,"rejection retires only an issued ticket and cleans its temporary");
            }
            var finishFailure=new Client{RejectEnd=true};var failedFinish=Session(finishFailure);
            Check(Throws(()=>failedFinish.TryWrite(path,1000,source))&&finishFailure.Ends==1&&finishFailure.Aborts==0,"failed verification consumes its ticket and never retries the write");
            Check(File.ReadAllBytes(path)[17]==0xA5&&Directory.GetFiles(root).Length==1,"post-write verification failure reports the actual saved image without deleting it");
            var concurrent=new Client();var guard=Session(concurrent);concurrent.Reenter=()=>guard.TryWrite(path,1000,source);
            Check(guard.TryWrite(path,1000,source)&&concurrent.ReentryRejected&&concurrent.Prepares==1,"nested I/O is rejected before reaching the transport");
            var invalidClient=new Client();var invalid=Session(invalidClient);
            Check(Throws(()=>invalid.TryWrite(path,1,source))&&invalidClient.Prepares==0,"UI slot mismatch cannot manufacture save provenance");
            Check(Throws(()=>invalid.TryWrite(path,1000,new byte[4]))&&invalidClient.Prepares==0,"wrong image length is rejected before file creation");
            File.WriteAllBytes(path,original);var reader=new Client();var readSession=Session(reader);byte[] target=new byte[26880];
            Check(readSession.TryRead(path,1000,target,()=>target[19]==0xCC),"native validation runs after transport transformation");
            Check(reader.Reads==1&&reader.Transforms==1&&reader.Ends==1&&reader.Aborts==0,"successful read publishes one validated ticket");
            foreach(int failure in new[]{0,1,2}){
                File.WriteAllBytes(path,failure==0?new byte[3]:original);var bad=new Client{RejectRead=failure==1};var badSession=Session(bad);Array.Fill(target,(byte)99);
                Check(Throws(()=>badSession.TryRead(path,1000,target,()=>false)),"short reads, rejected transforms and CRC failures are terminal");
                Check(bad.Reads==1&&bad.Ends==0&&bad.Aborts==1&&target.All(b=>b==0),"failure invalidates provenance and clears the destination");
            }
            var noClient=new FhCooperativeSession();noClient.CompleteBootstrap(true);
            Check(!noClient.TryWrite(path,1000,source),"absence of a bridge preserves the original provider path");
            var notStarted=new FhCooperativeSession();notStarted.RegisterSaveProvider(client);notStarted.RegisterClient(client);
            Check(Throws(()=>notStarted.TryWrite(path,1000,source)),"registered client cannot service I/O before hooks commit");
            Check(Throws(()=>notStarted.CompleteBootstrap(false)),"failed framework hook commit cannot advertise services");
        }finally{Directory.Delete(root,true);}
        Console.WriteLine($"Cooperative provider I/O: {_checks} checks, {_failed} failures");return _failed==0?0:1;
    }
}
#else
internal static class ProviderIoTests { public static int Main(){Console.WriteLine("FAIL: cooperative provider transaction implementation is missing");return 1;} }
#endif
