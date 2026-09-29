// Real coordinator, simulated external MinHook operations. No game process.
#include "../hooks/MinHookBatchCoordinator.h"
#include <array>
#include <cstdio>
#include <map>
using namespace FfxHooks::MinHookBatch;
namespace {
unsigned checks=0,failed=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failed;std::fprintf(stderr,"FAIL: %s\n",why);}}
struct Fake {
    unsigned enables=0,disables=0,applies=0,exact=0;
    unsigned failEnable=0,failDisable=0,failApply=0,failExact=0;
    std::map<uintptr_t,bool> queued,active;
    static bool Init(void*){return true;}
    static bool Enable(void* p,uintptr_t a){auto& f=*static_cast<Fake*>(p);if(++f.enables==f.failEnable)return false;f.queued[a]=true;return true;}
    static bool Disable(void* p,uintptr_t a){auto& f=*static_cast<Fake*>(p);if(++f.disables==f.failDisable)return false;f.queued[a]=false;return true;}
    static bool Apply(void* p){auto& f=*static_cast<Fake*>(p);++f.applies;for(auto i:f.queued)f.active[i.first]=i.second;f.queued.clear();return f.applies!=f.failApply;}
    static bool Exact(void* p,uintptr_t a){auto& f=*static_cast<Fake*>(p);if(++f.exact==f.failExact)return false;f.active[a]=false;return true;}
    BatchIo Io(){return {this,Enable,Disable,Apply,Exact};}
    unsigned Calls()const{return enables+disables+applies+exact;}
    bool Inert()const{for(auto i:active)if(i.second)return false;return true;}
};
constexpr std::array<uintptr_t,7> targets={0x1100,0x2200,0x3300,0x4400,0x5500,0x6600,0x7700};
constexpr std::array<Owner,18> owners={Owner::MonsterAiObserver,Owner::FieldScout,Owner::Difficulty,
    Owner::SeymourBattle,Owner::NovaSuperDamage,Owner::ArenaPositions,Owner::EquipmentWorkshop,
    Owner::EquipmentWorkshopUi,Owner::ElementScan,Owner::NulWardRecovery,Owner::GridTeachSave,
    Owner::GridTeachRecovery,Owner::SphereGridRecovery,Owner::SeymourCompatibility,Owner::SeymourOverdrive,Owner::SeymourGearPresentation,Owner::SeymourGearSort,Owner::SeymourMenuList};
constexpr std::array<Owner,4> recovery={Owner::NulWardRecovery,Owner::GridTeachSave,Owner::GridTeachRecovery,Owner::SphereGridRecovery};
void Init(Coordinator& c,Fake& f){Check(EnsureInitialized(&c,{&f,Fake::Init})==InitializationResult::Ready,"initialization");}
BatchReport Enable(Coordinator& c,Fake& f,Owner o){return EnableBatch(&c,f.Io(),o,targets.data(),targets.size());}
BatchReport Stop(Coordinator& c,Fake& f,Owner o){return NeutralizeBatch(&c,f.Io(),o,targets.data(),targets.size());}
void TestOwners(){
    for(auto o:owners){Coordinator c;Fake f;
        Check(Enable(c,f,o).result==BatchResult::NotInitialized,"known owner reaches initialization gate");
        Check(f.Calls()==0,"uninitialized performs no I/O");Init(c,f);
        auto r=Enable(c,f,o);Check(r.result==BatchResult::Applied,"declared owner accepted");
        Check(r.queuedEnableCount==targets.size()&&r.mayHaveRun,"actual complete publication");
        r=Stop(c,f,o);Check(r.result==BatchResult::Neutralized&&r.neutralized&&r.exactDisabled,"complete owner stop");
        Check(f.Inert()&&GetSnapshot(c).state==State::Idle,"stop inert and reusable");
    }
    for(auto o:{Owner::None,static_cast<Owner>(255)}){Coordinator c;Fake f;Init(c,f);
        Check(Enable(c,f,o).result==BatchResult::InvalidArgument,"unknown owner rejected");
        Check(Stop(c,f,o).result==BatchResult::InvalidArgument,"unknown stop rejected");
        Check(f.Calls()==0,"unknown owner performs no I/O");
    }
}
void TestPreparation(){
    for(auto o:recovery)for(unsigned n=1;n<=targets.size();++n){Coordinator c;Fake f;f.failEnable=n;Init(c,f);
        const auto r=Enable(c,f,o);
        Check(r.result==BatchResult::EnableFailedNeutralized,"failed target never reports installed");
        Check(r.primaryFailure==FailureStage::QueueEnable&&r.queuedEnableCount==n-1,"exact failure stage retained");
        Check(r.neutralized&&r.exactDisabled&&f.Inert(),"all failed-preparation targets neutralized");
    }
}
void TestPoison(){
    for(auto o:recovery){Coordinator c;Fake f;f.failApply=1;Init(c,f);const auto r=Enable(c,f,o);
        Check(r.result==BatchResult::Poisoned&&r.mayHaveRun,"partial publication poisons");
        Check(r.neutralized&&f.Inert(),"compensation disables all targets");
        const auto before=f.Calls();Check(Enable(c,f,Owner::ElementScan).result==BatchResult::Poisoned,"other owner cannot reuse poison");
        Check(f.Calls()==before,"poison retry performs no I/O");
    }
}
void TestIncompleteStop(){
    for(auto o:recovery)for(unsigned stage=0;stage<3;++stage){Coordinator c;Fake f;Init(c,f);
        Check(Enable(c,f,o).result==BatchResult::Applied,"stop fixture really installed");
        if(stage==0)f.failDisable=1;
        if(stage==1)f.failApply=2;
        if(stage==2)f.failExact=1;
        const auto r=Stop(c,f,o);
        Check(r.result==BatchResult::Poisoned&&!r.neutralized,"failed stop remains incomplete");
        Check(r.exactDisabled==(stage!=2),"exact disable is not complete neutralization");
        Check(GetSnapshot(c).state==State::Poisoned,"failed stop retains poisoned ownership");
    }
}
}
int main(){
    static_assert(static_cast<unsigned>(Owner::EquipmentWorkshopUi)==8);
    static_assert(static_cast<unsigned>(Owner::ElementScan)==9);
    TestOwners();TestPreparation();TestPoison();TestIncompleteStop();
    std::printf("RecoveryBatchRt0: %u/%u passed (real coordinator, simulated MinHook)\n",checks-failed,checks);
    return failed?1:0;
}
