// Jarvis-HOOK: real native MP/status producers in a private image.
static void VanguardCostStatusCases(unsigned char* s,unsigned char* t,unsigned char* gear){
 using Cost=int(__cdecl*)(unsigned,const unsigned char*);
 using Quarter=int(__cdecl*)(unsigned char*,unsigned*,int);
 using Status=int(__cdecl*)(unsigned,unsigned char*,unsigned,unsigned char*,const unsigned char*,unsigned*,unsigned*,unsigned char*,int,int*,int*);
 const auto cost=reinterpret_cast<Cost>(base+0x38D030),unusedCost=cost;(void)unusedCost;
 const auto quarter=reinterpret_cast<Quarter>(base+0x38C5F0);
 const auto status=reinterpret_cast<Status>(base+0x38AEC0);
 const auto savedS=std::vector<unsigned char>(s,s+0xF90),savedT=std::vector<unsigned char>(t,t+0xF90),savedGear=std::vector<unsigned char>(gear,gear+22);
 std::array<unsigned char,96> row{};row[0x25]=100;row[0x17]=1;s[0x592]=0;gear[2]=1;gear[4]=gear[5]=gear[6]=0;gear[11]=4;
 for(unsigned i=0;i<4;++i)W16(gear+14+2*i,255);
 W16(gear+14,0x808A);W16(s+0x6BC,0);s[0x640]=s[0x6CE]=0;
 Check(cost(0,row.data())==75,"native Efficiency costs 75 percent");W16(s+0x6BC,0x4000);Check(cost(0,row.data())==25,"Half MP and Efficiency cost 25 percent");
 row[0x25]=5;Check(cost(0,row.data())==2,"fractional costs round upward");row[0x25]=100;W16(s+0x6BC,0x4040);Check(cost(0,row.data())==50,"Magic Booster doubles reduced cost");
 W16(s+0x6BC,0xC040);Check(cost(0,row.data())==2,"One MP preserves Booster priority");s[0x640]=4;Check(cost(0,row.data())==0,"MP-zero dominates");s[0x640]=0;s[0x6CE]=1;Check(cost(0,row.data())==0,"native free action dominates");
 s[0x6CE]=0;s[0x592]=255;W16(s+0x6BC,0x4000);row[0x25]=5;Check(cost(0,row.data())==3,"unequipped Efficiency retains ceil-half");
 W16(t+0x616,0x40);t[0x6DA]=0;unsigned bits=0;Check(quarter(t,&bits,-1000)==-1000&&bits==0&&t[0x6DA]==0,"Shield never mitigates healing or consumes protection");Check(quarter(t,&bits,1000)==250&&(bits&0x8000)&&t[0x6DA]==1,"Shield still mitigates damage");
 row.fill(0);row[0x2E+12]=254;row[0x47]=4;std::array<unsigned char,44> info{};unsigned hits[8]{},effects[8]{};int amounts[3]{},counter=0;
 using Protection=int(__cdecl*)(const void*,unsigned*,int*,const void*,int);
 const auto shell=reinterpret_cast<Protection>(base+0x38AE80),protect=reinterpret_cast<Protection>(base+0x38AE00);
 for(unsigned kind=1;kind<=2;++kind){
     const auto producer=kind==1?protect:shell;row[0x20]=static_cast<unsigned char>(kind);
     info[0xA]=info[0xB]=1;unsigned flags=0x21;int divisor=7;const auto beforeInfo=info;
     Check(producer(row.data(),&flags,&divisor,info.data(),-1000)==-1000&&flags==0x21&&divisor==7&&info==beforeInfo,
           "shared Shell/Protect healing preserves amount, flags, divisor and active protection");
     flags=0;divisor=0;
     Check(producer(row.data(),&flags,&divisor,info.data(),1000)==500,
           "shared Shell/Protect retains native damage mitigation");
 }
 row[0x20]=0;info.fill(0);

 std::memset(s+0x5DE,0,25);std::memset(s+0x5F7,0,13);std::memset(t+0x641,0,25);W16(t+0x606,0);W16(t+0x62C,0);t[0x608]=info[7]=1;
 status(0,s,18,t,row.data(),hits,effects,info.data(),0,amounts,&counter);Check(info[7]==3&&t[0x608]==1,"refresh applies separate duration resistance");
 t[0x641+12]=255;info[7]=1;status(0,s,18,t,row.data(),hits,effects,info.data(),0,amounts,&counter);Check(info[7]==1&&t[0x608]==1,"refresh respects immunity");
 t[0x641+12]=0;t[0x608]=info[7]=255;status(0,s,18,t,row.data(),hits,effects,info.data(),0,amounts,&counter);Check(info[7]==255&&t[0x608]==255,"permanent duration remains permanent");
 t[0x608]=info[7]=0;row[0x47]=1;status(0,s,18,t,row.data(),hits,effects,info.data(),0,amounts,&counter);Check(info[7]==1,"finite successful status retains a turn");
 t[0x608]=info[7]=4;row[0x20]=0x20;row[0x47]=2;status(0,s,18,t,row.data(),hits,effects,info.data(),0,amounts,&counter);Check(info[7]==2&&t[0x608]==4,"cleanse uses native subtraction");
 std::memcpy(s,savedS.data(),0xF90);std::memcpy(t,savedT.data(),0xF90);std::memcpy(gear,savedGear.data(),22);
}
