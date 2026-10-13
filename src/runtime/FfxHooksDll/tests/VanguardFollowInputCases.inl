// Jarvis-HOOK: native input dispatch must not launch the next ordinary menu
// while an injected assist still owns the action phase. Rendering alone is a sink.
static unsigned followMenuOpens=0;
static void __cdecl FollowMenuOpened(unsigned owner){
    ++followMenuOpens;
    W32(reinterpret_cast<unsigned char*>(base+FfxHooks::ExecutableProfile::Rva<0x1FCC08C>()),1u<<owner);
}
static __declspec(naked) void FollowMenuEndpoint(){
    __asm {mov esp,ebp
        pop ebp
        jmp FollowMenuOpened}
}
static int __cdecl FollowInputNoPresentation(){return 0;}
template<class Pending>
static void FollowInputCases(Pending pending,unsigned char* queue,unsigned char* actionCount){
    namespace P=FfxHooks::ExecutableProfile;
    constexpr auto inputRva=P::Steam20261001?0x392A90u:0x392AB0u;
    constexpr auto appendRva=P::Steam20261001?0x3B2430u:0x3B2440u;
    const auto dispatch=reinterpret_cast<void(__cdecl*)()>(base+inputRva);
    const auto append=reinterpret_cast<int(__cdecl*)(unsigned,unsigned,unsigned,unsigned)>(base+appendRva);
    const auto finish=reinterpret_cast<int(__cdecl*)(unsigned,unsigned,unsigned)>(base+P::Rva<0x3B0870>());
    auto* input=reinterpret_cast<unsigned char*>(base+P::Rva<0xD2AA80>());
    auto* inputCount=reinterpret_cast<unsigned char*>(base+P::Rva<0xD2BDE0>());
    auto* menuMask=reinterpret_cast<unsigned char*>(base+P::Rva<0x1FCC08C>());
    // Preserve the real mode/busy rejection branches. Only the graphics-heavy
    // successful menu initialization tail is replaced by an admission witness.
    Check(PatchJump(base+P::Rva<0x49BA80>()+0x1E,reinterpret_cast<void*>(&FollowMenuEndpoint)),"successful native menu rendering is isolated after both original admission branches");
    constexpr auto eventRva=P::Steam20261001?0x397BC0u:0x397BD0u;
    Check(PatchJump(base+eventRva,reinterpret_cast<void*>(&FollowInputNoPresentation))&&
          PatchJump(base+P::Rva<0x3ACEC0>(),reinterpret_cast<void*>(&FollowInputNoPresentation)),"input presentation events are isolated from the native queue dispatcher");
    const auto enemyRoot=reinterpret_cast<std::uintptr_t>(Actor(20));
    std::memcpy(reinterpret_cast<void*>(base+P::Rva<0xD34460>()),&enemyRoot,4);
    auto* roster=reinterpret_cast<unsigned char*>(base+P::Rva<0xD2C895>());std::memset(roster,255,7);roster[0]=6;
    Actor(6)[0xDD2]=Actor(6)[0xDD3]=Actor(20)[0xDD2]=1;
    auto prepare=[&](bool busy){
        pending();std::memset(input,0,62*8);*inputCount=0;
        for(unsigned id=0;id<31;++id){Actor(id)[0xDE4]=255;Actor(id)[0xDE6]=0;}
        // Regeneration/turn-start preparation has already run in this fixture.
        Actor(6)[0x716]=Actor(6)[0xDF7]=1;Actor(6)[0xDF3]=0;
        W32(menuMask,busy?1:0);followMenuOpens=0;
        Check(append(6,0,1,0)==-1,"native input append reserves Rikku's ordinary turn");
    };
    prepare(false);const auto wait=Actor(6)[0x65C];const auto mp=R32(Actor(6)+0x5D4);
    dispatch();
    Check(followMenuOpens==0&&input[3]==0&&*inputCount==1&&*actionCount==1,
          "a pending follow-up defers ordinary menu admission without marking that turn as started");
    Actor(1)[0xDE5]=0;queue[4]=1;
    constexpr auto headRva=P::Steam20261001?0x3B0A10u:0x3B0A20u;
    reinterpret_cast<unsigned char*(__cdecl*)()>(base+headRva)();
    dispatch();
    Check(followMenuOpens==0&&input[3]==0,"a running assist also keeps the next ordinary input unopened");
    Hit(0,18,1,0,true,0,1);queue[2]=1;finish(1,0,0);
    dispatch();
    Check(followMenuOpens==1&&input[3]==1&&Actor(6)[0xDE4]==0&&*inputCount==1,
          "retiring the assist opens the original Rikku turn through native input dispatch exactly once");
    Check(Actor(6)[0x65C]==wait&&R32(Actor(6)+0x5D4)==mp,"deferring input spends no CTB or MP");
    prepare(true);dispatch();
    Check(input[3]==0&&followMenuOpens==0,"an earlier busy native menu cannot strand a new input request during follow-up");
    // Native menu shutdown can complete after the rejected launch. This is the
    // observed live failure: input marked started, menu mask empty, no action.
    W32(menuMask,0);W32(Actor(18)+0x5D0,0);
    reinterpret_cast<unsigned char*(__cdecl*)()>(base+headRva)();dispatch();
    Check(followMenuOpens==1&&input[3]==1&&*actionCount==0,
          "canceling the last dead-target assist leaves a usable normal input instead of the live orphan state");
    prepare(false);B::RunInitScene(B::kBattleStateInitSceneReturnRva,{nullptr,Scene},{},{});dispatch();
    Check(followMenuOpens==1&&input[3]==1,"an identical foreign action in a new epoch cannot hold normal input with old ownership");
    prepare(false);queue[0]=2;dispatch();
    Check(followMenuOpens==1&&input[3]==1,"foreign reactions cannot be classified as an owned input barrier");
    prepare(false);input[3]=1;const auto beforeInput=std::array<unsigned char,8>{input[0],input[1],input[2],input[3],input[4],input[5],input[6],input[7]};
    dispatch();Check(!std::memcmp(input,beforeInput.data(),8)&&followMenuOpens==0,
          "an already started ordinary input stays native-owned instead of being reset or relaunched");
    *inputCount=0;Actor(6)[0xDE4]=255;Actor(6)[0xDE6]=0;W32(menuMask,0);
}
