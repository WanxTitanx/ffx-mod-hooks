static unsigned statusObservations=0,statusObservedCapacity=0;
static void ObserveStatus(void*,unsigned capacity) noexcept {++statusObservations;statusObservedCapacity=capacity;}
static void ExtendedEquipmentCases(W::Store& store,const std::wstring& path,W::SaveImage& native,workshop::State& state){
    const auto shared=reinterpret_cast<int(__cdecl*)(float,float,const void*)>(imageBase+(::FfxHooks::ExecutableProfile::Rva<0x4D8A70>()));
    const auto* record=reinterpret_cast<const void*>(imageBase+(::FfxHooks::ExecutableProfile::Rva<0xD30F2C>()));
    ResetDraw();shared(ScaleX(970),ScaleY(340),record);
    Check(frames==5&&labels.size()==4&&HasRank(1)&&HasRank(2)&&HasRank(3)&&HasRank(4),
          "shared equipped/selected comparison draws the fifth and individual refinements");
    native[0x44DC+6]=0;state.pieces[0].native[6]=0;
    native[0x560C+0x2D]=0;native[0x560C+0x2E]=255;FfxHooks::RonsoPool::SealSave(native);
    Check(store.Write(path,native,state)&&W::LoadForTests(path.c_str(),native,native)&&W::CommitLoadForTests(native),"Status fixture associates an equipped piece through the real save boundary");
    // Only renderer setup, animation curve and header text are substituted.
    // The Status list builder and each ability row remain native code.
    for(unsigned rva:{(::FfxHooks::ExecutableProfile::Rva<0x4DCE30u>()),(::FfxHooks::ExecutableProfile::Rva<0x4C1770u>()),(::FfxHooks::ExecutableProfile::Rva<0x4C0BB0u>()),(::FfxHooks::ExecutableProfile::Rva<0x4D3090u>()),(::FfxHooks::ExecutableProfile::Rva<0x4F9280u>()),(::FfxHooks::ExecutableProfile::Rva<0x4BF0F0u>())})Redirect(rva,reinterpret_cast<const void*>(&NoDevice));
    Redirect((::FfxHooks::ExecutableProfile::Rva<0x4BF0D0>()),reinterpret_cast<const void*>(&NoDevice));
    Redirect((::FfxHooks::ExecutableProfile::Rva<0x4A9810>()),reinterpret_cast<const void*>(&NoDevice));
    Redirect((::FfxHooks::ExecutableProfile::Rva<0x390250>()),reinterpret_cast<const void*>(&CustomizeNativeTest::UnusedKernel));
    unsigned char object[152]{};
    W::NativeUi::SetStatusObserver(ObserveStatus);
    ResetDraw();reinterpret_cast<int(__cdecl*)(void*)>(imageBase+(::FfxHooks::ExecutableProfile::Rva<0x4D2760>()))(object);
    Check(labels.size()==4&&HasRank(1)&&HasRank(2)&&HasRank(3)&&HasRank(4),"Status Auto-Abilities includes the equipped fifth and per-instance refinements");
    Check(statusObservations==1&&statusObservedCapacity==9,"Status observer runs once with the actual fifth-slot capacity after ranked native rows");
    W::NativeUi::SetStatusObserver(nullptr);
}
