// Jarvis-HOOK: original post-roll reward accumulator and its UI-visible buffer.
// Inventory additions, random selection, currency and AP are separate owners.
static void SpiraRewardCases(){
    using Accumulate=int(__cdecl*)(unsigned,int,void*);
    const auto accumulate=reinterpret_cast<Accumulate>(base+0x398AD0);
    auto* rewards=reinterpret_cast<unsigned char*>(base+0x1F10EA0);
    const auto reset=[&](){std::memset(rewards,0,0x1B0);};
    for(unsigned owner=0;owner<18;++owner){Equip(owner,1,{});Actor(owner)[0xDC8]=0;}
    Actor(0)[0xDC8]=Actor(1)[0xDC8]=1;
    reset();accumulate(0x2001,2,rewards);
    Check(rewards[0xD0]==1&&S::Word(rewards+0xD4)==0x2001&&rewards[0xE4]==2,"no drop ability preserves native selected quantity");
    Equip(0,1,{0x809B});reset();accumulate(0x2001,2,rewards);
    Check(rewards[0xE4]==4,"Double Drop modifies the original post-roll reward quantity");
    Equip(1,1,{0x809C});reset();accumulate(0x2001,2,rewards);
    Check(rewards[0xE4]==6,"Double plus Triple uses party maximum3 rather than product6");
    accumulate(0x2001,2,rewards);
    Check(rewards[0xD0]==1&&rewards[0xE4]==12,"another award multiplies its own input once instead of re-multiplying the accumulated total");
    Actor(1)[0xDC8]=0;reset();accumulate(0x2001,2,rewards);
    Check(rewards[0xE4]==4,"an inactive Triple carrier does not change the active party decision");
    Actor(1)[0xDC8]=1;Equip(0,1,{0x809C});reset();accumulate(0x2001,2,rewards);
    Check(rewards[0xE4]==6,"two Triple carriers still select one maximum3");
    std::array<unsigned char,0x1B0> other{};accumulate(0x2001,2,other.data());
    Check(other[0xE4]==2,"an unrelated accumulator cannot borrow the battle reward context");
    reset();accumulate(0x5001,2,rewards);
    Check(rewards[0xE4]==2,"gear and non-item namespaces are not multiplied");
    reset();accumulate(0x2001,0,rewards);accumulate(0x2001,-1,rewards);
    Check(rewards[0xD0]==0,"zero and negative quantities remain non-awards");
    reset();accumulate(0x2001,40,rewards);
    Check(rewards[0xE4]==99,"the native reward-byte maximum remains authoritative");
    reset();std::array<unsigned char,0xD0> untouched{};untouched.fill(0x35);std::memcpy(rewards,untouched.data(),untouched.size());
    accumulate(0x2001,2,rewards);
    Check(!std::memcmp(rewards,untouched.data(),untouched.size()),"party outcome, AP and Gil fields are untouched by item multiplication");
    reset();for(unsigned i=0;i<8;++i)accumulate(0x2000+i,1,rewards);
    const auto end=rewards[0xE4+7];accumulate(0x2008,1,rewards);
    Check(rewards[0xD0]==8&&rewards[0xE4+7]==end,"a full native reward list never writes a ninth slot");
    Equip(0,1,{});Equip(1,1,{});Actor(0)[0xDC8]=Actor(1)[0xDC8]=1;reset();
}
