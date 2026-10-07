// Actual accuracy producer and one controlled native RNG stream; no game process.
static void VanguardAccuracyCases(unsigned char* source,unsigned char* target,unsigned char* gear){
    using Accuracy=int(__cdecl*)(const unsigned char*,const unsigned char*,const unsigned char*,const unsigned char*,int);
    const auto accuracy=reinterpret_cast<Accuracy>(base+(::FfxHooks::ExecutableProfile::Rva<0x38A950>()));
    const auto savedSource=std::vector<unsigned char>(source,source+0xF90);
    const auto savedTarget=std::vector<unsigned char>(target,target+0xF90);
    const auto savedGear=std::vector<unsigned char>(gear,gear+44);
    std::array<unsigned char,96> row{};std::array<unsigned char,44> info{};
    row[0x1C]=0x10;row[0x29]=100;
    source[0x5AD]=target[0x5AD]=source[0x65F]=target[0x661]=source[0x662]=target[0x663]=0;
    target[0x5AE]=10;source[0x60A]=0;W16(target+0x616,0);
    auto* armor=gear+22;std::memset(armor,0,22);armor[2]=1;armor[4]=armor[6]=1;armor[5]=1;armor[11]=4;
    for(unsigned i=0;i<4;++i)W16(armor+14+2*i,255);W16(armor+14,0x8091);target[0x593]=1;
    rngValue=80;rngCalls=0;
    Check(accuracy(source,target,row.data(),info.data(),0)==0&&rngCalls==1,"equipped Elude does not change accuracy while not defending");
    W16(target+0x616,0x800);rngCalls=0;
    Check(accuracy(source,target,row.data(),info.data(),0)==1&&rngCalls==1,"Elude adds fifty evasion only during native Defend and samples RNG once");
    target[0x5AE]=250;row[0x29]=255;source[0x5AD]=60;rngValue=40;rngCalls=0;
    Check(accuracy(source,target,row.data(),info.data(),0)==1&&rngCalls==1,"Elude computes evasion above 255 without BYTE wrap or saturation");
    target[0x593]=255;rngCalls=0;
    Check(accuracy(source,target,row.data(),info.data(),0)==0&&rngCalls==1,"unequipping Elude restores exact native odds");
    target[0x593]=1;row[0x1C]=0;rngCalls=0;
    Check(accuracy(source,target,row.data(),info.data(),2)==0&&rngCalls==0,"native guaranteed-hit mode stays guaranteed without spending an accuracy roll");
    row[0x1F]=0x80;W16(target+0x606,0);
    // Bit 0x00800000 is byte +0x1E, not a global accuracy override.
    row[0x1F]=0;row[0x1E]=0x80;
    Check(accuracy(source,target,row.data(),info.data(),0)==2,"guaranteed-hit policy preserves native invalid revival target rejection");
    row[0x1E]=0;row[0x1C]=0x10;info[7]=1;rngCalls=0;
    Check(accuracy(source,target,row.data(),info.data(),2)==0&&rngCalls==0,"sleep's native guaranteed-hit exception is preserved");
    info[7]=0;target[0x593]=255;W16(target+0x616,0);target[0x5AE]=0;source[0x5AD]=0;
    row[0x29]=100;rngValue=100;rngCalls=0;
    Check(accuracy(source,target,row.data(),info.data(),0)==0&&rngCalls==1,
          "an effective one-hundred-percent command does not miss on the native inclusive RNG endpoint");
    row[0x29]=99;rngCalls=0;
    Check(accuracy(source,target,row.data(),info.data(),0)==1&&rngCalls==1,
          "guaranteed-hit endpoint correction does not make lower-probability attacks infallible");
    row[0x29]=100;rngCalls=0;
    Check(accuracy(source,target,row.data(),info.data(),2)==1&&rngCalls==1,
          "explicit native forced-miss result is not replaced by the probability correction");
    std::memcpy(source,savedSource.data(),0xF90);std::memcpy(target,savedTarget.data(),0xF90);std::memcpy(gear,savedGear.data(),44);
    rngValue=90;
}
