#include <cstdio>
#include <string>
#if __has_include("../hooks/ElementRegistry.h")
#include "../hooks/ElementRegistry.h"
namespace E=FfxHooks::ElementalDominion;
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
static E::Registry MakeRegistry(unsigned count,bool reversed=false){
    E::Registry result;
    for(unsigned n=0;n<count;++n){const unsigned i=reversed?count-1-n:n;
        E::Element element; element.key="test.element-"+std::to_string(i);
        element.label="Element "+std::to_string(i);element.labelKey=element.key+".name";
        element.nativeBit=i<8?1u<<i:0;element.rgb=0x112233;
        Check(result.Add(element)==E::Error::Ok,"a unique bounded descriptor is admitted");
    }
    return result;
}
int main(){
    for(unsigned count:{8u,9u,10u,16u,32u}){
        auto first=MakeRegistry(count),second=MakeRegistry(count,true);
        Check(first.Size()==count&&second.Size()==count,"supported registry sizes retain every descriptor");
        for(unsigned i=0;i<count;++i){const auto key="test.element-"+std::to_string(i);
            const auto* a=first.Find(key);const auto* b=second.Find(key);
            Check(a&&b&&a->key==b->key&&a->nativeBit==b->nativeBit&&a->label==b->label,
                  "reordering never changes stable identity or native mapping");
        }
        const auto duplicate=*first.At(0);
        Check(first.Add(duplicate)!=E::Error::Ok&&first.Size()==count,"duplicate keys never mutate the registry");
    }
    auto full=MakeRegistry(32);E::Element extra;extra.key="test.overflow";extra.label="Overflow";extra.labelKey="test.overflow.name";
    Check(full.Add(extra)==E::Error::Capacity&&full.Size()==32,"element 33 is rejected explicitly");
    auto registry=MakeRegistry(8);const auto before=registry.Size();
    for(unsigned bit:{3u,128u,256u,512u}){
        extra.nativeBit=bit;
        Check(registry.Add(extra)!=E::Error::Ok&&registry.Size()==before,"invalid or occupied native bits are rejected");
    }
    extra.nativeBit=0;
    for(const auto* key:{"", "no_namespace", "test. spaces", "test/control\n", "test.Upper"}){
        extra.key=key;Check(registry.Add(extra)==E::Error::InvalidKey,"keys require bounded lowercase namespaced ASCII");
    }
    extra.key=std::string(65,'a')+".x";Check(registry.Add(extra)==E::Error::InvalidKey,"oversized keys are rejected");
    extra.key="test.external";extra.rgb=0x1000000;
    Check(registry.Add(extra)==E::Error::InvalidDescriptor,"colors are RGB24");
    extra.rgb=0;extra.label="Bad\nlabel";
    Check(registry.Add(extra)==E::Error::InvalidDescriptor,"labels reject control characters");
    extra.label="External";
    Check(registry.Add(extra)==E::Error::Ok&&registry.Find("test.external")->nativeBit==0,
          "the ninth element keeps its key without inventing a native bit");
    Check(!registry.At(32)&&!registry.Find("missing.key"),"missing references remain missing");
    std::printf("ELEMENT_REGISTRY_RT0 %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
#else
int main(){std::puts("FAIL production element registry is absent");return 1;}
#endif
