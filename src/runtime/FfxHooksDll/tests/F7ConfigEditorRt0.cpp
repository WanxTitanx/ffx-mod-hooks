#include <cstdio>
#include <cstring>
#if __has_include("../hooks/F7ConfigEditor.h")
#include "../hooks/F7ConfigEditor.h"
using namespace FfxHooks;
namespace E=FfxHooks::F7Editor;
static int checks=0,failures=0;
static void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL: %s\n",label);}}
int main(){
    auto initial=F7_GetConfigSnapshot();
    auto music=initial.config.music;music.fadeFrames=35;music.randomizer=true;music.playlistCount=2;
    music.playlist[0]=1;music.playlist[1]=181;
    Check(E::CommitMusic(initial.config.music,music)==E::Result::Applied,"one validated music draft is published atomically");
    auto current=F7_GetConfigSnapshot();
    Check(current.config.music.playlistCount==2&&current.config.music.playlist[1]==181&&current.config.music.fadeFrames==35,"playlist and fade travel together");
    Check(E::CommitMusic(initial.config.music,music)==E::Result::Conflict,"a stale editor cannot overwrite another music change");
    for(int bad:{-1,9,2147483647}){auto invalid=music;invalid.playlistCount=bad;Check(E::CommitMusic(music,invalid)==E::Result::Invalid,"playlist count is bounded before indexing");}
    for(int bad:{-1,0,182}){auto invalid=music;invalid.playlist[0]=bad;Check(E::CommitMusic(music,invalid)==E::Result::Invalid,"playlist rejects invalid and unsafe zero tracks");}
    auto zero=music;zero.lockTrack=0;Check(E::CommitMusic(music,zero)==E::Result::Invalid,"zero cannot bypass the established no-track sentinel");
    char label[48]{};
    Check(E::FormatMusicNumber(E::MusicRows::Fade,35,label,sizeof(label))&&std::strcmp(label,"35 frames")==0,"fade is a duration, not a track ID");
    Check(E::FormatMusicNumber(E::MusicRows::Count,2,label,sizeof(label))&&std::strcmp(label,"2 / 8")==0,"playlist count has an explicit capacity");
    Check(E::PlaylistTrack(music,3)==181,"randomizer indexes only the validated active prefix");
    auto fade=music;fade.fadeFrames=0;Check(E::FadeFrames(fade)==0,"explicit zero fade is not replaced by a hidden 90-frame duration");
    fade.fadeFrames=35;Check(E::FadeFrames(fade)==35,"configured fade reaches the battle transition unchanged");
    fade.fadeFrames=900;Check(E::FadeFrames(fade)==600,"legacy malformed fade is bounded at runtime");
    auto corrupt=music;corrupt.playlistCount=999;Check(E::PlaylistTrack(corrupt,4)==-1,"invalid runtime playlist is never read out of bounds");
    auto expected=current.difficulty;auto draft=expected;draft.global.mpMul=2200;draft.global.overkillMul=1700;
    auto preset=draft.global;preset.enabled=true;preset.hpMul=2500;preset.statusResist[24]=100;
    Check(E::PutArea(draft,123,preset,true)==E::Result::Applied&&draft.areaCount==1,"area draft creates one bounded rule");
    Check(E::PutArea(draft,123,preset,false)==E::Result::Applied&&draft.areaCount==1&&!draft.areas[0].enabled,"editing an existing field never creates a duplicate");
    Check(draft.global.mpMul==2200&&draft.areas[0].preset.statusResist[24]==100,"area editing preserves global and hidden preset fields");
    draft.byArea=true;
    Check(E::CommitDifficulty(expected,draft)==E::Result::Applied,"difficulty publishes validated canonical and legacy snapshots together");
    current=F7_GetConfigSnapshot();
    Check(current.config.areaCount==1&&current.config.areas[0].fieldRow==123&&current.config.diffByArea,"legacy UI and runtime agree on the area rules");
    Check(E::EqualMusic(current.config.music,music),"difficulty edits do not overwrite music");
    Check(E::CommitDifficulty(expected,draft)==E::Result::Conflict,"stale difficulty changes are rejected");
    for(int i=0;i<15;++i)Check(E::PutArea(draft,200+i,preset,true)==E::Result::Applied,"all sixteen rule slots are available");
    Check(E::PutArea(draft,300,preset,true)==E::Result::Full&&draft.areaCount==16,"full area table fails without replacing a rule");
    Check(E::EraseArea(draft,0)&&draft.areaCount==15&&draft.areas[0].fieldRow==200,"delete compacts rules and clears the unused tail");
    Check(!E::EraseArea(draft,16)&&E::PutArea(draft,-1,preset,true)==E::Result::Invalid,"invalid field and row cannot mutate the draft");
    std::printf("F7 config editor RT0: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#else
int main(){std::puts("FAIL: F7 atomic playlist and area-rule editor is missing");return 1;}
#endif
