static bool WorkshopRendered(const char* text){
    for(const auto& row:NativeMenu::rendered)if(row.text.find(text)!=std::string::npos)return true;
    return false;
}
static void WorkshopMenuEconomyCases(){
    using A=EquipmentMenu::Action;using P=EquipmentMenu::Page;
    Check(WorkshopTestOpen()&&WorkshopTestChoose(A::Piece,0),"comparison fixture opens target");
    Check(!WorkshopTestLists(A::Mode,0),"per-piece mode selection is replaced by F8 policy");
    WorkshopTestChoose(A::Fuse);
    for(int row=0;row<EquipmentMenu::count;++row)if(EquipmentMenu::rows[row].action==A::Value&&EquipmentMenu::rows[row].value==1)NativeMenu::WrW(EquipmentMenu::menu.obj,NativeMenu::O_SELECTED,static_cast<short>(row));
    NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
    Check(WorkshopRendered("Target")&&WorkshopRendered("Donor")&&WorkshopRendered("Sensor"),"donor browsing renders target and hovered donor simultaneously");
    Check(WorkshopRendered("#1 ")&&WorkshopRendered("#6 "),"comparison renders two distinct item identities");
    Check(EquipmentMenu::selectedSlot==0,"hovering donor never replaces selected target");
    WorkshopTestChoose(A::Value,1);NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
    Check(WorkshopRendered("Donor")&&WorkshopRendered("Target"),"source ability selection retains both cards");
    WorkshopTestChoose(A::Value,0);NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
    Check(WorkshopRendered("Donor")&&WorkshopRendered("Target"),"destination ability selection retains both cards");
    for(const auto& row:NativeMenu::rendered)Check(row.x>=0&&row.x<1&&row.y>=0&&row.y<1,"comparison text coordinates remain on screen");
    Check(NativeMenu::rendered.size()<=32,"two equipment cards stay within a bounded native text-call budget");
    Check(WorkshopTestOpen()&&WorkshopTestChoose(A::Piece,0),"inventory comparison starts with a target");
    WorkshopTestChoose(A::Back);
    for(int row=0;row<EquipmentMenu::count;++row)if(EquipmentMenu::rows[row].action==A::Piece&&EquipmentMenu::rows[row].value==1)NativeMenu::WrW(EquipmentMenu::menu.obj,NativeMenu::O_SELECTED,static_cast<short>(row));
    NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
    Check(WorkshopRendered("Selected")&&WorkshopRendered("Candidate")&&EquipmentMenu::selectedSlot==0,"choosing another equipment compares it against the retained target");
    Check(WorkshopTestOpen()&&WorkshopTestChoose(A::Piece,0),"roulette prerequisites fixture opens");
    TestHost::state.items[73]=0;EquipmentMenu::snapshot=TestHost::state;
    WorkshopTestChoose(A::Refine);NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
    Check(EquipmentMenu::page==P::Confirm&&!WorkshopTestLists(A::Confirm,0)&&WorkshopRendered("Ability Sphere")&&WorkshopRendered("MISSING"),"missing alternate material is visible and disables confirmation");
    Check(TestHost::commits==0,"showing unmet requirements never spends resources");
    Check(WorkshopTestOpen()&&WorkshopTestChoose(A::Piece,0),"hidden-winner fixture opens");
    WorkshopTestChoose(A::Refine);NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
    Check(WorkshopRendered("Prerequisites")&&WorkshopRendered("Power Sphere")&&WorkshopRendered("Skill Sphere")&&WorkshopRendered("Ability Sphere"),"roulette preview displays every alternative ingredient rather than revealing the winning recipe");
    WorkshopTestChoose(A::Confirm);
    Check(TestHost::commits==1&&TestHost::state.rolls==1,"default B works without choosing a per-piece mode");
    Check(WorkshopTestOpen()&&WorkshopTestChoose(A::Piece,0),"policy drift fixture opens");
    WorkshopTestChoose(A::Refine);
    FfxHooks::Config::LoadTextForTests("[equipment_workshop]\nrefinement_mode=1\n","C:\\private-workshop-menu.ini");
    WorkshopTestChoose(A::Confirm);
    Check(TestHost::commits==0,"a different F8 mode invalidates an already quoted operation");
    WorkshopTestChoose(A::Refine);WorkshopTestChoose(A::Confirm);
    Check(TestHost::commits==1&&TestHost::state.rolls==0&&TestHost::state.pieces[0].ranks[0]==1&&TestHost::state.pieces[0].ranks[3]==1,"explicit A refines all occupied abilities without RNG");
    for(unsigned mode:{2u,1u}){
        Check(WorkshopTestOpen(),"mod-only refinement disclosure fixture opens");
        TestHost::state.pieces[0].native[14]=20; // No AP has no native Customize recipe.
        if(mode==1)FfxHooks::Config::LoadTextForTests("[equipment_workshop]\nrefinement_mode=1\n","C:\\private-workshop-menu.ini");
        WorkshopTestChoose(A::Piece,0);WorkshopTestChoose(A::Refine);
        NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
        Check(EquipmentMenu::page==P::Confirm&&WorkshopRendered("mod-only recipe"),"A and B disclose non-native recipe pricing before confirmation");
        Check(TestHost::commits==0&&TestHost::state.rolls==0,"recipe disclosure never commits or advances RNG");
    }
    Check(WorkshopTestOpen(),"maxed mod-only ability fixture opens");
    TestHost::state.pieces[0].native[14]=20;TestHost::state.pieces[0].ranks[0]=10;
    WorkshopTestChoose(A::Piece,0);WorkshopTestChoose(A::Refine);
    NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
    Check(!WorkshopRendered("mod-only recipe"),"a maxed non-native ability is not advertised as a charged roulette outcome");
    Check(WorkshopTestOpen(),"mod-only fusion disclosure fixture opens");
    TestHost::state.pieces[1].native[14]=20;
    Check(WorkshopTestFusion(1,false),"mod-only donor ability reaches fusion confirmation");
    NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
    Check(EquipmentMenu::page!=P::Confirm&&TestHost::commits==0&&TestHost::state.pieces[1].id,"native Customize policy rejects non-native fusion without consuming the donor");
    Check(WorkshopTestOpen(),"unselected mod-only donor ability fixture opens");
    TestHost::state.pieces[1].native[16]=20;
    Check(WorkshopTestFusion(1,false),"native donor ability reaches fusion confirmation");
    NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
    Check(!WorkshopRendered("mod-only recipe"),"unselected donor abilities do not change the disclosed fusion recipe");
    EquipmentMenu::StopReady();TestHost::available=false;
}
