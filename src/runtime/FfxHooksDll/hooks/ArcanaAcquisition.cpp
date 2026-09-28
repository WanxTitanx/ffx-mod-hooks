#include "ArcanaAcquisition.h"

namespace FfxHooks::Arcana::Acquisition {
namespace {
enum Gate {Start,Valefor,Ifrit,Ixion,Shiva,Bahamut,Mirror,Yojimbo,Anima,Magus,Primers,DarkBahamut,DarkAnima,Lightning,DarkEight,Collection};
constexpr const char* hints[]={
    "Begin your pilgrimage.","Obtain Valefor in Besaid.","Obtain Ifrit in Kilika.","Obtain Ixion in Djose.",
    "Obtain Shiva in Macalania.","Obtain Bahamut in Bevelle.","Obtain the Celestial Mirror.",
    "Obtain Yojimbo.","Obtain Anima.","Obtain all three Magus Sisters.","Collect all 26 Al Bhed primers.",
    "Defeat Dark Bahamut.","Defeat Dark Anima.","Dodge 200 consecutive lightning bolts.",
    "Defeat all eight Dark Aeon encounters.","Collect the other 77 Tarot cards."};
// Stable card IDs are the per-save award receipts. Progress is read only;
// no story flag, reward item or native equipment record is written.
constexpr Gate gates[kCardCount]={
    Start,Bahamut,Shiva,Bahamut,Mirror,Yojimbo,Bahamut,Anima,Magus,Primers,Mirror,Yojimbo,Anima,Magus,Primers,
    DarkAnima,DarkBahamut,Mirror,Yojimbo,Lightning,DarkEight,Collection,
    Valefor,Ifrit,Ifrit,Ifrit,Ifrit,Ifrit,Ifrit,Bahamut,Bahamut,Bahamut,Ixion,Bahamut,Bahamut,Anima,
    Valefor,Ixion,Ixion,Ixion,Ixion,Ixion,Bahamut,Bahamut,Bahamut,Bahamut,Shiva,Bahamut,Magus,Primers,
    Valefor,Shiva,Bahamut,Bahamut,Bahamut,Bahamut,Bahamut,Mirror,Yojimbo,Anima,Shiva,Magus,Primers,Mirror,
    Valefor,Shiva,Shiva,Bahamut,Bahamut,Bahamut,Bahamut,Bahamut,Yojimbo,Bahamut,Shiva,Anima,Magus,Primers};
bool Obtained(const unsigned char* p,unsigned actor){return (p[0x55CC+actor*0x94+0x2C]&0x10)!=0;}
unsigned Word(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
bool Dark(const unsigned char* p,unsigned index){return (p[0xC89+index]&0x80)!=0||p[0x18F4+index]!=0;}
bool Satisfied(Gate gate,const unsigned char* p){
    switch(gate){
    case Start:return true;
    case Valefor:return Obtained(p,8);case Ifrit:return Obtained(p,9);case Ixion:return Obtained(p,10);
    case Shiva:return Obtained(p,11);case Bahamut:return Obtained(p,12);
    case Mirror:return p[0xC38]==1;
    case Yojimbo:return Obtained(p,14);case Anima:return Obtained(p,13);
    case Magus:return Obtained(p,15)&&Obtained(p,16)&&Obtained(p,17);
    case Primers:return p[0x3D10]==255&&p[0x3D11]==255&&p[0x3D12]==255&&(p[0x3D13]&3)==3;
    case DarkBahamut:return Dark(p,4);case DarkAnima:return Dark(p,6);
    case Lightning:return Word(p+0x400)>=200;
    case DarkEight:for(unsigned i=0;i<8;++i)if(!Dark(p,i))return false;return true;
    default:return false;
    }
}
}
const char* Requirement(unsigned card) noexcept {return card<kCardCount?hints[gates[card]]:"Unavailable";}
unsigned Reconcile(State& state,const unsigned char* payload,std::size_t size) noexcept {
    if(!payload||size<kPayloadBytes||Validate(state)!=Error::None)return 0;
    auto next=state;unsigned count=0;
    for(unsigned card=0;card<kCardCount;++card)if(!next.acquired[card]&&Satisfied(gates[card],payload)){
        if(Award(next,card)!=Error::None)return 0;
        ++count;
    }
    bool complete=true;for(unsigned card=0;card<kCardCount;++card)if(card!=21&&!next.acquired[card])complete=false;
    if(complete&&!next.acquired[21]){if(Award(next,21)!=Error::None)return 0;++count;}
    state=next;return count;
}
}
