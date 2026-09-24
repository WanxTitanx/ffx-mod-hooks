#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <fstream>
#include <vector>
static int checks,failures;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",label);}}
template<class T>static T Fn(HMODULE module,const char* name){return reinterpret_cast<T>(GetProcAddress(module,name));}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    HMODULE module=LoadLibraryA(argv[1]);Check(module!=nullptr,"the game's existing FMOD library loads in isolation");if(!module)return 2;
    auto create=Fn<int(WINAPI*)(void**)>(module,"FMOD_System_Create");
    auto output=Fn<int(WINAPI*)(void*,int)>(module,"FMOD_System_SetOutput");
    auto init=Fn<int(WINAPI*)(void*,int,unsigned,void*)>(module,"FMOD_System_Init");
    auto sound=Fn<int(WINAPI*)(void*,const char*,unsigned,void*,void**)>(module,"FMOD_System_CreateSound");
    auto play=Fn<int(WINAPI*)(void*,int,void*,int,void**)>(module,"FMOD_System_PlaySound");
    auto get=Fn<int(WINAPI*)(void*,float*)>(module,"FMOD_Channel_GetFrequency");
    auto set=Fn<int(WINAPI*)(void*,float)>(module,"FMOD_Channel_SetFrequency");
    auto current=Fn<int(WINAPI*)(void*,void**)>(module,"FMOD_Channel_GetCurrentSound");
    auto stop=Fn<int(WINAPI*)(void*)>(module,"FMOD_Channel_Stop");
    auto releaseSound=Fn<int(WINAPI*)(void*)>(module,"FMOD_Sound_Release");
    auto close=Fn<int(WINAPI*)(void*)>(module,"FMOD_System_Close");
    auto release=Fn<int(WINAPI*)(void*)>(module,"FMOD_System_Release");
    Check(create&&output&&init&&sound&&play&&get&&set&&current&&stop&&releaseSound&&close&&release,"required movie-channel APIs exist");if(failures)return 2;
    void* system=nullptr;Check(create(&system)==0&&system,"private audio system initializes");if(!system)return 2;
    Check(output(system,2)==0,"NOSOUND output avoids an audio device or audible playback");
    Check(init(system,8,0,nullptr)==0,"private NOSOUND mixer starts");
    std::vector<unsigned char> wav(44+96000,0);
    auto u16=[&](unsigned at,unsigned value){wav[at]=static_cast<unsigned char>(value);wav[at+1]=static_cast<unsigned char>(value>>8);};
    auto u32=[&](unsigned at,unsigned value){for(unsigned i=0;i<4;++i)wav[at+i]=static_cast<unsigned char>(value>>(8*i));};
    std::memcpy(wav.data(),"RIFF",4);u32(4,static_cast<unsigned>(wav.size()-8));std::memcpy(wav.data()+8,"WAVEfmt ",8);u32(16,16);u16(20,1);u16(22,1);u32(24,48000);u32(28,96000);u16(32,2);u16(34,16);std::memcpy(wav.data()+36,"data",4);u32(40,96000);
    {std::ofstream file("silent-fmv-fixture.wav",std::ios::binary);file.write(reinterpret_cast<const char*>(wav.data()),static_cast<std::streamsize>(wav.size()));}
    void* sample=nullptr;Check(sound(system,"silent-fmv-fixture.wav",2,nullptr,&sample)==0&&sample,"silence fixture loads");
    void* channel=nullptr;Check(play(system,-1,sample,0,&channel)==0&&channel,"private movie-equivalent channel starts");
    if(channel){
        float baseline=0;Check(get(channel,&baseline)==0&&baseline==48000,"native baseline frequency is read back");
        void* identity=nullptr;Check(current(channel,&identity)==0&&identity==sample,"sound identity guards the owned channel");
        for(unsigned factor:{2u,4u,8u}){float actual=0;const float requested=baseline*static_cast<float>(factor);const int result=set(channel,requested);Check(result==0&&get(channel,&actual)==0&&std::fabs(actual-requested)<0.5f,"the real FMOD API accepts and reports the requested movie rate");}
        float restored=0;Check(set(channel,baseline)==0&&get(channel,&restored)==0&&restored==baseline,"native audio frequency restores exactly");stop(channel);
    }
    if(sample)releaseSound(sample);close(system);release(system);DeleteFileA("silent-fmv-fixture.wav");FreeLibrary(module);
    std::printf("FmvAudioApiRt1: %d/%d passed; failures=%d\n",checks-failures,checks,failures);return failures?1:0;
}
