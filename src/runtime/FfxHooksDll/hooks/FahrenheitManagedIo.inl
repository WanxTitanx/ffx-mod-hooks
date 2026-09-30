// Jarvis-HOOK: compiled inside the existing Ronso producer translation unit.
// The managed provider owns disk I/O. This adapter reuses the native serializer,
// checkpoint selector, observer registry and verified-close tracker.
namespace FfxHooks::RonsoPool::ManagedIo {
enum class Kind {Read,Write};
struct Work {
    IoWork io;
    SaveSession next{};
    NativeSaveCommit::Ticket commit{};
    NativeSaveCommit::FileIdentity file{};
    std::uintptr_t handle=0;
    unsigned char* target=nullptr;
    std::uint64_t ticket=0;
    DWORD thread=0;
    Kind kind=Kind::Read;
    bool transformed=false,opened=false;
};
std::atomic_flag administration=ATOMIC_FLAG_INIT;
// Normal-context End/Abort owns destruction. Process termination must not run
// observer finish callbacks from a global unique_ptr destructor under loader lock.
// An interrupted operation is deliberately retained until the OS retires memory.
Work* active=nullptr;
std::unique_ptr<Work> TakeActive() noexcept {
    std::unique_ptr<Work> work(active);active=nullptr;return work;
}
std::uint64_t serial=0;
NativeSaveCommit::Tracker commits;
struct Guard {
    bool held=!administration.test_and_set(std::memory_order_acquire);
    ~Guard(){if(held)administration.clear(std::memory_order_release);}
};
bool Ready() noexcept {return Coexistence::runtime.SaveServicesAllowed()&&
    Coexistence::runtime.FrameAllowed()&&IsVerifiedSaveIoReady();}
bool Owner(std::uint64_t ticket) noexcept {
    return ticket&&active&&active->ticket==ticket&&active->thread==GetCurrentThreadId();
}
bool Live(const Work& work) noexcept {return Ready()&&work.io.epoch==stateEpoch.load(std::memory_order_acquire);}
bool PathOf(HANDLE handle,std::wstring& path){
    std::array<wchar_t,4097> buffer{};
    const auto length=GetFinalPathNameByHandleW(handle,buffer.data(),static_cast<DWORD>(buffer.size()),FILE_NAME_NORMALIZED);
    if(!length||length>=buffer.size())return false;
    path.assign(buffer.data(),length);return true;
}
bool SlotPath(const wchar_t* input,std::uint32_t slot,std::wstring& path){
    if(!input||slot>INT32_MAX)return false;
    std::array<wchar_t,4097> supplied{},full{};std::size_t length=0;
    for(;length<supplied.size();++length){
        if(!Copy(&supplied[length],input+length,sizeof(wchar_t)))return false;
        if(!supplied[length])break;
    }
    if(length<4||length>=supplied.size())return false;
    const bool absolute=(supplied[1]==L':'&&(supplied[2]==L'/'||supplied[2]==L'\\'))||
        (supplied[0]==L'\\'&&supplied[1]==L'\\');
    if(!absolute)return false;
    const auto count=GetFullPathNameW(supplied.data(),static_cast<DWORD>(full.size()),full.data(),nullptr);
    if(!count||count>=full.size())return false;
    std::wstring expanded(full.data(),count);
    const auto split=expanded.find_last_of(L"/\\");if(split==expanded.npos)return false;
    wchar_t leaf[32]{};swprintf_s(leaf,L"ffx_%03u",slot);
    if(_wcsicmp(expanded.c_str()+split+1,leaf)!=0)return false;
    const auto directory=expanded.substr(0,split+1);
    HANDLE parent=CreateFileW(directory.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(parent==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};std::wstring canonical;
    const bool ok=GetFileInformationByHandle(parent,&info)&&(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&PathOf(parent,canonical);
    CloseHandle(parent);if(!ok)return false;
    if(canonical.back()!=L'\\')canonical+=L'\\';
    path=canonical+leaf;return path.size()<=4096&&OwnerStore::IsSavePath(path);
}
void Retire(Work& work) noexcept {
    if(work.kind==Kind::Read){
        // BeginRead already invalidated this buffer before the attempted I/O.
        NativeSaveEvents::ReadRejected();
    }else{
        if(work.handle){bool inFlight=false;commits.InvalidateStream(work.handle,inFlight);}
        NativeSaveEvents::WriteAborted(work.commit.serial);
        NativeSaveEvents::FinishWrite(work.io.transaction,nullptr,0,false);
    }
}
std::uint64_t BeginWrite(const wchar_t* path,std::uint32_t slot,const unsigned char* input,std::uint32_t size,unsigned char* output){
    Guard guard;if(!guard.held||active||!Ready()||size!=kSaveSize||serial==UINT64_MAX||
       !DataRange(reinterpret_cast<std::uintptr_t>(input),size)||
       !DataRange(reinterpret_cast<std::uintptr_t>(output),size,true))return 0;
    const auto a=reinterpret_cast<std::uintptr_t>(input),b=reinterpret_cast<std::uintptr_t>(output);
    if(a<b+size&&b<a+size)return 0;
    auto work=std::make_unique<Work>();work->kind=Kind::Write;
    if(!SlotPath(path,slot,work->io.path)||!Copy(work->io.input.data(),input,size))return 0;
    work->io.epoch=stateEpoch.load(std::memory_order_acquire);work->thread=GetCurrentThreadId();
    NativeSaveEvents::CheckpointOwnership pool{};
    if(!FfxHooks::RonsoPool::SerializeCheckpoint(work->io.input.data(),work->io.output.data(),size,&pool))return 0;
    work->io.owner={pool.originalMax,pool.charge,pool.maximum};work->io.needsOwner=pool.present!=0;
    if(!NativeSaveEvents::ProjectWrite(work->io.path.c_str(),work->io.output.data(),work->io.projected.data(),size,work->io.transaction))return 0;
    if(work->io.projected!=work->io.output)SealSave(work->io.projected);
    if(!IsValidSave(work->io.projected)||!Live(*work)||
       !NativeSaveEvents::PrepareWrite(work->io.path.c_str(),work->io.projected.data(),size,work->io.transaction)||
       !Copy(output,work->io.projected.data(),size))return 0;
    if(work->io.needsOwner){
        // Owner records are keyed by both canonical path and complete image hash.
        // Stage a new record before touching the primary; failed I/O cannot
        // replace metadata belonging to a different existing image.
        SavedOwner existing{};const auto prior=store.Read(work->io.path,work->io.projected,&existing);
        if(prior==OwnerRead::Found){
            if(existing.originalMax!=work->io.owner.originalMax||existing.charge!=work->io.owner.charge||
               existing.maximum!=work->io.owner.maximum)return 0;
        }else if(prior!=OwnerRead::Missing||!store.Write(work->io.path,work->io.projected,work->io.owner))return 0;
    }
    NativeSaveEvents::WritePrepared(work->io.path.c_str(),work->io.projected.data(),size);
    work->ticket=++serial;const auto result=work->ticket;active=work.release();return result;
}
bool OpenWrite(std::uint64_t ticket,std::uintptr_t handle){
    Guard guard;if(!guard.held||!Owner(ticket)||active->kind!=Kind::Write||active->opened||!Live(*active))return false;
    auto& work=*active;std::wstring actual;LARGE_INTEGER length{};
    const auto file=reinterpret_cast<HANDLE>(handle);
    if(!HandleIdentity(file,work.file)||!PathOf(file,actual)||!GetFileSizeEx(file,&length)||length.QuadPart!=0)return false;
    const auto directory=actual.substr(0,actual.find_last_of(L'\\'));
    const auto expected=work.io.path.substr(0,work.io.path.find_last_of(L'\\'));
    if(_wcsicmp(directory.c_str(),expected.c_str())!=0||_wcsicmp(actual.c_str(),work.io.path.c_str())==0)return false;
    work.commit=commits.BeginWrite(handle,work.file,work.io.epoch,work.thread,work.io.path,work.io.projected);
    if(!work.commit.Valid())return false;
    work.handle=handle;work.opened=true;
    NativeSaveEvents::WriteStaged(work.commit.serial,work.io.path.c_str(),work.io.projected.data(),kSaveSize);
    return Live(work);
}
bool EndWrite(std::uint64_t ticket,bool completed){
    Guard guard;if(!guard.held||!Owner(ticket)||active->kind!=Kind::Write)return false;
    auto work=TakeActive();bool accepted=false;
    try{
        if(completed&&work->opened&&Live(*work)&&commits.FinishWrite(work->commit,true)){
            const auto closing=commits.BeginClose(work->handle,work->file,work->io.epoch,work->thread);
            SaveImage actual{};NativeSaveCommit::FileIdentity identity{};
            const bool read=VerifiedSaveReadback(work->io.path.c_str(),identity,actual);
            accepted=commits.CompleteClose(closing,read&&Live(*work),identity,stateEpoch.load(),GetCurrentThreadId(),actual);
            if(accepted&&work->io.needsOwner){
                SavedOwner owner{};accepted=store.Read(work->io.path,actual,&owner)==OwnerRead::Found&&
                    owner.originalMax==work->io.owner.originalMax&&owner.charge==work->io.owner.charge&&owner.maximum==work->io.owner.maximum;
            }
            if(accepted){
                NativeSaveEvents::FinishWrite(work->io.transaction,actual.data(),kSaveSize,true);
                NativeSaveEvents::WriteCompleted(work->io.path.c_str(),actual.data(),kSaveSize);
                NativeSaveEvents::WriteVerified(work->commit.serial,work->io.path.c_str(),actual.data(),kSaveSize);
            }
        }
    }catch(...){accepted=false;}
    if(!accepted){storageFault.store(true);Retire(*work);}
    return accepted;
}
std::uint64_t BeginRead(const wchar_t* path,std::uint32_t slot,unsigned char* target,std::uint32_t size){
    Guard guard;if(!guard.held||active||!Ready()||size!=kSaveSize||serial==UINT64_MAX||
       !DataRange(reinterpret_cast<std::uintptr_t>(target),size,true))return 0;
    auto work=std::make_unique<Work>();work->target=target;
    if(!SlotPath(path,slot,work->io.path))return 0;
    NativeSaveEvents::ReadStarting(target);
    work->io.epoch=stateEpoch.load();work->thread=GetCurrentThreadId();
    work->ticket=++serial;const auto result=work->ticket;active=work.release();return result;
}
bool TransformRead(std::uint64_t ticket,std::uintptr_t handle,const unsigned char* input,std::uint32_t size){
    Guard guard;if(!guard.held||!Owner(ticket)||active->kind!=Kind::Read||active->transformed||!Live(*active)||size!=kSaveSize||
       !DataRange(reinterpret_cast<std::uintptr_t>(input),size))return false;
    auto& work=*active;std::wstring actualPath;NativeSaveCommit::FileIdentity opened{},readback{};SaveImage actual{};
    if(!HandleIdentity(reinterpret_cast<HANDLE>(handle),opened)||!PathOf(reinterpret_cast<HANDLE>(handle),actualPath)||
       _wcsicmp(actualPath.c_str(),work.io.path.c_str())!=0||!Copy(work.io.input.data(),input,size)||
       !VerifiedSaveReadback(work.io.path.c_str(),readback,actual)||!(opened==readback)||actual!=work.io.input)return false;
    OwnerRead ownership=OwnerRead::Missing;SaveDecision decision=SaveDecision::Invalid;
    if(!PrepareReadWork(work.io,work.next,decision,ownership)||
       (decision!=SaveDecision::Native&&decision!=SaveDecision::Converted)||!Live(work)||
       !Copy(work.target,work.io.output.data(),size))return false;
    work.transformed=true;return true;
}
bool EndRead(std::uint64_t ticket,bool completed){
    Guard guard;if(!guard.held||!Owner(ticket)||active->kind!=Kind::Read)return false;
    auto work=TakeActive();bool accepted=false;
    try{
        SaveImage actual{};
        if(completed&&work->transformed&&Live(*work)&&Copy(actual.data(),work->target,kSaveSize)){
            // The game's CRC checker clears exactly these four bytes. Any other
            // changed byte means the ref buffer no longer belongs to this read.
            if(!actual[25844]&&!actual[25845]&&!actual[25846]&&!actual[25847])
                std::memcpy(actual.data()+25844,work->io.output.data()+25844,4);
            accepted=actual==work->io.output&&IsValidSave(actual);
        }
        if(accepted){
            std::lock_guard<std::mutex> lock(stateMutex);
            accepted=Live(*work);
            if(accepted){session=work->next;activeActor.store(0);actorThread.store(0);
                sessionEpoch=stateEpoch.fetch_add(1)+1;storageFault.store(false);}
        }
        if(accepted)NativeSaveEvents::ReadCompleted(work->io.path.c_str(),work->io.input.data(),
            work->target,kSaveSize,&work->io.selection,work->io.selected.data());
    }catch(...){accepted=false;}
    if(!accepted){storageFault.store(true);Retire(*work);}
    return accepted;
}
bool Abort(std::uint64_t ticket){
    Guard guard;if(!guard.held||!Owner(ticket))return false;
    auto work=TakeActive();Retire(*work);return true;
}
bool CancelRead(unsigned char* target,std::uint32_t size){
    Guard guard;if(!guard.held||(!active&&!Coexistence::runtime.SaveServicesAllowed())||size!=kSaveSize||!DataRange(reinterpret_cast<std::uintptr_t>(target),size,true))return false;
    if(active){if(active->kind!=Kind::Read||active->target!=target||active->thread!=GetCurrentThreadId())return false;
        auto work=TakeActive();Retire(*work);}
    else {NativeSaveEvents::ReadStarting(target);NativeSaveEvents::ReadRejected();}
    return true;
}
} // namespace FfxHooks::RonsoPool::ManagedIo

extern "C" std::uint64_t __cdecl FfxHooks_FahrenheitBeginWriteV2(const wchar_t* path,std::uint32_t slot,const unsigned char* input,std::uint32_t size,unsigned char* output){
    try{return FfxHooks::RonsoPool::ManagedIo::BeginWrite(path,slot,input,size,output);}catch(...){return 0;}
}
extern "C" int __cdecl FfxHooks_FahrenheitOpenWriteV2(std::uint64_t ticket,std::uintptr_t handle){
    try{return FfxHooks::RonsoPool::ManagedIo::OpenWrite(ticket,handle)?1:0;}catch(...){return 0;}
}
extern "C" int __cdecl FfxHooks_FahrenheitEndWriteV2(std::uint64_t ticket,int completed){
    try{return FfxHooks::RonsoPool::ManagedIo::EndWrite(ticket,completed==1)?1:0;}catch(...){return 0;}
}
extern "C" std::uint64_t __cdecl FfxHooks_FahrenheitBeginReadV2(const wchar_t* path,std::uint32_t slot,unsigned char* target,std::uint32_t size){
    try{return FfxHooks::RonsoPool::ManagedIo::BeginRead(path,slot,target,size);}catch(...){return 0;}
}
extern "C" int __cdecl FfxHooks_FahrenheitTransformReadV2(std::uint64_t ticket,std::uintptr_t handle,const unsigned char* input,std::uint32_t size){
    try{return FfxHooks::RonsoPool::ManagedIo::TransformRead(ticket,handle,input,size)?1:0;}catch(...){return 0;}
}
extern "C" int __cdecl FfxHooks_FahrenheitEndReadV2(std::uint64_t ticket,int completed){
    try{return FfxHooks::RonsoPool::ManagedIo::EndRead(ticket,completed==1)?1:0;}catch(...){return 0;}
}
extern "C" int __cdecl FfxHooks_FahrenheitAbortIoV2(std::uint64_t ticket){
    try{return FfxHooks::RonsoPool::ManagedIo::Abort(ticket)?1:0;}catch(...){return 0;}
}
extern "C" int __cdecl FfxHooks_FahrenheitCancelReadV2(unsigned char* target,std::uint32_t size){
    try{return FfxHooks::RonsoPool::ManagedIo::CancelRead(target,size)?1:0;}catch(...){return 0;}
}
