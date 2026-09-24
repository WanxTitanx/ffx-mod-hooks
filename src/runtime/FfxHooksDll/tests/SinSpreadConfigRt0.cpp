#include "../hooks/SinRamConfigCore.h"
#include "../hooks/SinSpreadCore.h"
#include <cstring>
#include <cstdio>
using namespace FfxHooks;
static int checks,failures;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
static auto Parse(const char* text,SinRam::Config& config){return SinRamConfig::ParseDocument(text,std::strlen(text),&config);}
int main(){
    SinRam::Config config{};
    Check(Parse("{\"sinRam\":{\"enabled\":true,\"distribution\":80,\"seed\":4294967295}}",config).code==SinRamConfig::Code::Ok &&
        config.seeded && config.distribution==80 && config.seed==0xFFFFFFFFu,"modern seed/distribution config accepts full unsigned seed");
    char output[128]{};auto encoded=SinRamConfig::SerializeValue(config,output,sizeof(output));
    Check(encoded.code==SinRamConfig::Code::Ok && !std::strstr(output,"threatLevel") && std::strstr(output,"4294967295"),"modern canonical config removes manual threat setting");
    Check(Parse("{\"sinRam\":{\"enabled\":true,\"threatLevel\":2}}",config).code==SinRamConfig::Code::Ok && !config.seeded && config.threatLevel==2,
        "old files still import through the legacy codec");
    Check(Parse("{\"sinRam\":{\"enabled\":true,\"distribution\":100,\"seed\":1}}",config).code==SinRamConfig::Code::OutOfRange && !config.enabled,"100 percent is rejected by design");
    Check(Parse("{\"sinRam\":{\"seed\":-1}}",config).code==SinRamConfig::Code::OutOfRange,"negative seed rejected");
    Check(Parse("{\"sinRam\":{\"seed\":4294967296}}",config).code==SinRamConfig::Code::OutOfRange,"overflow seed rejected");
    Check(Parse("{\"sinRam\":{\"seed\":1,\"Seed\":2}}",config).code==SinRamConfig::Code::DuplicateKey,"case-folded duplicate seed rejected");
    Check(Parse("{\"other\":{\"threatLevel\":999},\"sinRam\":{\"enabled\":true,\"distribution\":0,\"seed\":0}}",config).code==SinRamConfig::Code::Ok && config.seeded && config.seed==0,"zero seed is valid without coupling unrelated settings");
    SinRam::RuntimeRequest request{};request.config=config;request.areaVisit=7;
    request.encounterToken=310u<<16;request.origin=SinRam::EncounterOrigin::Natural;request.transitionCallerRva=0x00471CEFu;
    request.transitionRequestId=request.actorRequestId=5;request.transitionGeneration=3;
    SinRam::DifficultyValues values{};values.maxHp=100;values.overkill=200;values.stats.fill(20);
    const auto assignment=SinSpread::BuildAssignment(310,config.seed,7,SinSpread::Distribution::Random,true);
    for(unsigned i=0;i<assignment.count;++i){
        const auto& monster=assignment.monsters[i];
        const auto plan=SinRam::BuildRuntimeStructuralScalePlan(request,3,310,SinSpread::NativeMonsterId(monster.monster),values);
        Check(plan.admitted==(monster.curse!=0),"only selected area monsters receive runtime scaling");
        if(plan.admitted)Check(plan.writeback.maxHp==100+10*monster.threat,"runtime threat comes from the assigned curated curse");
    }
    std::printf("SinSpreadConfigRt0: %d/%d checks passed; failures=%d\n",checks-failures,checks,failures);return failures?1:0;
}
