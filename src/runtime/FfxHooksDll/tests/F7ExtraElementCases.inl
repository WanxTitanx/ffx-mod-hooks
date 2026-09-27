void TestExtendedElementApplyRestore(){
    for(unsigned bit:{0x10u,0x80u,0x20u,0x40u})for(unsigned category=0;category<3;++category){
        MemorySpy spy{};InitializeActor(spy,0,7,1000,250,100,25,200,10);
        const unsigned offsets[]={0x5DD,0x5DC,0x5DA};
        for(unsigned at=0x5DA;at<=0x5DD;++at)Put(spy,0,at,static_cast<uint8_t>(0x0F|bit));
        const MemoryIo io{&spy,&MemoryRead,&MemoryWrite,&CompleteAutoRefresh};
        const ActorRef actor{ActorAddress(spy,0),0};
        auto config=EnabledConfig(1000);
        auto& mask=category==0?config.global.elemWeak:category==1?config.global.elemResist:config.global.elemAbsorb;
        mask=static_cast<uint8_t>(bit);
        Runtime runtime{};runtime.BeginGeneration(1);
        const auto result=runtime.Update(io,config,true,-1,&actor,1);
        Expect(result.code==ResultCode::Applied,"extended element is admitted by the real Difficulty transaction");
        for(unsigned at=0x5DA;at<=0x5DD;++at)
            Expect(Get<uint8_t>(spy,0,at)==(0x0F|(at==offsets[category]?bit:0)),
                   "Holy Darkness and either custom bit replace only their selected native affinity");
        config.global.enabled=false;
        const auto restored=runtime.Update(io,config,true,-1,&actor,1);
        Expect(restored.code==ResultCode::Restored&&ActorCanariesIntact(spy,0),"extended affinity OFF restores ownership safely");
        for(unsigned at=0x5DA;at<=0x5DD;++at)Expect(Get<uint8_t>(spy,0,at)==(0x0F|bit),"extended affinity restores the exact native mask");
    }
    for(unsigned mask=0;mask<256;++mask){
        const std::string text="{\"diff_enabled\":true,\"diff_elemAbsorb\":"+std::to_string(mask)+"}";
        DifficultyConfig config{};const auto parsed=ParseConfig(text.data(),text.size(),&config);
        Expect(parsed.code==ConfigCode::Ok&&config.global.elemAbsorb==mask,"all native BYTE affinity masks survive parsing without truncation");
    }
}
