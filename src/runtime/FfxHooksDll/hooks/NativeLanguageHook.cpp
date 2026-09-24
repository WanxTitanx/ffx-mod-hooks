// Jarvis-HOOK. Restart-only asset-language selection, adapted from the behavior
// of Kaldaien/UnX faae4359, UnX/language.cpp (GPL-3.0-or-later). No external DLL.
#include "NativeLanguageHook.h"
#include "F8RuntimeCore.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <atomic>

namespace FfxHooks::NativeLanguage {
namespace {
struct OwnedString {std::uintptr_t address=0;std::size_t length=0;char original[64]{},applied[64]{};};
std::array<OwnedString,12> g_owned{};
unsigned g_ownedCount=0;
struct OwnedPage {std::uintptr_t address=0;DWORD protection=0;};
std::array<OwnedPage,12> g_pages{};
unsigned g_pageCount=0;
DWORD g_pageSize=0;
std::atomic<bool> g_stop{true};
std::atomic<Result> g_status{Result::Default};
std::atomic<unsigned> g_applied{0};
SRWLOCK g_lock=SRWLOCK_INIT;
// Allocate/open all handles before suspending anything. While frozen, the
// transaction only compares fixed buffers, changes page protection and copies
// bytes; it performs no allocation, config access, logging, waits or User32 work.
struct FrozenThreads {
    std::array<HANDLE,256> handles{};unsigned count=0,suspended=0;
    ~FrozenThreads(){for(unsigned i=0;i<suspended;++i)ResumeThread(handles[i]);for(unsigned i=0;i<count;++i)CloseHandle(handles[i]);}
    bool Freeze(){
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
        if(snapshot==INVALID_HANDLE_VALUE)return false;
        THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
        bool valid=true;
        if(Thread32First(snapshot,&entry))do {
            if(entry.th32OwnerProcessID!=GetCurrentProcessId() || entry.th32ThreadID==GetCurrentThreadId())continue;
            if(count==handles.size()){valid=false;break;}
            HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|SYNCHRONIZE,FALSE,entry.th32ThreadID);
            if(!thread){if(GetLastError()==ERROR_INVALID_PARAMETER)continue;valid=false;break;}
            if(WaitForSingleObject(thread,0)==WAIT_OBJECT_0){CloseHandle(thread);continue;}
            handles[count++]=thread;
        } while(Thread32Next(snapshot,&entry));
        else valid=false;
        CloseHandle(snapshot);if(!valid)return false;
        for(;suspended<count;++suspended)if(SuspendThread(handles[suspended])==static_cast<DWORD>(-1))return false;
        return true;
    }
};
bool Profile(std::uintptr_t base){
    __try {
        if(!base)return false;
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>0x1000)return false;
        const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
        return pe->Signature==IMAGE_NT_SIGNATURE && F8Runtime::IsSupportedExecutable({pe->FileHeader.Machine,pe->OptionalHeader.Magic,pe->FileHeader.TimeDateStamp,pe->OptionalHeader.SizeOfImage});
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Matches(const OwnedString& value,bool applied){
    __try {return std::memcmp(reinterpret_cast<const void*>(value.address),applied?value.applied:value.original,value.length)==0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool WriteBatch(unsigned count,bool restore){
    unsigned writable=0;DWORD ignored=0;
    for(;writable<g_pageCount;++writable)if(!VirtualProtect(reinterpret_cast<void*>(g_pages[writable].address),g_pageSize,PAGE_READWRITE,&ignored))break;
    bool ok=writable==g_pageCount;
    if(ok)for(unsigned i=0;i<count;++i)std::memcpy(reinterpret_cast<void*>(g_owned[i].address),restore?g_owned[i].original:g_owned[i].applied,g_owned[i].length);
    while(writable){--writable;if(!VirtualProtect(reinterpret_cast<void*>(g_pages[writable].address),g_pageSize,g_pages[writable].protection,&ignored))ok=false;}
    return ok;
}
}
bool Start(std::uintptr_t base,Settings settings){
    if(!g_stop || g_ownedCount || g_pageCount)return false;
    const auto plan=BuildPlan(settings);
    if(!plan.valid || !Profile(base)){g_status=Result::Unsupported;return false;}
    if(!plan.count){g_status=Result::Default;g_applied=0;return true;}
    if(GetModuleHandleW(L"unx.dll")){g_status=Result::Conflict;return false;}
    AcquireSRWLockExclusive(&g_lock);
    bool success=false;
    do {
        SYSTEM_INFO info{};GetSystemInfo(&info);g_pageSize=info.dwPageSize;
        bool prepared=g_pageSize && (g_pageSize&(g_pageSize-1))==0;
        if(!prepared){g_status=Result::Unsupported;break;}
        for(unsigned i=0;i<plan.count;++i){
            const auto& p=plan.patches[i];auto& value=g_owned[i];value={};value.address=base+p.rva;value.length=std::strlen(p.original)+1;
            if(value.length>sizeof(value.original)){prepared=false;break;}
            std::memcpy(value.original,p.original,value.length);std::memcpy(value.applied,p.replacement,std::strlen(p.replacement)+1);
            const auto page=value.address&~static_cast<std::uintptr_t>(g_pageSize-1);
            if(page!=((value.address+value.length-1)&~static_cast<std::uintptr_t>(g_pageSize-1))){prepared=false;break;}
            bool found=false;for(unsigned n=0;n<g_pageCount;++n)if(g_pages[n].address==page)found=true;
            if(!found){
                MEMORY_BASIC_INFORMATION memory{};
                if(g_pageCount>=g_pages.size() || !VirtualQuery(reinterpret_cast<void*>(page),&memory,sizeof(memory)) || memory.State!=MEM_COMMIT || (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS))){prepared=false;break;}
                g_pages[g_pageCount++]={page,memory.Protect};
            }
        }
        if(!prepared){g_pageCount=0;g_status=Result::Unsupported;break;}
        bool matches=true;for(unsigned i=0;i<plan.count;++i)matches=matches&&Matches(g_owned[i],false);
        if(!matches){g_pageCount=0;g_status=Result::Conflict;break;}
        FrozenThreads threads;if(!threads.Freeze()){g_pageCount=0;g_status=Result::ThreadUnavailable;break;}
        for(unsigned i=0;i<plan.count;++i)if(!Matches(g_owned[i],false))matches=false;
        for(unsigned i=0;i<g_pageCount;++i){MEMORY_BASIC_INFORMATION memory{};if(!VirtualQuery(reinterpret_cast<void*>(g_pages[i].address),&memory,sizeof(memory)) || memory.Protect!=g_pages[i].protection)matches=false;}
        if(!matches){g_pageCount=0;g_status=Result::Conflict;break;}
        const bool written=WriteBatch(plan.count,false);
        bool allOriginal=true,allApplied=true;
        for(unsigned i=0;i<plan.count;++i){allOriginal=allOriginal&&Matches(g_owned[i],false);allApplied=allApplied&&Matches(g_owned[i],true);}
        if(allApplied){
            g_ownedCount=plan.count;g_stop=false;
            g_applied=static_cast<unsigned>(settings.voice)|(static_cast<unsigned>(settings.sfx)<<8)|(static_cast<unsigned>(settings.video)<<16);
            g_status=written?Result::Applied:Result::RestorePending;success=written;
        } else {g_ownedCount=allOriginal?0:plan.count;g_stop=g_ownedCount==0;g_status=Result::ProtectFailed;}
    } while(false);
    ReleaseSRWLockExclusive(&g_lock);return success;
}
void RequestStop(){g_stop=true;}
void Stop(){
    RequestStop();AcquireSRWLockExclusive(&g_lock);
    if(g_ownedCount || g_pageCount){
        bool matches=true;for(unsigned i=0;i<g_ownedCount;++i)matches=matches&&Matches(g_owned[i],true);
        const bool pending=g_status==Result::RestorePending || g_status==Result::ProtectFailed;
        for(unsigned i=0;i<g_pageCount;++i){MEMORY_BASIC_INFORMATION memory{};if(!VirtualQuery(reinterpret_cast<void*>(g_pages[i].address),&memory,sizeof(memory)) || (memory.Protect!=g_pages[i].protection && !(pending && memory.Protect==PAGE_READWRITE)))matches=false;}
        if(!matches)g_status=Result::Conflict;
        else {
            FrozenThreads threads;
            if(!threads.Freeze())g_status=Result::RestorePending;
            else {
                bool unchanged=true;for(unsigned i=0;i<g_ownedCount;++i)unchanged=unchanged&&Matches(g_owned[i],true);
                if(unchanged && WriteBatch(g_ownedCount,true)){g_ownedCount=0;g_pageCount=0;g_applied=0;g_status=Result::Default;}
                else g_status=unchanged?Result::RestorePending:Result::Conflict;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}
Result Status(){return g_status.load();}
Settings Applied(){const unsigned value=g_applied.load();return {static_cast<Choice>(value&255),static_cast<Choice>((value>>8)&255),static_cast<Choice>((value>>16)&255)};}
const char* StatusText(){switch(Status()){
case Result::Default:return "Game defaults";case Result::Applied:return "Applied at startup";
case Result::Unsupported:return "Unsupported executable";case Result::Conflict:return "Another owner changed language references";
case Result::ThreadUnavailable:return "Startup transaction unavailable";case Result::ProtectFailed:return "Unable to apply language references";
case Result::RestorePending:return "Restore pending";default:return "Unavailable";
}}
}
