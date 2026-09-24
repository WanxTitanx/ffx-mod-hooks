#pragma once
#include <cstdint>
namespace FfxHooks::FmvSpeed {
inline unsigned Budget(unsigned requested,unsigned queued,bool admitted) noexcept {
    if(!admitted || (requested!=2 && requested!=4 && requested!=8) || queued>1024)return 1;
    while(requested>1 && requested>queued)requested/=2;
    return requested;
}
struct Io {
    void* context=nullptr;
    bool(*next)(void*)=nullptr;
    bool(*setRate)(void*,unsigned)=nullptr;
    void(*restore)(void*)=nullptr;
    bool(*stillAdmitted)(void*)=nullptr;
};
struct Burst {bool frame=false;unsigned produced=0,rate=1;};
inline Burst Fetch(const Io& io,unsigned requested,unsigned queued,bool admitted){
    Burst result{};if(!io.next)return result;
    unsigned budget=Budget(requested,queued,admitted);
    while(budget>1 && (!io.setRate || !io.setRate(io.context,budget)))budget/=2;
    if(budget==1 && io.restore)io.restore(io.context);
    for(unsigned i=0;i<budget;++i){
        if(i && io.stillAdmitted && !io.stillAdmitted(io.context)){if(io.restore)io.restore(io.context);budget=1;break;}
        if(!io.next(io.context))break;
        ++result.produced;
    }
    result.frame=result.produced!=0;
    result.rate=result.produced?result.produced:1;
    if(result.produced<budget){
        if(result.produced>1 && io.setRate)io.setRate(io.context,result.produced);
        else if(io.restore)io.restore(io.context);
    }
    return result;
}
}
