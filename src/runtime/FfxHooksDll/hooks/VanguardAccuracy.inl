#include "../shared/ExecutableProfile.h"
// Jarvis-HOOK: exact native accuracy rules with wide Elude evasion. Never
// temporarily modify actor stats or reroll an already sampled native hit.
using AccuracyFn=int(__cdecl*)(const Byte*,const Byte*,const Byte*,const Byte*,int);
int __cdecl AccuracyShim(const Byte* source,const Byte* target,const Byte* command,const Byte* info,int forced){
    const auto original=reinterpret_cast<AccuracyFn>(originals[AccuracyHook]);
    if(!Enter()||!(On(Feature::Elude)||On(Feature::GuaranteedHits))||ActorId(source)>=31||ActorId(target)>=31||!command||!info)
        return original(source,target,command,info,forced);
    bool evade=false;
    if(On(Feature::Elude)&&(Read<std::uint16_t>(target+0x616)&0x800)){
        Effects attacker{},defender{};ReadEffects(source,target,attacker,defender);evade=defender[10];
    }
    if(!running.load()||(!evade&&!On(Feature::GuaranteedHits)))return original(source,target,command,info,forced);
    Byte row[0x2A]{},result[0x15]{};
    if(!Copy(row,command,sizeof(row))||!Copy(result,info,sizeof(result)))return original(source,target,command,info,forced);
    const unsigned misc=Read<std::uint32_t>(row+0x1C),mode=(misc>>3)&7;
    if((misc&0x800000)&&!(Read<std::uint16_t>(target+0x606)&3))return 2;
    if(!mode||result[7]||(result[20]&4))return 0;
    if(Read<Byte>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2A91F>()))))return 1;
    int accuracy=mode<=2?row[0x29]:Read<Byte>(source+0x5AF);
    if(mode==5)accuracy=accuracy*5/2;
    else if(mode==6)accuracy=accuracy*3/2;
    else if(mode==7)accuracy/=2;
    const int evasion=int(Read<Byte>(target+0x5AE))+(evade?50:0);
    int chance=accuracy-evasion;
    if(mode!=2&&mode!=4){
        const int index=(std::max)(0,(std::min)(8,accuracy*2/5-evasion+10));
        chance=Read<std::int8_t>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0x8421E0>())+index));
    }
    if((misc&0x40)&&Read<Byte>(source+0x60A))chance/=10;
    chance+=10*(int(Read<Byte>(source+0x65F))-int(Read<Byte>(target+0x661)))+
        int(Read<Byte>(source+0x662))+int(Read<Byte>(target+0x663))+
        int(Read<Byte>(source+0x5AD))-int(Read<Byte>(target+0x5AD));
    const int roll=static_cast<int>(Sample(source,1)%101);
    // Native samples the inclusive range0..100 but rejects roll==chance. Only
    // an effective probability already reaching100 gets its missing endpoint;
    // lower odds, Evasion/Blind penalties and explicit forced misses stay native.
    const bool miss=roll>=chance&&!(On(Feature::GuaranteedHits)&&chance>=100);
    return (miss||forced==2)&&!Read<Byte>(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0xD2A90A>())));
}
