// Jarvis-HOOK: production navigation/preview/confirmation with a private core host.
static bool PaidUiMapping(FfxHooks::AeonAscension::Mapping& out) noexcept {
    out=TestHost::paidMapping;return FfxHooks::AeonAscension::ValidMapping(out);
}
static const FfxHooks::AeonAscension::Provider paidUiProvider{PaidUiMapping};
static void AeonAscensionMenuCases(){
#ifndef FFXHOOKS_ASCENSION_MENU_V1
    Check(false,"production dedicated Ascension menu is missing");
#else
    namespace E=EquipmentMenu;namespace P=FfxHooks::AeonAscension;using A=E::Action;
    Check(WorkshopTestOpen(),"paid menu fixture opens");E::StopReady();
    TestHost::receipts={};TestHost::saveId={};TestHost::saveId[0]=1;
    TestHost::paidMapping={};TestHost::paidMapping.enabled=true;TestHost::paidMapping.proof=123;
    TestHost::paidMapping.replacements={0x8017,0x8018,0x8019};
    Check(P::RegisterProvider(&paidUiProvider),"paid UI uses the same loaded-mapping provider contract");
    std::array<unsigned char,4400> records{};std::array<std::uint16_t,112> items{};items.fill(255);
    for(unsigned kind=0;kind<2;++kind){auto* gear=records.data()+kind*22;gear[2]=1;gear[3]=kind?0:4;gear[4]=gear[6]=8;gear[5]=static_cast<unsigned char>(kind);gear[11]=4;
        const std::uint16_t weapon[]={0x807B,0x8019,0x8062,0x8001},armor[]={0x8000,255,0x8017,0x8018};std::memcpy(gear+14,kind?armor:weapon,8);}
    Check(workshop::Import(records.data(),items.data(),771,TestHost::state)==workshop::Error::Ok,"canonical paid menu fixture imports");
    TestHost::aeons={};TestHost::aeons.obtained=1u<<8;TestHost::aeons.crests=2;TestHost::aeons.gear[0]=0;TestHost::aeons.gear[1]=1;
    TestHost::gil=50000000;TestHost::commits=0;TestHost::available=true;E::ownerFilter=-1;
    Check(E::Open(),"paid menu has a real bounded native object");WorkshopTestChoose(A::Piece,0);
    Check(WorkshopTestLists(A::Ascension,0),"canonical Aeon piece exposes dedicated upgrades");
    WorkshopTestChoose(A::Ascension);Check(WorkshopTestLists(A::AscensionApply,1)&&!WorkshopTestLists(A::AscensionRemove,1),"weapon exposes damage upgrade only");
    WorkshopTestChoose(A::AscensionApply,1);
    Check(WorkshopTestLists(A::Value,1)&&!WorkshopTestLists(A::Value,0)&&!WorkshopTestLists(A::Value,2)&&!WorkshopTestLists(A::Value,4),"slot picker offers explicit BDL replacement without immunity or occupied unrelated slots");
    WorkshopTestChoose(A::Value,1);
    Check(E::page==E::Page::Confirm&&E::reviewError==workshop::Error::Ok&&E::preview.gilCost==15000000&&E::preview.costs[53]==99,"confirmed quote has exact fixed recipe and final Gil");
    NativeMenu::rendered.clear();E::Draw(E::menu.obj);
    Check(WorkshopRendered("15000000")&&WorkshopRendered("Dark Matter")&&WorkshopRendered("99"),"actual draw exposes final price and materials");
    WorkshopTestChoose(A::Back);Check(TestHost::commits==0&&TestHost::gil==50000000&&!TestHost::receipts.count,"cancel spends nothing and emits no receipt");
    WorkshopTestChoose(A::Value,1);++TestHost::paidMapping.proof;WorkshopTestChoose(A::Confirm);
    Check(!TestHost::commits&&!TestHost::receipts.count,"changed mapping proof invalidates a displayed confirmation");
    WorkshopTestChoose(A::Ascension);WorkshopTestChoose(A::AscensionApply,1);WorkshopTestChoose(A::Value,1);WorkshopTestChoose(A::Confirm);
    Check(TestHost::commits==1&&TestHost::gil==35000000&&TestHost::receipts.count==1,"one explicit confirmation creates one paid entitlement");
    Check(workshop::Ability(TestHost::state.pieces[0],0)==0x807B&&workshop::Ability(TestHost::state.pieces[0],1)==0x8095,"replacement preserves permanent Aeon Immunity");
    WorkshopTestChoose(A::Clear);Check(!WorkshopTestLists(A::Value,1),"generic remove never advertises a paid cap");WorkshopTestChoose(A::Back);
    WorkshopTestChoose(A::Ascension);Check(!WorkshopTestLists(A::AscensionApply,1)&&WorkshopTestLists(A::AscensionRemove,1),"paid entry offers dedicated removal rather than duplicate purchase");
    WorkshopTestChoose(A::AscensionRemove,1);WorkshopTestChoose(A::Value,1);
    Check(E::page==E::Page::Confirm&&!E::preview.gilDebit,"removal requires its own confirmation without invented fee");
    WorkshopTestChoose(A::Confirm);Check(TestHost::commits==2&&TestHost::gil==35000000&&!TestHost::receipts.count&&workshop::Ability(TestHost::state.pieces[0],1)==255,"removal revokes its receipt without refund or restoring another ability");
    E::StopReady();
    auto& weapon=TestHost::state.pieces[0];const std::uint16_t bdl=0x8019;std::memcpy(weapon.native+16,&bdl,2);weapon.abilities[1]=TestHost::state.nextId++;weapon.fifthUnlocked=1;++TestHost::state.revision;
    Check(E::Open(),"fifth paid case opens");WorkshopTestChoose(A::Piece,0);WorkshopTestChoose(A::Ascension);WorkshopTestChoose(A::AscensionApply,1);
    Check(WorkshopTestLists(A::Value,4),"four filled native slots expose the existing unlocked logical fifth");WorkshopTestChoose(A::Value,4);
    Check(E::preview.costs[53]==99&&E::preview.gilCost==15000000,"fifth placement adds no1.5x materials or placement fee");WorkshopTestChoose(A::Confirm);
    Check(TestHost::state.pieces[0].fifth==0x8095&&TestHost::state.pieces[0].native[11]==4&&TestHost::receipts.count==1,"fifth upgrade has a receipt and creates no sixth native slot");
    E::StopReady();TestHost::paidMapping.enabled=false;Check(E::Open(),"disabled mapping still permits browsing");WorkshopTestChoose(A::Piece,1);WorkshopTestChoose(A::Ascension);
    Check(!WorkshopTestLists(A::AscensionApply,0),"unavailable runtime mapping cannot expose an actionable purchase");
    E::StopReady();P::UnregisterProvider(&paidUiProvider);TestHost::receipts={};TestHost::paidMapping={};
#endif
}
