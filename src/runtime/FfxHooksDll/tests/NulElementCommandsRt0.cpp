#include "../hooks/NulElementCommands.h"
#include <cstdio>
namespace N=FfxHooks::NulElements;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
int main(){
    for(const auto& command:N::Commands){
        unsigned char row[96]{};row[16]=static_cast<unsigned char>(command.animation);row[17]=static_cast<unsigned char>(command.animation>>8);
        row[23]=row[24]=4;row[25]=1;row[26]=5;row[37]=2;row[43]=1;
        Check(N::Grant(0x3000+command.id,row,96,true)==command.mask,"each canonical Nul maps to one private protection identity");
        Check(!N::Grant(0x3000+command.id,row,96,false),"expanded spells require their explicit module gate");
        Check(!N::Grant(0x3000+command.id,row,95,true),"truncated rows fail closed");
        row[35]=1;Check(!N::Grant(0x3000+command.id,row,96,true),"a damage command cannot be mistaken for a ward");row[35]=0;
        row[46]=1;Check(!N::Grant(0x3000+command.id,row,96,true),"a native status payload cannot leak through a custom ward");row[46]=0;
        row[16]^=1;Check(!N::Grant(0x3000+command.id,row,96,true),"unreviewed animation references fail closed");
    }
    Check(N::KeyMask("hook.custom03")==N::Poison&&N::KeyMask("spira.poison")==N::Poison,"Poison uses stable external keys");
    Check(N::KeyMask("hook.custom04")==N::Gravity&&N::KeyMask("spira.gravity")==N::Gravity,"Gravity uses stable external keys");
    Check(!N::KeyMask("other.element")&&!N::KeyMask(nullptr),"unrelated external elements are not rebound by index");
    Check((N::Native&~255u)==0&&N::Poison>255&&N::Gravity>255,"external charges never become native BYTE flags");
    Check(!N::Find(0x3176)&&!N::Find(0x2140),"unassigned or wrong-bank commands cannot grant protection");
    unsigned char legacy[96]{};legacy[16]=146;
    Check(N::Grant(0x3140,legacy,96,false)==N::Holy&&N::Grant(0x3141,legacy,96,false)==N::Shadow,"old Radiant/Umbral adapters remain compatible");
    std::printf("NulElementCommandsRt0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
