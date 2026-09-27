#include "../hooks/EquipmentWorkshopPresentationCore.h"
#include <cstdio>
#include <cstring>
namespace P=FfxHooks::EquipmentWorkshop::Presentation;
static unsigned checks=0,failures=0;
static void Check(bool b,const char* s){++checks;if(!b){++failures;std::printf("FAIL %s\n",s);}}
static workshop::Piece Piece(){
    workshop::Piece p{};p.id=1;p.native[2]=1;p.native[6]=255;p.native[11]=4;p.fifth=255;p.mode=2;
    for(unsigned i=0;i<4;++i){p.native[14+2*i]=0x62;p.native[15+2*i]=0x80;p.abilities[i]=i+2;}return p;
}
int main(){
    auto p=Piece();P::GearView view{};
    Check(!P::BuildGearView(p,view),"unmodified gear retains the original native presentation");
    p.ranks[0]=3;const auto native=p;
    Check(P::BuildGearView(p,view)&&view.bytes[11]==4&&view.bytes[22]==255,"rank-only view does not invent a fifth slot");
    Check(std::memcmp(&p,&native,sizeof(p))==0,"presentation construction never writes native or persisted state");
    p.fifthUnlocked=1;p.fifth=0x8063;p.abilities[4]=9;p.ranks[4]=10;
    Check(P::BuildGearView(p,view)&&view.bytes[11]==5&&view.bytes[22]==0x63&&view.bytes[23]==0x80&&p.native[11]==4,"temporary 24-byte view contains exactly five words without changing saved capacity");
    p.fifth=255;p.abilities[4]=0;p.ranks[4]=0;
    Check(P::BuildGearView(p,view)&&view.bytes[22]==255,"empty unlocked fifth still provides a native empty row");
    p.native[14]=p.native[15]=0;p.abilities[0]=0;p.ranks[0]=0;
    Check(P::BuildGearView(p,view)&&view.bytes[14]==255&&p.native[14]==0,"native zero-empty words normalize only in the draw copy");
    p.id=0;Check(!P::BuildGearView(p,view),"vacant identity is never decorated");
    p=Piece();p.native[11]=5;Check(!P::BuildGearView(p,view),"invalid native capacity cannot request a neighboring word");
    p=Piece();unsigned cursor=0;
    Check(P::NextSlot(p,0x8062,cursor)==0&&P::NextSlot(p,0x8062,cursor)==1,"duplicate abilities keep their separate slot identities");
    const auto before=cursor;Check(P::NextSlot(p,0x8063,cursor)==5&&cursor==before,"a mismatched native sequence never borrows another rank");
    p.native[18]=255;p.native[19]=0;p.abilities[2]=0;
    Check(P::NextSlot(p,0x8062,cursor)==3&&P::NextSlot(p,0x8062,cursor)==5,"sequence skips empty words and stops at native capacity");
    const unsigned char name[]={0x50,0x51,0x52};unsigned char text[12];std::memset(text,0xA5,sizeof(text));
    Check(P::AppendRank(name,3,10,text,sizeof(text))&&text[3]==0x3A&&text[4]==0x45&&text[5]==0x31&&text[6]==0x30&&text[7]==0,"rank suffix uses the verified native font encoding");
    Check(text[8]==0xA5,"native rank suffix stays inside its declared extent");
    unsigned char guard[7];std::memset(guard,0xA5,sizeof(guard));
    Check(!P::AppendRank(name,3,10,guard,sizeof(guard))&&guard[0]==0xA5&&guard[6]==0xA5,"short text buffers fail before writing");
    Check(!P::AppendRank(name,3,0,text,sizeof(text))&&!P::AppendRank(name,3,11,text,sizeof(text)),"zero or invalid ranks do not fabricate a label");
    std::printf("NATIVE_PRESENTATION_CORE %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
