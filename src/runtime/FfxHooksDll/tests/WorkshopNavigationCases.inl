static bool NavigationChoose(EquipmentMenu::Action action,unsigned value=0){
    for(int i=0;i<EquipmentMenu::count;++i){
        const auto row=EquipmentMenu::rows[i];
        if(row.action==action&&row.value==value){
            NativeMenu::WrW(EquipmentMenu::menu.obj,NativeMenu::O_SELECTED,static_cast<short>(i));
            EquipmentMenu::Choose(row);return true;
        }
    }
    Check(false,"navigation target exists");return false;
}
static void WorkshopNavigationCases(){
    using A=EquipmentMenu::Action;using P=EquipmentMenu::Page;
    Check(WorkshopTestOpen(),"nested navigation fixture opens");WorkshopPrepareFusion();
    NavigationChoose(A::Piece,0);NavigationChoose(A::Fuse);
    const int obj=EquipmentMenu::menu.obj;
    NativeMenu::WrW(obj,NativeMenu::O_TOP,2);
    NavigationChoose(A::Value,9);const auto donor=EquipmentMenu::draft.otherId;
    NavigationChoose(A::Value,0);NavigationChoose(A::Value,2);
    Check(EquipmentMenu::page==P::TransferCount&&
          std::strcmp(EquipmentMenu::rows[EquipmentMenu::count-1].label,"Back to destination")==0,
          "fusion Back label describes retaining the draft rather than cancelling it");
    NavigationChoose(A::Second);NavigationChoose(A::Value,1);NavigationChoose(A::Value,3);
    const auto before=TestHost::state;NavigationChoose(A::Back);
    Check(EquipmentMenu::page==P::Destination&&EquipmentMenu::transfer==1&&EquipmentMenu::draft.otherId==donor,
          "second fusion confirmation returns to its destination with donor retained");
    if(EquipmentMenu::page==P::Destination){
        NavigationChoose(A::Back);
        Check(EquipmentMenu::page==P::Source&&EquipmentMenu::transfer==1,"second destination returns to second source");
        NavigationChoose(A::Back);
        Check(EquipmentMenu::page==P::TransferCount&&EquipmentMenu::transfer==0&&EquipmentMenu::draft.count==1&&EquipmentMenu::draft.to[0]==2,
              "backing out of second transfer retains the completed first transfer");
        NavigationChoose(A::Back);Check(EquipmentMenu::page==P::Destination,"transfer count returns to first destination");
        NavigationChoose(A::Back);Check(EquipmentMenu::page==P::Source,"first destination returns to first source");
        NavigationChoose(A::Back);
        Check(EquipmentMenu::page==P::Donor&&NativeMenu::RdW(obj,NativeMenu::O_TOP)==2&&NativeMenu::RdW(obj,NativeMenu::O_SELECTED)==8,
              "source Back restores donor list cursor and scroll");
        NavigationChoose(A::Back);
        Check(EquipmentMenu::page==P::Piece&&NativeMenu::RdW(obj,NativeMenu::O_SELECTED)==3,"donor Back restores Fuse equipment row");
        NavigationChoose(A::Back);Check(EquipmentMenu::page==P::Inventory,"piece Back restores inventory");
    }
    Check(TestHost::commits==0&&std::memcmp(&before,&TestHost::state,sizeof(before))==0,"nested Back never consumes or rolls");
    Check(WorkshopTestOpen(),"expansion navigation fixture opens");
    auto& piece=TestHost::state.pieces[0];piece.native[11]=2;
    for(unsigned i=2;i<4;++i){piece.native[14+2*i]=255;piece.native[15+2*i]=0;piece.abilities[i]=0;piece.ranks[i]=0;}
    NavigationChoose(A::Piece,0);NavigationChoose(A::Expand);
    Check(EquipmentMenu::count==3&&!WorkshopTestLists(A::Value,4|(1u<<8)),"default expansion lists one recipe per target capacity");
    NavigationChoose(A::Value,4);NavigationChoose(A::Back);
    Check(EquipmentMenu::page==P::Expand&&NativeMenu::RdW(EquipmentMenu::menu.obj,NativeMenu::O_SELECTED)==1,
          "expansion confirmation Back returns to original target capacity");
    EquipmentMenu::StopReady();
    Check(WorkshopTestOpen(),"stale navigation fixture opens");NavigationChoose(A::Piece,0);NavigationChoose(A::Fuse);NavigationChoose(A::Value,1);
    ++TestHost::state.revision;NavigationChoose(A::Back);
    Check(EquipmentMenu::page==P::Inventory&&EquipmentMenu::parentCount==0,"inventory drift discards obsolete navigation and donor selection");
    Check(WorkshopTestOpen(),"post-fusion navigation fixture opens");NavigationChoose(A::Piece,0);NavigationChoose(A::Fuse);NavigationChoose(A::Value,1);
    NavigationChoose(A::Value,0);NavigationChoose(A::Value,0);NavigationChoose(A::Value,0);NavigationChoose(A::Confirm);
    Check(TestHost::commits==1&&!TestHost::state.pieces[1].id&&EquipmentMenu::page==P::Piece,"successful fusion returns to the target after consuming the donor");
    NavigationChoose(A::Back);Check(EquipmentMenu::page==P::Inventory&&EquipmentMenu::parentCount==0,"post-commit Back cannot resurrect a consumed donor menu");
    Check(WorkshopTestOpen(),"missing-material navigation fixture opens");TestHost::state.items[70]=0;NavigationChoose(A::Piece,0);NavigationChoose(A::Refine);
    Check(EquipmentMenu::page==P::Confirm&&!WorkshopTestLists(A::Confirm,0),"missing materials have a review page without a confirmation action");
    NavigationChoose(A::Back);Check(EquipmentMenu::page==P::Piece,"failed refinement review returns to its immediate parent");
    EquipmentMenu::StopReady();
}
static void WorkshopExpansionSettingsCases(){
    namespace C=FfxHooks::Config;
    C::ResetForTests();C::LoadTextForTests("[core]\nlog_level=1\n","C:\\private-expansion.ini");
    C::SetProvidersForTests({nullptr,nullptr,nullptr,WorkshopF8Persist});f8AllowWrite=true;
    F8NativeSettingsReset();const int obj=NativeMenu::Alloc();if(!obj){Check(false,"expansion settings object allocated");return;}
    F8NativeSettingsPush(obj,NativeSettingsPage::Workshop);int expansion=-1;
    for(int i=0;i<F8NativeSettingsCount(F8NativeSettingsPage());++i){char label[128]{};F8NativeSettingsLabel(F8NativeSettingsPage(),i,label,sizeof(label));if(std::strstr(label,"Expansion"))expansion=i;}
    Check(expansion>=0,"F8 Workshop exposes expansion recipe preferences");
    if(expansion>=0){
        F8NativeSettingsActivate(obj,expansion);NativeMenu::rendered.clear();F8NativeSettingsDraw(obj,1);
        Check(WorkshopRendered("one")&&WorkshopRendered("1/2/3/4"),"expansion page describes both quantities");
        F8NativeSettingsActivate(obj,1);const auto read=C::ReadIntExact("equipment_workshop.expansion_recipe",1,2);
        Check(read.state==C::IntReadState::Valid&&read.value==2,"F8 expansion B persists in existing config");
        Check(F8NativeSettingsPage()==NativeSettingsPage::Workshop,"expansion selection returns to Workshop parent");
    }
    F8NativeSettingsReset();NativeMenu::Reset(obj);C::ResetForTests();
}
