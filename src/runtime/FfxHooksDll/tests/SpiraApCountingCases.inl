// The native Enter handler repeatedly calls this counter until all AP settle.
// Execute its actual Steam body with seven late-game party members and a large
// reward. The only synthetic input is their small, valid growth table.
static void SpiraApCountingCases(){
#ifdef FFXHOOKS_TARGET_STEAM_20261001
    std::array<unsigned char,20+8*44> growth{};
    W16(growth.data(),1);W16(growth.data()+10,7);W16(growth.data()+12,44);W32(growth.data()+16,20);
    for(unsigned i=0;i<8;++i){auto* row=growth.data()+20+i*44;row[0x11]=2;row[0x13]=5;W32(row+0x14,22000);}
    const auto oldGrowth=*reinterpret_cast<std::uintptr_t*>(base+0xD2A938);
    *reinterpret_cast<std::uintptr_t*>(base+0xD2A938)=reinterpret_cast<std::uintptr_t>(growth.data());
    std::array<std::array<unsigned char,0x94>,7> saved{};
    *reinterpret_cast<unsigned char*>(base+0x1469F18)=7;
    for(unsigned i=0;i<7;++i){
        std::memcpy(saved[i].data(),Ply(i),saved[i].size());
        W32(Ply(i)+0x14,20000000);W32(Ply(i)+0x18,7000);Ply(i)[0x3B]=22;Ply(i)[0x3C]=101;
        W16(reinterpret_cast<unsigned char*>(base+0x1469F26+i*14),i);
        W32(reinterpret_cast<unsigned char*>(base+0x1F10F20+i*4),1000000);
    }
    const auto validations=S::ValidationCountForTests();
    const auto begin=std::chrono::steady_clock::now();
    const auto step=reinterpret_cast<int(__cdecl*)()>(base+0x4BA550);
    unsigned loops=0;
    while(step()&&++loops<20000){}
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    const auto checked=S::ValidationCountForTests()-validations;
    std::printf("SPIRA_AP_COUNTER loops=%u validation_calls=%llu elapsed_ms=%.3f\n",loops,static_cast<unsigned long long>(checked),elapsed);
    Check(loops<20000,"native large-AP result counting terminates");
    Check(checked==0,"AP result counting never revalidates unrelated Spira equipment data");
    for(unsigned i=0;i<7;++i){
        Check(R32(reinterpret_cast<unsigned char*>(base+0x1F10F20+i*4))==0&&R32(Ply(i)+0x14)==21000000&&
            R32(Ply(i)+0x18)==17000&&Ply(i)[0x3B]==67,"every party member receives the original AP and Sphere Levels exactly once");
        std::memcpy(Ply(i),saved[i].data(),saved[i].size());
    }
    *reinterpret_cast<std::uintptr_t*>(base+0xD2A938)=oldGrowth;
#endif
}
