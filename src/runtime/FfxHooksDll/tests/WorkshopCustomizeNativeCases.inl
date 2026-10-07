#include "../../../../research/equipment_workshop/include/customize_recipes.h"
#include "../../../../research/equipment_workshop/include/customize_constraints.h"
namespace CustomizeNativeTest {
struct Recipe {std::uint16_t kind,ability,item,quantity;};
static Recipe recipe{};
static const void* __cdecl Recipes(){return &recipe;}
static int __cdecl One(){return 1;}
static int __cdecl Rich(unsigned){return 255;}
static const void* __cdecl UnusedKernel(unsigned,int* count){if(count)*count=0;return nullptr;}
static void Run(){
    Redirect((::FfxHooks::ExecutableProfile::Rva<0x390250>()),reinterpret_cast<const void*>(&UnusedKernel));
    Redirect((::FfxHooks::ExecutableProfile::Rva<0x4A9810>()),reinterpret_cast<const void*>(&NoDevice));
    Redirect((::FfxHooks::ExecutableProfile::Rva<0x4C1B50>()),reinterpret_cast<const void*>(&Recipes));
    Redirect((::FfxHooks::ExecutableProfile::Rva<0x4C1B70>()),reinterpret_cast<const void*>(&One));
    Redirect((::FfxHooks::ExecutableProfile::Rva<0x390500>()),reinterpret_cast<const void*>(&Rich));
    const auto build=reinterpret_cast<int(__cdecl*)(int,const void*)>(imageBase+(::FfxHooks::ExecutableProfile::Rva<0x4C2370>()));
    workshop::Piece piece{};piece.native[2]=1;piece.native[6]=255;piece.native[11]=4;
    const std::uint16_t empty=255;for(unsigned i=0;i<4;++i)std::memcpy(piece.native+14+2*i,&empty,2);
    for(unsigned id=0;id<131;++id){const auto row=workshop::CustomizeRecipes[id];if(!row.quantity)continue;
        piece.native[5]=static_cast<unsigned char>(row.kind-1);
        recipe={row.kind,static_cast<std::uint16_t>(0x8000+id),static_cast<std::uint16_t>(0x2000+row.item),row.quantity};
        for(unsigned other=0;other<131;++other){const auto word=static_cast<std::uint16_t>(0x8000+other);std::memcpy(piece.native+14,&word,2);
            build(5,piece.native);
            const auto* entry=reinterpret_cast<const unsigned char*>(imageBase+(::FfxHooks::ExecutableProfile::Rva<0x1197730>()));
            std::uint16_t listed=0;std::memcpy(&listed,entry,2);
            Check(listed==recipe.ability&&(entry[2]==11||entry[2]==12||entry[2]==15),"actual native Customize builder classified the candidate");
            Check((workshop::CustomizeEligibility(piece,4,recipe.ability)==workshop::Error::Ok)==(entry[2]==11),
                  "Workshop restrictions match native Customize for every stock candidate and existing ability");
        }
    }
}
}
