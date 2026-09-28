static void AeonWorkshopMenuCases(){
    using A=EquipmentMenu::Action;
    Check(WorkshopTestOpen(),"Aeon menu fixture starts");EquipmentMenu::StopReady();
    std::array<unsigned char,4400> records{};std::array<std::uint16_t,112> items{};items.fill(99);
    for(unsigned i=0;i<200;++i)std::memcpy(records.data()+22*i,TestHost::state.pieces[i].native,22);
    for(unsigned kind=0;kind<2;++kind){auto* gear=records.data()+22*(190+kind);std::memset(gear,0,22);
        gear[2]=1;gear[3]=static_cast<unsigned char>(kind?3:7);gear[4]=8;gear[5]=static_cast<unsigned char>(kind);gear[6]=8;gear[11]=4;
        const std::uint16_t weapon[]={0x807B,0x8019,0x8062,0x8001},armor[]={0x8000,255,0x8017,0x8018};std::memcpy(gear+14,kind?armor:weapon,8);}
    Check(workshop::Import(records.data(),items.data(),77,TestHost::state)==workshop::Error::Ok,"Aeon menu imports real record shapes");
    TestHost::aeons={};TestHost::aeons.obtained=1u<<8;TestHost::aeons.gear[0]=190;TestHost::aeons.gear[1]=191;
    TestHost::gil=10000000;TestHost::available=true;EquipmentMenu::ownerFilter=-1;
    Check(EquipmentMenu::Open(),"Aeon inventory opens");
    const bool listed=WorkshopTestLists(A::Piece,190)&&WorkshopTestLists(A::Piece,191);
    Check(listed,"obtained Aeon weapons and armor appear in Workshop");
    if(listed){
        WorkshopTestChoose(A::Piece,190);
        Check(!WorkshopTestLists(A::Refine,0)&&!WorkshopTestLists(A::Unlock,0),"missing Crest denies menu edits");
        Check(std::strstr(EquipmentMenu::notice,"Nirvana")!=nullptr,"locked Aeon names the required weapon");
        WorkshopTestChoose(A::Back);TestHost::aeons.crests=2;WorkshopTestChoose(A::Piece,190);
        Check(WorkshopTestLists(A::Refine,0)&&!WorkshopTestLists(A::Reforge,0),"unlocked equipped Aeon can refine but cannot change owner");
        WorkshopTestChoose(A::Clear);
        Check(!WorkshopTestLists(A::Value,0)&&WorkshopTestLists(A::Value,2),"immunity cannot be selected for removal");
        WorkshopTestChoose(A::Back);WorkshopTestChoose(A::Unlock);
        Check(EquipmentMenu::reviewError==workshop::Error::Ok,"native fifth quote is admitted");
        for(unsigned i=75;i<=79;++i)Check(EquipmentMenu::preview.costs[i]==4,"confirmation lists four of each sphere");
        WorkshopTestChoose(A::Back);WorkshopTestChoose(A::Refine);
        Check(EquipmentMenu::reviewError==workshop::Error::Ok&&EquipmentMenu::preview.gilCost==2000,"Aeon preview displays doubled random-refinement Gil");
        NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
        Check(WorkshopRendered("Aeon Immunity [Permanent]"),"Aeon confirmation identifies its protected native ability");
        Check(!WorkshopRendered("mod-only recipe"),"permanent immunity is not advertised as a paid refinement ingredient");
        WorkshopTestChoose(A::Back);WorkshopTestChoose(A::Fuse);
        Check(!WorkshopTestLists(A::Value,191),"Aeon armor cannot become a Fusion donor");
    }
    EquipmentMenu::StopReady();TestHost::aeons={};Check(EquipmentMenu::Open(),"menu rebuilds with current acquisition");
    Check(!WorkshopTestLists(A::Piece,190),"unobtained Aeons stay hidden");EquipmentMenu::StopReady();
}
