#include "../hooks/ElementScanCore.h"
#include "../hooks/ElementScanSprites.h"
#include <cstdio>
#include <cmath>
namespace E=FfxHooks::ElementScan;
static unsigned checks=0,failures=0;
static void Check(bool b,const char* s){++checks;if(!b){++failures;std::printf("FAIL %s\n",s);}}
int main(){
    E::Settings c{};Check(E::Valid(c),"default Scan palette is valid");
    for(unsigned bit:{0x20u,0x40u}){c.extraBit=bit;
        for(unsigned mask=0;mask<256;++mask){auto row=E::Orbs(mask,c);const unsigned bits[]={0x10,0x80,bit};
            for(unsigned i=0;i<3;++i){
                Check(row[i].active==bool(mask&bits[i])&&row[i].bit==bits[i],"each high-bit affinity maps to its own fixed column");
                Check(row[i].x>=350&&row[i].x+row[i].size<E::PanelWidth&&row[i].size>0,"all three extra orbs remain inside the enlarged native panel");
            }
        }
    }
    c.extraBit=0x10;Check(!E::Valid(c),"the configurable third bit cannot duplicate Holy");
    c={};c.rgb[0]=0x1000000;Check(!E::Valid(c),"colors outside RGB24 fail closed");
    Check(E::FromHsv({0,100,100})==0xFF0000&&E::FromHsv({120,100,100})==0x00FF00&&E::FromHsv({240,100,100})==0x0000FF,"hue wheel reaches the three primary colors");
    Check(E::FromHsv({120,0,100})==0xFFFFFF&&E::FromHsv({240,100,0})==0,"saturation and brightness include white and black");
    Check(E::NativeColor(0xFF0000)==0x800000FF&&E::NativeColor(0x0000FF)==0x80FF0000,"RGB colors use the engine ABGR byte order and normal alpha");
    for(unsigned rgb:{0u,0xFFFFFFu,0xFF0000u,0x00FF00u,0x0000FFu,0xFFE080u,0xA35CEFu,0x5FCF7Eu}){
        const auto hsv=E::ToHsv(rgb);const auto round=E::FromHsv(hsv);
        Check(hsv.h<360&&hsv.s<=100&&hsv.v<=100,"color editor components stay in range");
        for(unsigned shift:{0u,8u,16u})Check(std::abs(int((rgb>>shift)&255)-int((round>>shift)&255))<=4,"slider quantization preserves a close RGB color");
    }
    for(const auto uv:{E::SilverSphere,E::Connector,E::InactiveSphere}){
        Check(uv.u0>=0&&uv.u0<uv.u1&&uv.u1<=1&&uv.v0>=0&&uv.v0<uv.v1&&uv.v1<=1,"native sprite crops remain inside the original atlas");
    }
    Check(std::fabs(E::SilverSphere.u0*1024.f-(230.f+191.f*247.f/225.f))<.001f,"silver crop aligns with the original fourth sphere");
    Check(E::InactiveSphere.u0*1024.f==557.f&&E::InactiveSphere.v1*1024.f==1010.f,"inactive sphere reuses the exact native mask tile");
    std::printf("ELEMENT_SCAN_CORE %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
