#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../hooks/EquipmentWorkshopAcquisition.h"
#include "../hooks/CaptureCascadeBattleCore.h"
#include "../hooks/CaptureCascadeCatalog.generated.h"
#include "PrivatePeFixture.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iterator>
namespace FfxHooks::CaptureCascade::Battle {
using namespace FfxHooks::CaptureCascade;
#include "CascadeAsset.inc"
}
static unsigned checks=0,failures=0;
static void Check(bool v,const char* why){++checks;if(!v){++failures;std::printf("FAIL %s\n",why);}}
int main(int argc,char** argv){if(argc!=4)return 2;const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!image)return 2;
    Check(PrivatePeFixture::NormalizeRelocations(image),"actual PE fixture relocates");const auto base=reinterpret_cast<std::uintptr_t>(image);
    std::ifstream in(argv[2],std::ios::binary);std::vector<unsigned char> table((std::istreambuf_iterator<char>(in)),{});
    const auto root=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(table.data()));std::memcpy(reinterpret_cast<void*>(base+FfxHooks::EquipmentWorkshop::Acquisition::PointerRva),&root,4);
    // Call the real current/legacy native getter: its result must identify the
    // same table and row count as the Workshop read-only admission snapshot.
    using Getter=void*(__cdecl*)(unsigned*);unsigned rows=0;const auto getter=base+(FfxHooks::ExecutableProfile::Steam20261001?0x390A10:0x390A60);
    const unsigned expectedRows=table.size()>=20?unsigned(table[10])+(unsigned(table[11])<<8)+1u:0u;
    Check(reinterpret_cast<Getter>(getter)(&rows)==table.data()+20&&rows==expectedRows&&rows>=125,"native Customize getter resolves the complete current Spira table including appended Aero recipes");
    workshop::AcquisitionCatalog snapshot{};Check(FfxHooks::EquipmentWorkshop::Acquisition::Read(base,snapshot),"production reader admits the installed Spira table");
    for(unsigned word:{0x800Du,0x8011u,0x8018u})Check(!workshop::AcquisitionAllowed(static_cast<std::uint16_t>(word),&snapshot),"retired native target is not authorable");
    for(unsigned word:{0x800Cu,0x8017u,0x8019u,0x807Au})Check(workshop::AcquisitionAllowed(static_cast<std::uint16_t>(word),&snapshot),"Half MP, Break Limits, BDL and Capture are retained");
    const auto original=snapshot.proof;table[14]^=1;Check(!FfxHooks::EquipmentWorkshop::Acquisition::Read(base,snapshot),"malformed live table closes new acquisition");table[14]^=1;
    Check(FfxHooks::EquipmentWorkshop::Acquisition::Read(base,snapshot)&&snapshot.proof==original,"unchanged repaired bytes recover the exact original proof");
    namespace C=FfxHooks::CaptureCascade;const auto path=std::filesystem::path(argv[3]);
    for(auto monster:C::Catalog::monsters){wchar_t name[64]{};swprintf_s(name,L"_m%03u/m%03u.bin",monster.destination,monster.destination);
        Check(C::Battle::Asset(path/name,monster),"actual runtime file gate accepts the current verified clone bytes");
        monster.hash^=1;Check(!C::Battle::Asset(path/name,monster),"changed content remains rejected without weakening the witness");}
    std::printf("SPIRA_ACQUISITION_NATIVE %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
