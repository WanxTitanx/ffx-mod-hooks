// Jarvis-HOOK: annotate the native gauge using the same current quote as commit.
// The original owns the gauge; this read-only suffix never changes charge/costs.
using GaugeFn=void(__cdecl*)(unsigned,int,int);
bool GaugeDrawProfile(std::uintptr_t base) noexcept {
    Byte expected[]={0x55,0x8B,0xEC,0x81,0xEC,0xAC,0,0,0,0xA1,0xD8,0x13,0xC6,0,0x33,0xC5};
    std::uint32_t cookie=0;std::memcpy(&cookie,expected+10,4);
    cookie+=static_cast<std::uint32_t>(base-0x400000u);std::memcpy(expected+10,&cookie,4);
    Byte actual[sizeof(expected)]{};
    return Copy(actual,reinterpret_cast<void*>(base+0x4F4B20),sizeof(actual))&&!std::memcmp(expected,actual,sizeof(actual));
}
const Byte* SelectedGaugeCommand(unsigned owner) noexcept {
    if(owner>=18||owner==7||!Read<Byte>(reinterpret_cast<void*>(module+0xD2A8E0))||
       Read<int>(reinterpret_cast<void*>(module+0x1FCC08C))!=1)return nullptr;
    const int index=Read<std::int8_t>(reinterpret_cast<void*>(module+0x1FCC092),-1);
    if(index<0||index>=8)return nullptr;
    const auto* window=reinterpret_cast<const Byte*>(module+0xF3C910+0xF0*index);
    const auto phase=Read<Byte>(window+1);
    if(phase<3||phase>6||Read<std::uint16_t>(window+8,255)!=owner)return nullptr;
    const unsigned selected=Read<std::uint16_t>(window+0x42,65535);
    const int count=Read<int>(window+0x24);
    const auto list=Read<std::uint32_t>(window+0x20);
    if(count<1||count>320||selected>=static_cast<unsigned>(count)||list<0x10000||list>UINT32_MAX-2*static_cast<unsigned>(count))return nullptr;
    const auto command=Read<std::uint16_t>(window+0x1E,255);
    if(command<0x3000||command>0x313F||Read<std::uint16_t>(reinterpret_cast<void*>(list+2*selected),255)!=command)return nullptr;
    CommandTableView table{};
    return ReadCommandTable(table)&&(command&0xFFFu)<=table.last?
        reinterpret_cast<const Byte*>(table.rows+96*(command&0xFFFu)):nullptr;
}
void __cdecl GaugeShim(unsigned owner,int x,int y){
    reinterpret_cast<GaugeFn>(originals[GaugeHook])(owner,x,y);
    if(!Enter()||!(On(Feature::PartialOverdriveCosts)||On(Feature::Efficiency)))return;
    const auto* command=SelectedGaugeCommand(owner);CostOwner::Quote quote{};
    if(!command||!CostOwner::Read(owner,command,quote)||!quote.cost||!quote.maximum||quote.charge>quote.maximum)return;
    const int width=Read<std::int16_t>(reinterpret_cast<void*>(module+0x21D0AA4));
    const int height=Read<std::int16_t>(reinterpret_cast<void*>(module+0x21D0AA2));
    if(width<=0||width>4096||height<=0||height>64)return;
    // These signed WORD offsets are the exact geometry used by the native body.
    const float left=static_cast<float>(static_cast<std::int16_t>(x)+Read<std::int16_t>(reinterpret_cast<void*>(module+0x21D0AA8))-1);
    const float top=static_cast<float>(static_cast<std::int16_t>(y)+Read<std::int16_t>(reinterpret_cast<void*>(module+0x21D0AA6)));
    const float unit=static_cast<float>(width)/static_cast<float>(quote.maximum);
    const unsigned available=(std::min)(quote.charge,quote.cost);
    const unsigned missing=(std::min)(quote.cost-available,quote.maximum-available);
    const unsigned begin=quote.charge-available;
    using Rectangle=int(__cdecl*)(float,float,float,float,unsigned,unsigned);
    const auto draw=reinterpret_cast<Rectangle>(module+0x4F4B20);
    __try {
        if(available)draw(left+unit*begin,top,unit*available,static_cast<float>(height),0x80FFFFFFu,0x80FFFFFFu);
        if(missing)draw(left+unit*quote.charge,top,unit*missing,static_cast<float>(height),0x800000FFu,0x800000FFu);
    }__except(EXCEPTION_EXECUTE_HANDLER){running=false;}
}
