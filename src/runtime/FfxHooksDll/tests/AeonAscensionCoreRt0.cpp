// Jarvis-HOOK: paid upgrades, canonical gates and persistent receipt identities.
#include <cstdio>
#include <cstring>
#include <array>
#if __has_include("../hooks/AeonAscensionCore.h")
#include "../hooks/AeonAscensionCore.h"
namespace A=FfxHooks::AeonAscension;namespace W=workshop;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
static void Put(unsigned char* p,unsigned v){p[0]=static_cast<unsigned char>(v);p[1]=static_cast<unsigned char>(v>>8);}
static W::State Inventory(unsigned owner,unsigned kind){
 std::array<unsigned char,4400> records{};std::array<std::uint16_t,112> items{};items.fill(255);
 auto* g=records.data();g[2]=1;g[3]=kind?0:4;g[4]=static_cast<unsigned char>(owner);g[5]=static_cast<unsigned char>(kind);g[6]=static_cast<unsigned char>(owner);g[11]=4;
 for(unsigned i=0;i<4;++i)Put(g+14+2*i,255);
 if(!kind)Put(g+14,0x807B);
 W::State s{};Check(W::Import(records.data(),items.data(),17,s)==W::Error::Ok,"private inventory imports");return s;
}
static W::Economy Economy(unsigned o,unsigned k){W::Economy e{};e.gil=100000000;e.customizeUnlocked=1;e.aeons.obtained=1u<<o;e.aeons.crests=127;e.aeons.gear[2*(o-8)+k]=0;return e;}
struct FixtureContext {A::Mapping mapping{};A::SaveId saveId{};};
static FixtureContext Context(){FixtureContext c{};c.mapping.enabled=true;c.mapping.proof=123;c.saveId[0]=27;c.mapping.replacements={0x8017,0x8018,0x8019};return c;}
static W::Error Preview(const W::State& state,const A::Ledger& receipts,const A::Request& request,const W::Economy& economy,const FixtureContext& context,A::Plan& plan){
 return A::Preview(state,receipts,context.saveId,context.mapping,economy,request,plan);
}
static bool Authorized(const A::Ledger& receipts,const W::Piece& piece,unsigned position,unsigned effect,const FixtureContext& c){return A::Authorized(receipts,c.saveId,piece,position,effect,c.mapping);}
static A::Request Request(const W::State& s,unsigned e,unsigned p){A::Request r{};r.slot=0;r.revision=s.revision;r.pieceId=s.pieces[0].id;r.effect=e;r.position=p;return r;}
int main(){
 const auto context=Context();A::Ledger empty{};
 for(unsigned owner=8;owner<18;++owner)for(unsigned kind=0;kind<2;++kind){
  const auto before=Inventory(owner,kind),original=before;const auto economy=Economy(owner,kind);auto request=Request(before,kind?0u:1u,kind?0:1);A::Plan p{},q{};
  Check(Preview(before,empty,request,economy,context,p)==W::Error::Ok,"all canonical acquired Aeons can purchase");
  auto revised=context;++revised.mapping.proof;A::Plan revisedPlan{};
  Check(Preview(before,empty,request,economy,revised,revisedPlan)==W::Error::Ok&&
        std::memcmp(&p,&revisedPlan,sizeof(p))!=0,"a changed mapping proof invalidates the previous confirmation even with identical prices and words");
  Check(!std::memcmp(&before,&original,sizeof(before))&&empty.count==0,"preview never mutates input or receipt");
  Check(p.inventory.gilCost==(kind?10000000u:15000000u)&&p.inventory.gilDebit==p.inventory.gilCost,"Gil includes Aeon multiplier exactly once");
  const auto& piece=p.inventory.after.pieces[0];Check(W::Ability(piece,request.position)==(kind?0x8094:0x8095)&&!W::AbilityRank(piece,request.position),"one selected slot receives fixed rank zero");
  Check(kind||W::Ability(piece,0)==0x807B,"Aeon Immunity preserved");
  Check(A::Validate(p.receipts,context.saveId,p.inventory.after)&&Authorized(p.receipts,piece,request.position,request.effect,context),"receipt binds paid inventory and save");
  Check(p.inventory.after.rng==before.rng&&p.inventory.after.rolls==before.rolls,"no random roll for deterministic purchase");
  for(unsigned item=0;item<112;++item){const unsigned cost=kind?(item==108?60:item==69?60:item==110?30:item==80?2:0):(item==53?99:item==111?30:item==109?20:item==80?3:0);
   Check(p.inventory.costs[item]==cost&&p.inventory.requirements[item]==cost&&p.inventory.after.items[item]==before.items[item]-cost,"exact materials without fifth multiplier");}
  request.revision=p.inventory.after.revision;Check(Preview(p.inventory.after,p.receipts,request,economy,context,q)==W::Error::Duplicate,"duplicate purchase is not charged");
  auto missing=economy;missing.aeons.obtained=0;request.revision=before.revision;Check(Preview(before,empty,request,missing,context,q)==W::Error::Locked,"acquisition required");
  if(W::AeonCrest(owner)){missing=economy;missing.aeons.crests=0;Check(Preview(before,empty,request,missing,context,q)==W::Error::Locked,"applied Crest required");}
  missing=economy;missing.aeons.gear[2*(owner-8)+kind]=1;Check(Preview(before,empty,request,missing,context,q)==W::Error::Protected,"canonical piece required");
  auto wrong=context.saveId;wrong[0]^=1;Check(!A::Validate(p.receipts,wrong,p.inventory.after),"receipt is save-specific");
  auto copied=piece;++copied.id;Check(!Authorized(p.receipts,copied,request.position,request.effect,context),"ID word alone cannot copy purchase");
  copied=piece;++copied.abilities[request.position];Check(!Authorized(p.receipts,copied,request.position,request.effect,context),"ability instance cannot inherit purchase");
  request.remove=true;request.revision=p.inventory.after.revision;Check(Preview(p.inventory.after,p.receipts,request,economy,context,q)==W::Error::Ok&&q.receipts.count==0&&W::Ability(q.inventory.after.pieces[0],request.position)==255,"dedicated removal retires receipt");
  Check(!q.inventory.gilDebit&&!std::memcmp(q.inventory.after.items,p.inventory.after.items,sizeof(before.items)),"no refund or new removal fee");
 }
 auto before=Inventory(8,0);auto economy=Economy(8,0);A::Plan p{};auto r=Request(before,1,1);
 economy.policy.devFreeMaterials=economy.policy.devFreeGil=economy.policy.devIgnoreProgression=1;
 Check(Preview(before,empty,r,economy,context,p)==W::Error::InvalidPolicy,"generic DEV waiver does not mint paid entitlement");
 economy=Economy(8,0);before.items[53]=98;Check(Preview(before,empty,r,economy,context,p)==W::Error::Materials,"missing material rejects purchase");before.items[53]=255;
 economy.gil=14999999;Check(Preview(before,empty,r,economy,context,p)==W::Error::Gil,"missing Gil rejects purchase");economy=Economy(8,0);
 r.position=0;Check(Preview(before,empty,r,economy,context,p)==W::Error::Protected,"Immunity cannot be replaced");
 r.position=1;r.replace=1;Put(before.pieces[0].native+16,0x8019);before.pieces[0].abilities[1]=before.nextId++;
 Check(Preview(before,empty,r,economy,context,p)==W::Error::Ok,"explicit vanilla Break replacement");r.replace=0;
 Check(Preview(before,empty,r,economy,context,p)==W::Error::Protected,"replacement needs explicit choice");r.position=4;before.pieces[0].fifthUnlocked=1;
 Check(Preview(before,empty,r,economy,context,p)==W::Error::InvalidRequest,"fifth needs filled native slots");
 for(unsigned i=2;i<4;++i){Put(before.pieces[0].native+14+2*i,0x8060+i);before.pieces[0].abilities[i]=before.nextId++;}
 Check(Preview(before,empty,r,economy,context,p)==W::Error::Ok&&p.inventory.costs[53]==99&&p.inventory.gilDebit==15000000,"fifth has identical exact recipe");
 Check(p.inventory.after.pieces[0].fifth==0x8095&&p.inventory.after.pieces[0].native[11]==4,"no sixth native slot");
 auto invalid=context;invalid.mapping.enabled=false;Check(Preview(before,empty,r,economy,invalid,p)==W::Error::UnsupportedAbility,"OFF has no receipt");
 invalid=context;invalid.mapping.proof=0;Check(Preview(before,empty,r,economy,invalid,p)==W::Error::UnsupportedAbility,"mapping proof required");
 invalid=context;invalid.mapping.words[1]=0x8087;Check(Preview(before,empty,r,economy,invalid,p)==W::Error::UnsupportedAbility,"Vanguard IDs remain reserved");
 before.pieces[0].native[4]=before.pieces[0].native[6]=7;Check(Preview(before,empty,r,economy,context,p)==W::Error::Protected,"Seymour is not an acquired Aeon");
 std::printf("AEON_ASCENSION_CORE_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production AeonAscensionCore.h is missing");return 1;}
#endif
