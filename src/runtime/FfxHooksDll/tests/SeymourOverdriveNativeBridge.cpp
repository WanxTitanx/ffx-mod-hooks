// Test bridge: never compiled into the production DLL.
#include "../hooks/SeymourOverdrivePlan.h"
extern "C" {
unsigned SeymourOdSize(unsigned function) {
    using namespace FfxHooks::SeymourOverdrive;
    return function<FunctionCount?Functions[function].size:0;
}
unsigned SeymourOdRva(unsigned function) {
    using namespace FfxHooks::SeymourOverdrive;
    return function<FunctionCount?Functions[function].rva:0;
}
int SeymourOdBuild(unsigned function,unsigned base,unsigned destination,
                  unsigned counter,unsigned gauge,const unsigned char* source,
                  unsigned size,unsigned char* output,unsigned capacity) {
    return FfxHooks::SeymourOverdrive::Relocate(function,base,destination,
        {counter,gauge},source,size,output,capacity)?1:0;
}
}
