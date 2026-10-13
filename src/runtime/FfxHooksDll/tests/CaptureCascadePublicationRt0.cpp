#include "../hooks/F7DifficultyCore.h"
#include <cstdio>
int main(){
    using namespace FfxHooks::F7Difficulty;unsigned checks=0,failures=0;
    const auto check=[&](bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL: %s\n",label);}};
    BattleFieldPublication field;
    const auto first=field.BeginRequest();
    check(!field.Matches(first,41,BattleFieldSource::Natural),"uncommitted natural request cannot authorize a patrol");
    check(field.Commit(first,41,BattleFieldSource::Natural)&&field.Matches(first,41,BattleFieldSource::Natural),"committed exact natural request authorizes its own field only");
    check(!field.Matches(first,42,BattleFieldSource::Natural)&&!field.Matches(first,41,BattleFieldSource::Arena),"field and encounter origin are independent authority checks");
    const auto force=field.BeginRequest();
    check(!field.Matches(first,41,BattleFieldSource::Natural),"a forced launch invalidates a prior patrol even before it commits");
    check(field.Commit(force,41,BattleFieldSource::Force)&&!field.Matches(force,41,BattleFieldSource::Natural),"replaying identical encounter resources does not become a natural encounter");
    const auto next=field.BeginRequest();check(field.Commit(next,41,BattleFieldSource::Natural),"a later ordinary encounter can acquire new authority");
    field.Cancel(first);field.Cancel(force);
    check(field.Matches(next,41,BattleFieldSource::Natural),"late cancellation of another request preserves current natural authority");
    check(!field.Commit(first,41,BattleFieldSource::Natural),"late natural completion cannot overwrite a newer request");
    const auto consumed=field.Consume();
    check(consumed.request==next&&!field.Matches(next,41,BattleFieldSource::Natural),"consumed request cannot be borrowed for another battle");
    const auto cancelled=field.BeginRequest();field.Commit(cancelled,41,BattleFieldSource::Natural);field.Cancel(cancelled);
    check(!field.Matches(cancelled,41,BattleFieldSource::Natural),"cancelled natural roll cannot be re-used");
    std::printf("Capture Cascade publication RT0: %u/%u passed\n",checks-failures,checks);return failures?1:0;
}
