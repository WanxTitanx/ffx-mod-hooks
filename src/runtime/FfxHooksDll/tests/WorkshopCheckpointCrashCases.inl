// Jarvis-HOOK: kill only this isolated store-test child, never the game process.
static bool CrashSuccessor(SaveImage& image,workshop::State& state){
    std::uint32_t gil=0;std::memcpy(&gil,image.data()+0x3D88,4);
    if(!gil||state.revision==UINT64_MAX||state.rolls==UINT64_MAX)return false;
    --gil;std::memcpy(image.data()+0x3D88,&gil,4);FfxHooks::RonsoPool::SealSave(image);
    ++state.revision;++state.rolls;state.rng^=0x13579BDFu;return true;
}
static int CheckpointCrashChild(char** argv){
    const std::wstring root(argv[2],argv[2]+std::strlen(argv[2]));
    const std::wstring path(argv[4],argv[4]+std::strlen(argv[4]));
    if(std::strlen(argv[5])!=1||argv[5][0]<'0'||argv[5][0]>'4')return 201;
    SaveImage disk{},selected{};std::ifstream input(argv[1],std::ios::binary);
    if(!input.read(reinterpret_cast<char*>(disk.data()),disk.size()))return 202;
    Store store;if(!store.Initialize(root,false))return 203;
    FfxHooks::NativeSaveEvents::CheckpointSelection selection{};workshop::State state{};Hash anchor{};
    if(!Fingerprint(disk.data(),disk.size(),anchor)||
       store.SelectCheckpoint(path,disk,selected,selection)!=StoreResult::Found||
       store.ReadCheckpointLoaded(path,anchor,selection.proof,selected,state)!=StoreResult::Found||
       !CrashSuccessor(selected,state))return 204;
    Store::CrashCheckpointWriteForTests(argv[5][0]-'0');
    if(!store.PublishCheckpoint(path,anchor,selected,selection.pool,state,selection.proof))return 205;
    return 206; // a requested abrupt boundary was not reached: test failure
}
static void CheckpointCrashCases(const char* fixture,const std::wstring& root,const SaveImage& disk,
                                 const workshop::State& original){
    wchar_t exe[32768]{};const DWORD length=GetModuleFileNameW(nullptr,exe,32768);
    Check(length>0&&length<32768,"process-crash fixture resolves only its own executable");
    if(!length||length>=32768)return;
    const std::wstring fixtureWide(fixture,fixture+std::strlen(fixture));
    Hash anchor{};Fingerprint(disk.data(),disk.size(),anchor);
    for(int point=0;point<5;++point){
        Store store;Check(store.Initialize(root,false),"crash fixture opens the existing private storage");
        const auto path=root+L"\\ffx_07"+std::to_wstring(point);
        SaveImage paidImage=disk;auto paid=original;paid.revision+=200;paid.rng^=0x785u;
        Hash head{};Check(store.PublishCheckpoint(path,anchor,paidImage,{},paid,head),"crash fixture seeds one complete accepted pair");
        auto successor=paid;auto nextImage=paidImage;
        Check(CrashSuccessor(nextImage,successor),"crash fixture computes the complete expected successor independently");
        std::wstring command=L"\""+std::wstring(exe)+L"\" \""+fixtureWide+L"\" \""+root+
            L"\" checkpoint-child \""+path+L"\" "+std::to_wstring(point);
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        const bool started=CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
                                         nullptr,nullptr,&startup,&process)!=FALSE;
        Check(started,"checkpoint crash child starts with an explicit executable and no shell");
        if(!started)continue;
        CloseHandle(process.hThread);
        const auto wait=WaitForSingleObject(process.hProcess,20000);
        if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,239);WaitForSingleObject(process.hProcess,5000);}
        DWORD exitCode=0;const bool exited=GetExitCodeProcess(process.hProcess,&exitCode)!=FALSE;
        CloseHandle(process.hProcess);
        Check(wait==WAIT_OBJECT_0&&exited&&exitCode==238,"the child dies abruptly at the requested real I/O boundary");
        Store reopened;SaveImage selected{};workshop::State restored{};
        FfxHooks::NativeSaveEvents::CheckpointSelection selection{};
        const bool recovered=reopened.Initialize(root,false)&&
            reopened.SelectCheckpoint(path,disk,selected,selection)==StoreResult::Found&&
            reopened.ReadCheckpointLoaded(path,anchor,selection.proof,selected,restored)==StoreResult::Found;
        const auto& expectedImage=point==4?nextImage:paidImage;
        const auto& expectedState=point==4?successor:paid;
        Check(recovered&&selected==expectedImage&&std::memcmp(&restored,&expectedState,sizeof(restored))==0,
              "after process death recovery contains exactly the prior pair before rename or the paid successor after rename");
    }
}
