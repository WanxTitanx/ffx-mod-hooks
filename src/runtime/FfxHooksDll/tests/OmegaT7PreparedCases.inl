// Jarvis-HOOK: the normal AI fills an already queued empty packet. Its native
// copy/return path executes here; only the script VM reply is a fixture boundary.
static unsigned preparedCommand=0x400B,preparedCount=1,preparedMode=0;
static void* preparedClaimCode=nullptr;
static __declspec(naked) void __cdecl PreparedClaim(unsigned char*,unsigned char*){
    __asm {
        push ebx
        push edi
        mov ebx,dword ptr [esp+12]
        mov edi,dword ptr [esp+16]
        xor ecx,ecx
        call dword ptr [preparedClaimCode]
        pop edi
        pop ebx
        ret
    }
}
static void __cdecl PreparedReset() {}
static void __cdecl PreparedArgument(unsigned,unsigned,unsigned,unsigned) {}
static int __cdecl PreparedScript(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned) {
    auto* row=reinterpret_cast<unsigned char*>(base+0xD36A78);
    row[3]=static_cast<unsigned char>(preparedCount);
    for(unsigned n=0;n<preparedCount;++n){Put(row+8+16*n,preparedCommand,2);Put(row+10+16*n,0,2);Put(row+16+16*n,1);}
    if(preparedMode==1)R::RequestStop();
    if(preparedMode==2)++context.generation;
    return -1;
}
static int __cdecl PreparedEvent(unsigned,unsigned,unsigned,unsigned,unsigned) {return 0x4A11;}
static void PreparedWrite(unsigned rva,const void* bytes,unsigned count){
    DWORD old=0,ignored=0;Check(VirtualProtect(reinterpret_cast<void*>(base+rva),count,PAGE_EXECUTE_READWRITE,&old)!=0,"private native fixture code is writable during its bounded replacement");
    std::memcpy(reinterpret_cast<void*>(base+rva),bytes,count);VirtualProtect(reinterpret_cast<void*>(base+rva),count,old,&ignored);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base+rva),count);
}
static void PreparedPacketCases(){
    namespace Q=FfxHooks::SharedQueue;
    // Execute the actual caller's row/actor claim instructions. State1 means
    // that native owns the row; count0 means AI has not prepared a command yet.
    const unsigned claimRva=0x3924BB,claimBytes=0x21;
    auto* claim=static_cast<unsigned char*>(VirtualAlloc(nullptr,claimBytes+1,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(claim!=nullptr,"native queue-claim fixture allocation");if(!claim)return;
    std::memcpy(claim,reinterpret_cast<void*>(base+claimRva),claimBytes);claim[claimBytes]=0xC3;
    DWORD claimProtection=0;Check(VirtualProtect(claim,claimBytes+1,PAGE_EXECUTE_READ,&claimProtection)!=0,"native claim instructions are executable in the isolated fixture");
    FlushInstructionCache(GetCurrentProcess(),claim,claimBytes+1);preparedClaimCode=claim;
    const unsigned rvas[]={0x397FF0,0x397B70,0x397D50,0x3A07B0};
    const void* replacements[]={reinterpret_cast<void*>(&PreparedReset),reinterpret_cast<void*>(&PreparedArgument),reinterpret_cast<void*>(&PreparedScript),reinterpret_cast<void*>(&PreparedEvent)};
    std::array<std::array<unsigned char,5>,4> originals{};
    for(unsigned i=0;i<4;++i){std::memcpy(originals[i].data(),reinterpret_cast<void*>(base+rvas[i]),5);unsigned char jump[5]={0xE9};Put(jump+1,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(replacements[i])-base-rvas[i]-5));PreparedWrite(rvas[i],jump,5);}
    using Prepare=int(__cdecl*)(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned char*);
    const auto prepare=reinterpret_cast<Prepare>(base+0x3ACEB0);
    auto* q=reinterpret_cast<unsigned char*>(base+P::Rva<0xD2AC70>());
    auto empty=[&](unsigned owner=20){
        *reinterpret_cast<unsigned char*>(base+P::Rva<0xD2BDE1>())=0;
        std::array<unsigned char,72> row{};row[0]=static_cast<unsigned char>(owner);
        // Real queued placeholders have opaque unused payload bytes.
        std::memset(row.data()+8,0xA5,64);
        Check(Q::Shim(owner,row.data(),0,0,0)==-1&&Count()==1&&q[3]==0,"native append reserves an empty AI packet without fabricating an attack");
        PreparedClaim(q,Actor(owner));
        Check(q[3]==0&&q[4]==1&&Actor(owner)[0xDE5]==0&&Actor(owner)[0xDF0]==2,"the actual dispatcher claims an empty row before invoking the AI");
    };
    Battle(15);
#ifdef FFXHOOKS_OMEGA_AI_READY_V1
    const auto aiByte=*reinterpret_cast<unsigned char*>(base+O::Evidence::AiPrepareRva+31);const auto altered=static_cast<unsigned char>(aiByte^1u);
    PreparedWrite(O::Evidence::AiPrepareRva+31,&altered,1);
    Check(!R::EnableAiProducerForTests(),"changed native AI preparation witness refuses installation");
    PreparedWrite(O::Evidence::AiPrepareRva+31,&aiByte,1);
    Check(R::EnableAiProducerForTests(),"the exact native AI preparation detour installs");
    PreparedWrite(O::Evidence::AiPrepareRva+31,&altered,1);
    Check(!R::EnableAiProducerForTests(),"owned AI preparation proof rejects later byte drift");
    PreparedWrite(O::Evidence::AiPrepareRva+31,&aiByte,1);
    Check(R::EnableAiProducerForTests(),"restoring the owned entry regains its exact proof");
#endif
    for(unsigned command:{0x400Bu,0x3045u})for(unsigned repeat=0;repeat<2;++repeat){
        Battle(command==0x400B?15:16);preparedCommand=command;preparedCount=1;preparedMode=0;empty();
        std::array<unsigned char,16> input{};std::memcpy(input.data(),reinterpret_cast<void*>(base+P::Rva<0xD2AA80>()),input.size());
        Check(prepare(20,255,0,1,0,q)==-1,"native AI completion preserves the script result rather than the later event callback result");
        Check(Count()==1&&q[0]==20&&q[1]==0&&q[2]==0&&q[3]==2&&N::Word(q+8)==command&&N::Word(q+24)==bindings.Get(O::Catalog[profileNow].skill).id,
            "an actual native AI-filled placeholder gains one extra before the first command starts");
        Check(!std::memcmp(input.data(),reinterpret_cast<void*>(base+P::Rva<0xD2AA80>()),input.size()),"late AI composition never creates or rewrites native input tickets");
        Actor(20)[0xDE5]=0;q[4]=1;
        std::array<unsigned char,72> counter{};counter[1]=counter[3]=1;Put(counter.data()+8,0x3000,2);Put(counter.data()+10,255,2);Put(counter.data()+16,1u<<20);
        using Insert=int(__cdecl*)(unsigned,const unsigned char*,unsigned,unsigned,unsigned,unsigned,unsigned);
        Check(reinterpret_cast<Insert>(base+0x3B0A30)(0,counter.data(),0,0,1,0,20)==-1&&q[72]==0&&q[73]==1,"the actual native counter remains behind the AI-filled combined packet");
        q[2]=1;
        Check(reinterpret_cast<unsigned char*(__cdecl*)()>(base+0x3B0A10)()==q&&q[2]<q[3],"the native head retains the AI extra after the first subcommand");
        Finish(20);Check(Count()==1&&q[0]==0,"the AI-built normal-plus-extra packet retires once before the counter");
        Actor(0)[0xDE5]=0;Finish(0);Check(Count()==0,"the native counter then drains normally");
    }
    for(unsigned count:{2u,3u,4u}){
        Battle(15);preparedCount=count;preparedCommand=0x400B;preparedMode=0;empty();prepare(20,255,0,1,0,q);
        Check(q[3]==(count<4?count+1:count),"AI preparation respects native four-command capacity");
        bool same=true;for(unsigned n=0;n<count;++n)same=same&&N::Word(q+8+16*n)==0x400B;
        Check(same,"all AI-authored original commands retain their order and bytes");Finish(20);
    }
    preparedCount=1;
    for(unsigned mode:{1u,2u}){
        Battle(15);preparedMode=mode;empty();prepare(20,255,0,1,0,q);
        Check(q[3]==1,"stop or battle replacement during native AI cannot append stale extra work");Finish(20);
    }
    preparedMode=0;
    Battle(15);empty(0);prepare(0,255,0,1,0,q);Check(q[3]==1,"player preparation remains native-only");Finish(0);
    Battle(15);empty();prepare(20,255,3,1,0,q);Check(q[3]==1,"a reaction script filling a row does not gain an Omega chain");Finish(20);
    Battle(15);empty();preparedCommand=0x61DF;prepare(20,255,0,1,0,q);Check(q[3]==1,"native monster overdrive remains outside the extra policy");Finish(20);
    Battle(15);empty();preparedCommand=0x400B;q[3]=1;Put(q+8,preparedCommand,2);
    prepare(20,255,0,1,0,q);Check(q[3]==1&&q[4]==1,"a previously prepared command cannot be extended by the preparation observer");Finish(20);
    Battle(15);empty();std::array<unsigned char,72> foreign{};prepare(20,255,0,1,0,foreign.data());
    Check(foreign[3]==1&&q[3]==0,"a native AI output outside the current queue receives no extra");Finish(20);
    for(unsigned i=0;i<4;++i)PreparedWrite(rvas[i],originals[i].data(),5);
    preparedClaimCode=nullptr;VirtualFree(claim,0,MEM_RELEASE);
    preparedMode=0;
}
