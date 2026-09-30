// SPDX-License-Identifier: LGPL-3.0-or-later
// Original source-pinned cooperative integration, Jarvis-HOOK, 2026-09-29.
using System;
using System.Globalization;
using System.IO;
using System.Threading;

namespace Fahrenheit;

public interface IFhCooperativeClient {
    uint Start(uint protocol,uint providers,Func<string,bool> hasReplacement);
    ulong BeginWrite(string path,int slot,ReadOnlySpan<byte> source,Span<byte> output);
    bool OpenWrite(ulong ticket,nint handle);
    // End consumes an issued ticket even when verification returns false.
    bool EndWrite(ulong ticket,bool completed);
    ulong BeginRead(string path,int slot,nint target,int size);
    bool TransformRead(ulong ticket,nint handle,ReadOnlySpan<byte> disk);
    bool EndRead(ulong ticket,bool accepted);
    void Abort(ulong ticket);
    void CancelRead(nint target,int size);
    nint OpenResource(string normalizedPath);
}

public static class FhCooperativeServices {
    public static FhCooperativeSession Current { get; } = new();
}

public sealed class FhCooperativeSession {
    public const uint Protocol=2,Save=4,Resources=8;
    public const int SaveSize=26880;
    private readonly object _registration=new();
    private object? _saveOwner,_resourceOwner;
    private IFhCooperativeClient? _client;
    private Func<string,bool>? _hasReplacement;
    private uint _providers,_active;
    private int _phase,_io; // 0 registration, 1 bootstrap, 2 active, 3 failed

