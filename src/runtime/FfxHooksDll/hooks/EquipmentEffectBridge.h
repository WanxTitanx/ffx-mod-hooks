#pragma once
#include "../../../../research/equipment_workshop/include/workshop.h"
#include <atomic>
#include <cstdint>

namespace FfxHooks::EquipmentEffects::Pipeline {
// The existing Workshop owns Field/Aggregate/Gear/Row. Additional consumers
// supply private row views through this interface, never a competing detour.
struct Provider {
    bool (*fifth)(unsigned owner,const workshop::Piece&) noexcept;
    bool (*row)(unsigned owner,unsigned slot,unsigned position,const unsigned char* gear,
                unsigned word,const void* table,const unsigned char* original,
                unsigned char* privateRow) noexcept;
};
inline std::atomic<const Provider*> provider{nullptr};
struct Scope {unsigned owner=255;bool battle=false;Scope* previous=nullptr;unsigned depth=0;};
inline thread_local Scope* current=nullptr;
inline void Enter(Scope& scope,unsigned owner,bool battle) noexcept {
    scope={owner,battle,current,current?current->depth+1:1};current=&scope;
}
inline void Leave(Scope& scope) noexcept {if(current==&scope)current=scope.previous;}
inline bool Register(const Provider* value) noexcept {
    if(!value||!value->fifth||!value->row)return false;
    const Provider* expected=nullptr;
    return provider.compare_exchange_strong(expected,value)||expected==value;
}
inline void Unregister(const Provider* value) noexcept {
    const Provider* expected=value;if(value)(void)provider.compare_exchange_strong(expected,nullptr);
}
inline bool Required() noexcept {return provider.load()!=nullptr;}
inline bool Fifth(unsigned owner,const workshop::Piece& piece) noexcept {
    const auto* value=provider.load();return value&&value->fifth(owner,piece);
}
inline bool Row(unsigned owner,unsigned slot,unsigned position,const unsigned char* gear,unsigned word,
                const void* table,const unsigned char* original,unsigned char* output) noexcept {
    const auto* value=provider.load();
    return current&&current->depth<=16&&value&&value->row(owner,slot,position,gear,word,table,original,output);
}
} // namespace FfxHooks::EquipmentEffects::Pipeline
