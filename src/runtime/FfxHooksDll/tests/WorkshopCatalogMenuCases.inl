// Real picker/confirmation/drawing with a bounded native-catalogue boundary.
static void WorkshopCatalogMenuCases(){
    namespace B=FfxHooks::EquipmentWorkshop::CatalogBridge;
    using A=EquipmentMenu::Action;using P=EquipmentMenu::Page;
    static workshop::Catalog supplied{};
    static const B::Provider provider{
        [](workshop::Catalog& out) noexcept {out=supplied;return true;},
        [](unsigned word) noexcept -> const char* {return word==0x81F4?"Hero's Bravery":word==0x808D?"P-Trade":nullptr;}};
    Check(B::Register(&provider),"extension picker has one verified catalogue provider");
    for(unsigned kind=0;kind<2;++kind){
        Check(WorkshopTestOpen(),"extension picker opens a fresh paid inventory");
        supplied={};supplied.proof=111;supplied.entries[0]={0x81F4,1,73,0};supplied.entries[6]={0x808D,2,73,0};
        auto& piece=TestHost::state.pieces[0];piece.fifthUnlocked=1;piece.native[5]=static_cast<unsigned char>(kind);
        const unsigned wanted=kind?0x808D:0x81F4,wrong=kind?0x81F4:0x808D;
        WorkshopTestChoose(A::Piece,0);WorkshopTestChoose(A::Fifth);
        Check(WorkshopTestLists(A::Value,wanted)&&!WorkshopTestLists(A::Value,wrong)&&!WorkshopTestLists(A::Value,0x8087),
              "picker uses current mapped IDs and filters weapon versus armour without default-ID aliases");
        if(!WorkshopTestLists(A::Value,wanted))continue;
        Check(WorkshopTestChoose(A::Value,wanted)&&EquipmentMenu::page==P::Confirm,
              "extension selection reaches the same paid native confirmation");
        NativeMenu::rendered.clear();EquipmentMenu::Draw(EquipmentMenu::menu.obj);
        Check(WorkshopRendered(kind?"P-Trade":"Hero's Bravery")&&WorkshopRendered("mod-only recipe")&&
              EquipmentMenu::preview.costs[73]==45&&EquipmentMenu::preview.gilDebit==200000,
              "confirmation shows the extension identity and declared 1.5x mod recipe and fifth fee");
        const auto before=TestHost::state;supplied.proof++;
        WorkshopTestChoose(A::Confirm);
        Check(TestHost::commits==0&&std::memcmp(&before,&TestHost::state,sizeof(before))==0,
              "kernel change after preview rejects the displayed extension without charging");
        WorkshopTestChoose(A::Fifth);WorkshopTestChoose(A::Value,wanted);WorkshopTestChoose(A::Confirm);
        Check(TestHost::commits==1&&TestHost::gil==800000&&TestHost::state.pieces[0].fifth==wanted,
              "fresh extension confirmation spends materials and Gil exactly once");
    }
    B::Unregister(&provider);EquipmentMenu::StopReady();
}
