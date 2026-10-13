#pragma once
#include <cstddef>
#include <cstdint>
namespace FfxHooks::EquipmentWorkshop::MainMenu {
// All UI callbacks execute on the native menu owner. They reuse the native Workshop
// controller and transaction backend, never a second equipment writer.
struct Ui {
    bool (*ready)()=nullptr;
    bool (*open)()=nullptr;
    bool (*busy)()=nullptr;
    bool (*close)()=nullptr;
    void (*label)(bool help,unsigned char*,std::size_t)=nullptr;
    std::uint32_t (*bankBytes)(unsigned,std::uintptr_t)=nullptr;
};
bool Start(std::uintptr_t image,bool enabled,bool validateOnly,const Ui&,void(*log)(const char*));
void Pump() noexcept;
void RequestStop() noexcept;
bool StopReady() noexcept;
bool Active() noexcept;
bool AbilityName(unsigned word,unsigned char*,std::size_t,std::size_t&) noexcept;
bool DrawText(const unsigned char*,float x,float y,float width,bool compact) noexcept;
}
