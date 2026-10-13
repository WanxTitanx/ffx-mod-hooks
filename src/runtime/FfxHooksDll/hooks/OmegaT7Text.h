#pragma once
#include <cstdio>
#include <cstddef>
namespace FfxHooks::OmegaT7::Text {
inline constexpr unsigned Pages=4;
inline constexpr const char* English[Pages]={
    "The will imprisoned within these ruins still reaches every creature that breathes in their depths. Each bears a single Mark of the Seventh Seal.",
    "A mark grants a power and awakens before pain, magic or the fall of a companion. Watch its signs: wounding a creature may awaken what keeps it standing.",
    "Omega's currents never rest. When they shift, the marks change. A familiar enemy may hold a different power in your next encounter. Its mark remains fixed throughout a battle.",
    "Some guardians gather a still greater force. Recognize that omen before it is unleashed. You may revisit these words through the Omega rules in the S.I.N. menu."
};
inline constexpr const char* Portuguese[Pages]={
    u8"A vontade aprisionada nestas ruínas ainda alcança cada criatura que respira em suas profundezas. Cada uma carrega uma única Marca do Sétimo Selo.",
    u8"A marca concede um poder e desperta diante da dor, da magia ou da queda de seus semelhantes. Observe seus sinais: ferir uma criatura pode despertar aquilo que a mantém de pé.",
    u8"As correntes de Omega jamais repousam. Quando elas mudam, as marcas também mudam. Um inimigo conhecido pode guardar um poder diferente no próximo encontro. Sua marca permanece até o fim da batalha.",
    u8"Alguns guardiões acumulam uma força ainda maior. Reconheça esse presságio antes que ele seja libertado. Reveja estas palavras nas regras de Omega do menu SIN."
};
inline bool Format(unsigned kind,unsigned page,bool portuguese,char* out,std::size_t capacity) noexcept {
    if(!out||!capacity||kind>2||(kind==0&&page>=Pages))return false;
    const char* title=portuguese?u8"Ruínas de Omega - O Sétimo Selo":"Omega Ruins - The Seventh Seal";
    const char* footer=portuguese?u8"Confirmar: prosseguir   Cancelar: fechar":"Confirm: continue   Cancel: close";
    int n=0;
    if(kind==0)n=std::snprintf(out,capacity,"%s (%u/%u)\n\n%s\n\n%s",title,page+1,Pages,portuguese?Portuguese[page]:English[page],footer);
    else n=std::snprintf(out,capacity,"%s\n\n%s\n\n%s",title,kind==1?
        (portuguese?u8"As correntes de Omega se agitam. As marcas das criaturas foram renovadas.":"Omega's currents stir. The creatures' marks have been renewed."):
        (portuguese?u8"As Marcas do Sétimo Selo ainda vigiam estas ruínas. Observe os sinais de cada criatura.":"The Marks of the Seventh Seal still watch over these ruins. Observe each creature's signs."),footer);
    return n>0&&static_cast<std::size_t>(n)<capacity;
}
}
