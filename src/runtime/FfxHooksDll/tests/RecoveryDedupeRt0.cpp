#include "../hooks/FieldScoutDedupe.h"
#include <cstdio>
#include <string>
#include <vector>

using FfxHooks::FieldScout::BoundedDedupe;
using FfxHooks::FieldScout::RememberResult;
namespace { int checks=0, failed=0;
void Check(bool value,const char* name) {++checks;if(!value){++failed;std::fprintf(stderr,"FAIL %s\n",name);}}
}
int main() {
    BoundedDedupe values;
    Check(values.Remember("asset") == RememberResult::Unavailable,"no use before allocation");
    Check(!values.Initialize(0),"zero capacity rejected");
    Check(values.Initialize(8),"small capacity initializes");
    Check(values.Remember(nullptr)==RememberResult::Invalid,"null key rejected");
    Check(values.Remember("")==RememberResult::Invalid,"empty key rejected");
    Check(values.Remember("Map/MACA/asset.dds")==RememberResult::Inserted,"insert first key");
    Check(values.Remember("mAP/maca/ASSET.DDS")==RememberResult::Duplicate,"ASCII case folding preserves old identity");
    Check(values.Size()==1,"duplicate does not grow storage");
    Check(values.Contains("map/maca/asset.dds"),"lookup finds case variant");
    Check(!values.Contains("map/maca/other.dds"),"lookup rejects unrelated key");
    std::string tooLong(BoundedDedupe::kKeyBytes,'x');
    Check(values.Remember(tooLong.c_str())==RememberResult::TooLong,"overlong key never silently truncates");
    Check(values.Size()==1,"rejected key does not enter set");
    std::vector<std::string> keys;
    for(unsigned n=1;n<8;++n) {
        keys.push_back("asset|"+std::to_string(n));
        Check(values.Remember(keys.back().c_str())==RememberResult::Inserted,"fill bounded set");
    }
    Check(values.Remember("new asset")==RememberResult::Full,"capacity exhaustion explicit");
    Check(values.Remember("Map/MACA/asset.dds")==RememberResult::Duplicate,"duplicate detected even when full");
    for(const auto& key:keys)Check(values.Contains(key.c_str()),"collision chain preserves exact identity");
    Check(!values.Initialize(16),"live allocation cannot be silently replaced");
    values.Reset();
    Check(values.Size()==0 && values.Capacity()==8,"reset clears contents but retains bounded storage");
    Check(!values.Contains("Map/MACA/asset.dds"),"reset removes earlier capture keys");
    Check(values.Remember("Map/MACA/asset.dds")==RememberResult::Inserted,"new capture generation can record same path");
    values.Release();
    Check(values.Capacity()==0 && !values.Contains("Map/MACA/asset.dds"),"release clears all storage");
    Check(values.Initialize(2000000),"legacy Ultra request accepted with hard memory ceiling");
    Check(values.Capacity()==BoundedDedupe::kMaximumEntries,"huge requested count bounded");
    Check(values.AllocatedBytes()<=8u*1024u*1024u,"dedupe allocation stays below eight MiB");
    for(std::size_t i=0;i<values.Capacity();++i) {
        const auto key="field|node|"+std::to_string(i);
        if(values.Remember(key.c_str())!=RememberResult::Inserted){Check(false,"dense table fill");break;}
    }
    Check(values.Size()==values.Capacity(),"dense table fills without losing hash collisions");
    for(std::size_t i=0;i<values.Capacity();i+=31) {
        const auto key="FIELD|NODE|"+std::to_string(i);
        if(values.Remember(key.c_str())!=RememberResult::Duplicate){Check(false,"dense lookup");break;}
    }
    Check(values.Remember("one more")==RememberResult::Full,"full dense table terminates");
    std::printf("RecoveryDedupeRt0: %d/%d passed\n",checks-failed,checks);
    return failed?1:0;
}