    public void RegisterSaveProvider(object owner){
        ArgumentNullException.ThrowIfNull(owner);
        lock(_registration){
            if(_phase!=0||(_saveOwner is not null&&!ReferenceEquals(owner,_saveOwner)))throw new InvalidOperationException("Save provider ownership is sealed or already assigned.");
            _saveOwner=owner;_providers|=Save;
        }
    }
    public void RegisterResourceProvider(object owner,Func<string,bool> hasReplacement){
        ArgumentNullException.ThrowIfNull(owner);ArgumentNullException.ThrowIfNull(hasReplacement);
        lock(_registration){
            if(_phase!=0||(_resourceOwner is not null&&!ReferenceEquals(owner,_resourceOwner)))throw new InvalidOperationException("Resource provider ownership is sealed or already assigned.");
            _resourceOwner=owner;_hasReplacement=hasReplacement;_providers|=Resources;
        }
    }
    public void RegisterClient(IFhCooperativeClient client){
        ArgumentNullException.ThrowIfNull(client);
        lock(_registration){
            if(_phase!=0||(_client is not null&&!ReferenceEquals(client,_client)))throw new InvalidOperationException("Cooperative client ownership is sealed or already assigned.");
            _client=client;
        }
    }
    public bool HasFileReplacement(string path)=>_hasReplacement?.Invoke(path)??false;
    public void CompleteBootstrap(bool hooksCommitted){
        IFhCooperativeClient? client;uint providers;
        lock(_registration){
            if(_phase!=0)throw new InvalidOperationException("Cooperative bootstrap is one-shot.");
            _phase=1;client=_client;providers=_providers;
        }
        try{
            if(client is not null){
                if(!hooksCommitted)throw new InvalidOperationException("Fahrenheit hooks did not commit; cooperative services remain closed.");
                uint selected=client.Start(Protocol,providers,HasFileReplacement);
                if((selected&~providers)!=0)throw new InvalidOperationException("Client claimed an absent cooperative provider.");
                Volatile.Write(ref _active,selected);
            }
            Volatile.Write(ref _phase,2);
        }catch{Volatile.Write(ref _active,0);Volatile.Write(ref _phase,3);throw;}
    }
    private IFhCooperativeClient? ClientFor(uint capability){
        var client=Volatile.Read(ref _client);if(client is null)return null;
        if(Volatile.Read(ref _phase)!=2)throw new IOException("Cooperative services are not ready; I/O was not started.");
        return (Volatile.Read(ref _active)&capability)!=0?client:null;
    }
    private static string Validate(string path,int slot,int size){
        ArgumentNullException.ThrowIfNull(path);
        if(slot<0||size!=SaveSize||path.Length>4096||path.Contains('\0')||!Path.IsPathFullyQualified(path))
            throw new ArgumentException("Expected an absolute FFX save path, a final nonnegative slot and the exact image length.");
        string full=Path.GetFullPath(path);
        string leaf="ffx_"+slot.ToString("D3",CultureInfo.InvariantCulture);
        if(!string.Equals(Path.GetFileName(full),leaf,StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("The actual save path does not match the final remapped slot.");
        return full;
    }
    private void EnterIo(){if(Interlocked.CompareExchange(ref _io,1,0)!=0)throw new IOException("Another cooperative save transaction is active.");}

    public bool TryWrite(string path,int slot,ReadOnlySpan<byte> source){
        var client=ClientFor(Save);if(client is null)return false;
        path=Validate(path,slot,source.Length);EnterIo();
        ulong ticket=0;string? temporary=null;
        try{
            byte[] output=new byte[source.Length];
            ticket=client.BeginWrite(path,slot,source,output);
            if(ticket==0)throw new IOException("Native save preparation rejected the transaction before file creation.");
            temporary=Path.Combine(Path.GetDirectoryName(path)!,".ffx-hooks-save-"+Guid.NewGuid().ToString("N")+".tmp");
            using(var stream=new FileStream(temporary,FileMode.CreateNew,FileAccess.Write,FileShare.None,4096,FileOptions.WriteThrough)){
                if(!client.OpenWrite(ticket,stream.SafeFileHandle.DangerousGetHandle()))throw new IOException("Native save staging rejected the opened temporary file.");
                stream.Write(output); // Exactly one image; never retry with raw RAM bytes.
                stream.Flush(flushToDisk:true);
            }
            File.Move(temporary,path,overwrite:true);temporary=null;
            bool verified=client.EndWrite(ticket,true);ticket=0;
            if(!verified)throw new IOException("Primary save was written, but cooperative close/readback or metadata verification failed.");
            return true;
        }finally{
            try{if(ticket!=0)client.Abort(ticket);}
            finally{try{if(temporary is not null)File.Delete(temporary);}finally{Volatile.Write(ref _io,0);}}
        }
    }

    public bool TryRead(string path,int slot,Span<byte> target,Func<bool> validateCrc) =>
        TryRead(path,slot,target,validateCrc,out _);

    // Once this method owns the operation, it owns failure cleanup too. The
    // save UI must still close, but must not cancel an already-retired ticket.
    public unsafe bool TryRead(string path,int slot,Span<byte> target,Func<bool> validateCrc,out bool ownsFailureCleanup){
        ownsFailureCleanup=false;
        var client=ClientFor(Save);if(client is null)return false;
        ArgumentNullException.ThrowIfNull(validateCrc);path=Validate(path,slot,target.Length);EnterIo();
        ownsFailureCleanup=true;
        ulong ticket=0;bool accepted=false,consumed=false;
        fixed(byte* destination=target){
            try{
                // Invalidate this destination before every read attempt, including failed I/O.
                ticket=client.BeginRead(path,slot,(nint)destination,target.Length);
                if(ticket==0)throw new IOException("Native read admission rejected the transaction.");
                byte[] disk=new byte[target.Length];
                using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)){
                    if(stream.Length!=disk.Length)throw new IOException("FFX save image has an unexpected length.");
                    stream.ReadExactly(disk);
                    if(!client.TransformRead(ticket,stream.SafeFileHandle.DangerousGetHandle(),disk))throw new IOException("Native read identity/checkpoint transformation was rejected.");
                }
                if(!validateCrc())throw new InvalidDataException("The game rejected the transformed save checksum.");
                bool completed=client.EndRead(ticket,true);ticket=0;consumed=true;
                if(!completed)throw new IOException("Native read provenance publication was rejected.");
                accepted=true;return true;
            }finally{
                try{
                    if(ticket!=0)client.Abort(ticket);
                    else if(!accepted&&!consumed)client.CancelRead((nint)destination,target.Length);
                }
                finally{if(!accepted)target.Clear();Volatile.Write(ref _io,0);}
            }
        }
    }
    public void CancelPendingRead(nint target,int size){
        var client=ClientFor(Save);if(client is null)return;
        EnterIo();try{client.CancelRead(target,size);}finally{Volatile.Write(ref _io,0);}
    }
    public bool TryOpenResource(string path,bool readOnly,out nint handle){
        handle=0;if(!readOnly)return false;var client=ClientFor(Resources);if(client is null)return false;
        handle=client.OpenResource(path);
        if(handle==(nint)(-1))throw new IOException("Pinned language resource unavailable; refusing incompatible fallback after font publication.");
        return handle!=0;
    }
}
