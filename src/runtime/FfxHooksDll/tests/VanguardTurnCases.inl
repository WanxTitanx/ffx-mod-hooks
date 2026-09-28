static void VanguardTurnCases(unsigned char* actors,unsigned char* gear){
    using Edge=void(__cdecl*)(unsigned,unsigned char*);
    const auto edge=reinterpret_cast<Edge>(base+0x3B13D0);
    auto* actor=actors+8*0xF90;auto* armor=gear+22*3;
    const auto oldActor=std::vector<unsigned char>(actor,actor+0xF90),oldGear=std::vector<unsigned char>(armor,armor+22);
    std::memset(armor,0,22);armor[2]=1;armor[4]=armor[6]=8;armor[5]=1;armor[11]=4;
    for(unsigned i=0;i<4;++i)W16(armor+14+2*i,255);W16(armor+14,0x8090);
    W32(actor+0x5D4,100);W32(actor+0x598,999);actor[0x593]=3;actor[0xDC8]=1;W16(actor+0x606,0);
    edge(8,actor);Check(*reinterpret_cast<int*>(actor+0x5D4)==119,"MP Regen restores floor(2 percent maximum) at a real native Aeon turn edge");
    edge(8,actor);Check(*reinterpret_cast<int*>(actor+0x5D4)==138,"a subsequent native turn may regenerate again");
    W32(actor+0x5D4,995);edge(8,actor);Check(*reinterpret_cast<int*>(actor+0x5D4)==999,"MP Regen clamps to current effective maximum");
    W32(actor+0x5D4,100);actor[0xDC8]=0;edge(8,actor);
    Check(*reinterpret_cast<int*>(actor+0x5D4)==100,"inactive or reserve actors do not regenerate from a stray edge");
    actor[0xDC8]=1;actor[0x593]=255;edge(8,actor);
    Check(*reinterpret_cast<int*>(actor+0x5D4)==100,"a kernel row without equipped MP Regen grants nothing");
    actor[0x593]=3;W16(actor+0x606,1);edge(8,actor);
    Check(*reinterpret_cast<int*>(actor+0x5D4)==100,"dead actors do not receive MP Regen");
    W16(actor+0x606,0);edge(9,actor);
    Check(*reinterpret_cast<int*>(actor+0x5D4)==100,"turn identity must match the native actor rather than merely its reused pointer");
    std::memcpy(actor,oldActor.data(),0xF90);std::memcpy(armor,oldGear.data(),22);
}
