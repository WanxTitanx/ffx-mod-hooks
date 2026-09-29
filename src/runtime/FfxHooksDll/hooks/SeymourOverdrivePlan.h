#pragma once
#include "SeymourOverdriveEvidence.generated.h"
#include <array>
#include <cstring>
#include <limits>
namespace FfxHooks::SeymourOverdrive {
struct Targets {std::uint32_t counter=0,gauge=0;};
inline bool ImageBaseValid(std::uint32_t base) noexcept {return base&&base<=UINT32_MAX-0x237d000u;}
inline bool Reference(unsigned function,std::uint32_t base,std::uint8_t* out,std::size_t capacity) noexcept {
    if(function>=FunctionCount||!ImageBaseValid(base)||!out)return false;
    const auto& spec=Functions[function];
    if(capacity<spec.size)return false;
    std::array<std::uint8_t,1024> prepared{};
    if(spec.size>prepared.size())return false;
    std::memcpy(prepared.data(),spec.bytes,spec.size);
    for(unsigned i=0;i<spec.absoluteCount;++i){
        const auto at=spec.absolutes[i];if(at+4u>spec.size)return false;
        std::uint32_t value=0;std::memcpy(&value,prepared.data()+at,4);
        value+=base-0x400000u;std::memcpy(prepared.data()+at,&value,4);
    }
    std::memcpy(out,prepared.data(),spec.size);return true;
}
// Copy verified native instructions; change only a local party bound. Every
// external call is enumerated and additional Seymour effects use scoped proxies.
inline bool Relocate(unsigned function,std::uint32_t base,std::uint32_t destination,
                     Targets routes,const std::uint8_t* source,std::size_t size,
                     std::uint8_t* out,std::size_t capacity) noexcept {
    if(function>=FunctionCount||!source||!out||!destination||!routes.counter||!routes.gauge)return false;
    const auto& spec=Functions[function];std::array<std::uint8_t,1024> copy{};
    if(size!=spec.size||capacity<spec.size||destination>UINT32_MAX-spec.size||
       !Reference(function,base,copy.data(),copy.size())||std::memcmp(source,copy.data(),size)!=0)return false;
    if(spec.boundOffset>=spec.size||copy[spec.boundOffset]!=spec.oldBound)return false;
    copy[spec.boundOffset]=spec.extendedBound;
    for(unsigned i=0;i<spec.callCount;++i){
        const auto& call=spec.calls[i];
        if(call.offset+5u>spec.size||copy[call.offset]!=0xe8||call.targetRva>=0x237d000u)return false;
        std::uint32_t old=0;std::memcpy(&old,copy.data()+call.offset+1,4);
        if(spec.rva+call.offset+5+old!=call.targetRva)return false;
        const auto target=call.targetRva==CounterRva?routes.counter:
                          call.targetRva==GaugeRva?routes.gauge:base+call.targetRva;
        const auto displacement=target-(destination+call.offset+5u);
        std::memcpy(copy.data()+call.offset+1,&displacement,4);
    }
    std::memcpy(out,copy.data(),size);return true;
}
} // namespace FfxHooks::SeymourOverdrive
