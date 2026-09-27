// In-memory host for the real Workshop menu + shared transaction core. Native
// memory and disk consumption are checked separately by runtime/save-flow RT1.
static bool WorkshopTestChoose(EquipmentMenu::Action action,unsigned value=0){
    for(int i=0;i<EquipmentMenu::count;++i){
        const auto row=EquipmentMenu::rows[i];
        if(row.action==action&&row.value==value){EquipmentMenu::Choose(row);return true;}
    }
    Check(false,"requested row exists in the actual Workshop page");return false;
}
static bool WorkshopTestLists(EquipmentMenu::Action action,unsigned value){
    for(int i=0;i<EquipmentMenu::count;++i)if(EquipmentMenu::rows[i].action==action&&EquipmentMenu::rows[i].value==value)return true;
    return false;
}
static bool WorkshopTestOpen(){
    EquipmentMenu::StopReady();EquipmentMenu::wantOpen=EquipmentMenu::wantClose=0;
    foreground=true;NativeMenu::padDirection=NativeMenu::padEdge=0;
    pointerSample={};pointerState={};
    std::array<unsigned char,4400> gear{};std::array<std::uint16_t,112> materials{};materials.fill(255);
    for(unsigned slot=0;slot<12;++slot){auto* n=gear.data()+22*slot;n[2]=1;n[6]=255;n[11]=4;
        for(unsigned i=0;i<4;++i){const unsigned words[]={0,100,98,99};n[14+2*i]=static_cast<unsigned char>(words[i]);n[15+2*i]=128;}}
    if(workshop::Import(gear.data(),materials.data(),12345,TestHost::state)!=workshop::Error::Ok)return false;
    for(unsigned slot=0;slot<12;++slot)TestHost::state.pieces[slot].mode=2;
    TestHost::state.pieces[1].ranks[0]=7;TestHost::state.pieces[1].ranks[1]=8;
    TestHost::available=true;TestHost::customizeUnlocked=true;TestHost::failReadback=false;TestHost::commits=0;TestHost::gil=1000000;
    FfxHooks::Config::LoadTextForTests("[equipment_workshop]\nrefinement_mode=2\n","C:\\private-workshop-menu.ini");
    EquipmentMenu::ownerFilter=-1;NativeMenu::sounds.clear();
    return EquipmentMenu::Open();
}
static void WorkshopPrepareFusion(){
    TestHost::state.pieces[0].native[14]=1;TestHost::state.pieces[0].native[16]=98;
}
static bool WorkshopTestFusion(unsigned count=1,bool fresh=true){
    using A=EquipmentMenu::Action;
    if(fresh&&!WorkshopTestOpen())return false;
    WorkshopPrepareFusion();
    if(!WorkshopTestChoose(A::Piece,0)||!WorkshopTestChoose(A::Fuse)||!WorkshopTestChoose(A::Value,1)||
       !WorkshopTestChoose(A::Value,0)||!WorkshopTestChoose(A::Value,2))return false;
    if(count==2)return WorkshopTestChoose(A::Second)&&WorkshopTestChoose(A::Value,1)&&WorkshopTestChoose(A::Value,3);
    return WorkshopTestChoose(A::Value,0);
}
static void WorkshopTestRetireDonor(){
    workshop::Request r{};r.op=workshop::Op::Retire;r.slot=1;
    r.pieceId=TestHost::state.pieces[1].id;r.revision=TestHost::state.revision;
    workshop::Plan plan{};Check(workshop::Preview(TestHost::state,r,plan)==workshop::Error::Ok,"host observed donor retirement is valid");
    TestHost::state=plan.after;
}
static void WorkshopMenuTransactionCases(){
    using A=EquipmentMenu::Action;using P=EquipmentMenu::Page;
    for(unsigned transfers:{1u,2u}){
        Check(WorkshopTestFusion(transfers)&&EquipmentMenu::page==P::Confirm,"actual menu reaches reviewed fusion for one/two abilities");
        const auto before=TestHost::state;const auto twin=before.pieces[2];
        Check(before.pieces[1].id!=0&&TestHost::commits==0,"preview never consumes the donor");
        NativeMenu::sounds.clear();WorkshopTestChoose(A::Confirm);
        Check(TestHost::commits==1&&!TestHost::state.pieces[1].id&&!TestHost::state.pieces[1].native[2],"menu fusion consumes native occupancy and extension identity once");
        Check(TestHost::state.pieces[0].abilities[2]==before.pieces[1].abilities[0]&&TestHost::state.pieces[0].ranks[2]==7,"selected ability keeps only its own donor rank");
        Check(transfers==1||(TestHost::state.pieces[0].abilities[3]==before.pieces[1].abilities[1]&&TestHost::state.pieces[0].ranks[3]==8),"second transferred ability keeps its instance and rank");
        Check(TestHost::state.items[73]+1==before.items[73]&&TestHost::state.items[77]+(transfers==2?1:0)==before.items[77]&&TestHost::gil==1000000-10000*transfers&&std::memcmp(&TestHost::state.pieces[2],&twin,sizeof(twin))==0,"Customize fraction and Gil are exact and identical unselected equipment survives");
        Check(!EquipmentMenu::snapshot.pieces[1].id&&std::strstr(EquipmentMenu::notice,"consumed"),"successful fusion explicitly reports verified donor consumption");
        Check(NativeMenu::sounds==std::vector<int>{1},"commit emits one success sound");
        WorkshopTestChoose(A::Back);
        Check(EquipmentMenu::page==P::Inventory&&!WorkshopTestLists(A::Piece,1)&&WorkshopTestLists(A::Piece,2),"consumed donor disappears from inventory without hiding its identical twin");
        WorkshopTestChoose(A::Piece,0);WorkshopTestChoose(A::Fuse);
        Check(!WorkshopTestLists(A::Value,1)&&WorkshopTestLists(A::Value,2),"new fusion cannot select the consumed donor");
        auto stale=EquipmentMenu::draft;stale.other=1;stale.otherId=before.pieces[1].id;stale.count=1;stale.to[0]=2;
        workshop::Plan plan{};Check(FfxHooks::EquipmentWorkshop::Preview(stale,plan)==workshop::Error::Stale,"replayed donor identity cannot be reused with a fresh revision");
    }
    Check(WorkshopTestFusion(),"cancel fixture reaches confirmation");
    auto before=TestHost::state;NativeMenu::sounds.clear();WorkshopTestChoose(A::Back);
    Check(!TestHost::commits&&std::memcmp(&before,&TestHost::state,sizeof(before))==0,"cancel preserves donor, materials and RNG exactly");
    Check(NativeMenu::sounds==std::vector<int>{4},"cancel row emits one native back sound");
    Check(WorkshopTestFusion(),"readback failure fixture reaches confirmation");
    TestHost::failReadback=true;WorkshopTestChoose(A::Confirm);
    Check(TestHost::commits==1&&!TestHost::state.pieces[1].id,"readback outage occurs after a real successful transaction");
    Check(EquipmentMenu::page==P::Info&&EquipmentMenu::selectedSlot==-1&&!EquipmentMenu::snapshot.pieces[1].id,"failed post-commit capture cannot display the consumed donor from an old snapshot");
    Check(std::strstr(EquipmentMenu::notice,"committed")!=nullptr,"post-commit capture failure does not invite a second transaction");
    Check(WorkshopTestOpen()&&WorkshopTestChoose(A::Piece,0),"back refresh fixture opens a piece");
    WorkshopTestRetireDonor();WorkshopTestChoose(A::Back);
    Check(!WorkshopTestLists(A::Piece,1),"back to inventory refreshes an externally retired donor");
    Check(WorkshopTestOpen(),"filter refresh fixture opens inventory");
    WorkshopTestRetireDonor();WorkshopTestChoose(A::Filter);
    Check(!WorkshopTestLists(A::Piece,1),"filter cannot rebuild the list from stale consumed gear");
    Check(WorkshopTestOpen(),"stale selection fixture opens inventory");
    WorkshopTestRetireDonor();WorkshopTestChoose(A::Piece,1);
    Check(EquipmentMenu::page==P::Inventory&&EquipmentMenu::selectedSlot==-1&&!WorkshopTestLists(A::Piece,1),"stale inventory row cannot reopen a retired donor");
    Check(WorkshopTestOpen(),"materials failure fixture opens inventory");
    TestHost::state.items[73]=0;EquipmentMenu::snapshot=TestHost::state;
    WorkshopTestFusion(1,false);
    Check((EquipmentMenu::page==P::Piece||EquipmentMenu::page==P::Confirm)&&TestHost::commits==0&&TestHost::state.pieces[1].id,"unpaid fusion leaves the donor intact");
    Check(!NativeMenu::sounds.empty()&&NativeMenu::sounds.back()==3,"rejected preview produces error feedback, not success");
    EquipmentMenu::StopReady();TestHost::available=false;
}
static void WorkshopMenuAudioCases(){
    Check(WorkshopTestOpen(),"audio fixture opens the real menu");
    const int obj=EquipmentMenu::menu.obj;
    // First pointer observation seeds position; only a later movement is hover.
    pointerSample.valid=true;pointerSample.x=.99f;pointerSample.y=.5f;
    EquipmentMenu::delay=0;NativeMenu::sounds.clear();EquipmentMenu::Input(obj);
    Check(NativeMenu::sounds.empty(),"idle frame is silent");
    NativeMenu::padDirection=0x4000;EquipmentMenu::Input(obj);NativeMenu::padDirection=0;
    Check(NativeMenu::RdW(obj,NativeMenu::O_SELECTED)==1&&NativeMenu::sounds==std::vector<int>{1},"keyboard/controller navigation emits one sound");
    NativeMenu::sounds.clear();pointerSample.valid=true;pointerSample.x=.10f;pointerSample.y=.24f+3*.066f+.02f;
    EquipmentMenu::Input(obj);EquipmentMenu::Input(obj);
    Check(NativeMenu::RdW(obj,NativeMenu::O_SELECTED)==3&&NativeMenu::sounds==std::vector<int>{1},"mouse row change emits once and stationary hover is silent");
    NativeMenu::sounds.clear();pointerSample.x=.99f;pointerSample.y=.5f;pointerSample.wheelSteps=1;
    EquipmentMenu::Input(obj);
    Check(NativeMenu::sounds==std::vector<int>{1},"wheel scroll uses native navigation feedback");
    pointerSample={};NativeMenu::sounds.clear();NativeMenu::WrW(obj,NativeMenu::O_SELECTED,1);EquipmentMenu::delay=0;
    NativeMenu::padEdge=0x20;EquipmentMenu::Input(obj);EquipmentMenu::Input(obj);EquipmentMenu::Tick();
    for(unsigned i=0;i<20;++i){EquipmentMenu::Input(obj);EquipmentMenu::Tick();}
    Check(EquipmentMenu::page==EquipmentMenu::Page::Piece&&NativeMenu::sounds==std::vector<int>{1},"held confirm changes page and sounds once, never replaying on the next page");
    NativeMenu::padEdge=0;EquipmentMenu::Input(obj);NativeMenu::sounds.clear();
    NativeMenu::padEdge=0x40;EquipmentMenu::Input(obj);EquipmentMenu::Tick();
    Check(EquipmentMenu::page==EquipmentMenu::Page::Inventory&&NativeMenu::sounds==std::vector<int>{4},"keyboard/controller back emits one cancel sound");
    NativeMenu::padEdge=0;EquipmentMenu::delay=0;EquipmentMenu::Input(obj);
    pointerSample.valid=true;pointerSample.x=.10f;pointerSample.y=.24f+2*.066f+.02f;pointerSample.buttonDown=true;
    NativeMenu::padDirection=0x4000;
    NativeMenu::sounds.clear();EquipmentMenu::Input(obj);EquipmentMenu::Tick();NativeMenu::padDirection=0;
    Check(EquipmentMenu::page==EquipmentMenu::Page::Piece&&NativeMenu::sounds==std::vector<int>{1},"hover plus mouse click in the same frame does not double-play feedback");
    for(unsigned i=0;i<20;++i){EquipmentMenu::Input(obj);EquipmentMenu::Tick();}
    Check(NativeMenu::sounds==std::vector<int>{1},"held mouse click cannot trigger or sound on the new page");
    NativeMenu::sounds.clear();foreground=false;EquipmentMenu::Input(obj);EquipmentMenu::Tick();
    Check(NativeMenu::sounds.empty(),"focus loss is silent");
    foreground=true;EquipmentMenu::StopReady();TestHost::available=false;
}
