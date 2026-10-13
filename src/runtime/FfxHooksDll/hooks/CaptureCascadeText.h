#pragma once
#include "CaptureCascadeCore.h"
#include <cstdio>

namespace FfxHooks::CaptureCascade {
struct Page {const char* title;const char* english;const char* portuguese;};
inline constexpr Page sinPages[]={
    {"S.I.N. System","From Macalania onward, some monsters bear S.I.N. curses. These curses change their abilities and the way they fight.",u8"A partir de Macalania, alguns monstros carregam maldições SIN. Elas mudam suas capacidades e a maneira como lutam."},
    {"Read the encounter","Watch the battle notices and use Scan to examine enemies. Adapt your equipment and strategy to the threats you face.",u8"Observe os avisos de batalha e use Scan para examinar os inimigos. Adapte seus equipamentos e sua estratégia às ameaças que encontrar."},
    {"A changing world","Curses vary with the region and its enemies. Defeating a Dark Aeon awakens its region and strengthens S.I.N. there.",u8"As maldições variam conforme a região e os inimigos. Derrotar um Dark Aeon desperta sua região e intensifica o Sistema SIN naquele local."}
};
inline constexpr Page cascadePages[]={
    {"Yoke of Spira","You defeated a Dark Aeon. Its region has awakened, and encounters there will change.",u8"Você derrotou um Dark Aeon. Sua região despertou, e os encontros desse lugar mudarão."},
    {"Regional awakening","Weak monsters gain a Calm Lands or Gagazet stat base before tier 7. Regions new to S.I.N. draw from all compatible curses; other regions keep their abilities.",u8"Monstros fracos recebem uma base de atributos de Calm Lands ou Gagazet antes do tier 7. Áreas novas no SIN usam todas as maldições compatíveis; as demais mantêm suas habilidades."},
    {"Dark Aeon patrols","Defeated Dark Aeons can appear in regular encounters in awakened regions that allow them. More than one can appear together. A warning signals their arrival.",u8"Dark Aeons já derrotados podem aparecer em encontros comuns nas regiões despertadas que os permitem. Mais de um pode surgir na mesma batalha. Um aviso sinaliza sua chegada."},
    {"A lasting consequence","The awakening lasts for this save. New Game+ resets it. Defeating the Dark Aeon is enough: Capture is not required.",u8"O despertar é permanente neste save. New Game+ restaura o estado das regiões. Basta derrotar o Dark Aeon: Capture não é necessário."}
};
inline bool Format(const View& view,bool portuguese,char* output,std::size_t capacity) noexcept {
    if(!output||!capacity||view.kind==Tutorial::None||view.page>=PageCount(view.kind))return false;
    const Page* page=nullptr;const char* title=nullptr;
    if(view.kind==Tutorial::Sin){page=&sinPages[view.page];title=portuguese?"Sistema SIN":page->title;}
    if(view.kind==Tutorial::Cascade){page=&cascadePages[view.page];title=portuguese?"Jugo de Spira":page->title;}
    int size=0;
    if(page){
        const char* navigation=portuguese?(view.page+1u==PageCount(view.kind)?"Confirmar: concluir   Esquerda: anterior   Cancelar: fechar":"Confirmar: continuar   Esquerda: anterior   Cancelar: fechar"):
            (view.page+1u==PageCount(view.kind)?"Confirm: finish   Left: previous   Cancel: close":"Confirm: next   Left: previous   Cancel: close");
        size=std::snprintf(output,capacity,"%s (%u/%u)\n\n%s\n\n%s",title,unsigned(view.page)+1,PageCount(view.kind),portuguese?page->portuguese:page->english,navigation);
    }else if(view.family<kFamilies){
        size=std::snprintf(output,capacity,portuguese?u8"Jugo de Spira\n\nA região de %s despertou. Prepare-se para maldições SIN mais fortes e patrulhas de Dark Aeons.\n\nConfirmar: continuar":
            "Yoke of Spira\n\nThe region of %s has awakened. Prepare for stronger S.I.N. curses and Dark Aeon patrols.\n\nConfirm: continue",Name(static_cast<Aeon>(view.family)));
    }
    return size>0&&static_cast<std::size_t>(size)<capacity;
}
}
