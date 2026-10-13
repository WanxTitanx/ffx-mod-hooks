#include "ArcanaFieldPresentation.h"
#include "ArcanaFieldDialogCore.h"
#include "ArcanaFieldEvidence.generated.h"
#include "NativeUiHookSupport.h"
#include "../shared/ffx_addresses.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace FfxHooks::Arcana::FieldPresentation {
namespace {
static_assert(sizeof(void*)==4,"The native field message ABI is x86 only");
constexpr unsigned slot=7,eventBytes=44,contextBytes=568,contextCount=7;
using Entry=Evidence::Entry;
using SyncFn=int(__cdecl*)();
std::uintptr_t base=0;
void* originals[1]{};
Callbacks bindings{};
std::atomic<bool> installed{false},stopping{false};
std::atomic<Code> status{Code::Off};
bool attempted=false;
DWORD ownerThread=0;
thread_local bool inside=false;
FieldDialog::Controller controller;
void(*logger)(const char*)=nullptr;
std::uint64_t imageGeneration=0;
std::atomic<std::uint64_t> observedGeneration{0};

struct Snapshot {
    std::uint16_t scene=0,held=0,pressed=0;
    std::uint32_t activeScene=0,context=0,character=0,transition=0;
    std::int32_t menu=-1;
    unsigned char bank=255,battle=1;
    std::array<unsigned char,32> contextHead{};
    std::array<std::array<unsigned char,eventBytes>,8> events{};
    bool focused=false,pointers=false;
};
struct Lease {
    bool valid=false;
    std::uint16_t scene=0;
    std::uint32_t context=0;
    std::uint64_t generation=0;
    Presentation::View view{};
    std::uintptr_t text=0;
    std::array<unsigned char,eventBytes> event{};
    std::array<unsigned char,28> manager{};
    std::array<unsigned char,304> window{};
    std::array<unsigned char,312> response{};
    std::array<unsigned char,124> frame{};
    std::array<unsigned char,256> parameters{};
    std::array<std::uint16_t,8> selections{};
} lease;
// Native windows retain these pointers until their closing animation finishes.
// The installed hook pins this module; neither a load nor Stop frees a buffer.
FieldDialog::EncodedText nameText,messageText;

template<class T> bool Read(std::uintptr_t address,T& value) noexcept {
    return NativeUiSupport::Copy(&value,reinterpret_cast<const void*>(address),sizeof(value));
}
template<class T> bool Write(std::uintptr_t address,const T& value) noexcept {
    return NativeUiSupport::Copy(reinterpret_cast<void*>(address),&value,sizeof(value));
}
template<class T> T Function(Entry entry) noexcept {return reinterpret_cast<T>(base+Evidence::Rva(entry));}
std::uint16_t Word(const unsigned char* p) noexcept {return std::uint16_t(p[0])|(std::uint16_t(p[1])<<8);}
std::uint32_t Dword(const unsigned char* p) noexcept {
    return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
}
bool Focused() noexcept {
    if(bindings.focused)return bindings.focused();
    DWORD process=0;const auto window=GetForegroundWindow();
    return window&&GetWindowThreadProcessId(window,&process)&&process==GetCurrentProcessId();
}
bool Pointer(std::uint32_t registry,unsigned index,std::uint32_t expected) noexcept {
    std::uint32_t value=0;return Read(base+registry+index*4,value)&&value==base+expected;
}
bool PrimaryPointers() noexcept {
    for(unsigned i=0;i<8;++i){
        if(!Pointer(Evidence::kManagers,i,Evidence::kManagerPrimary+i*28)||
           !Pointer(Evidence::kWindows,i,Evidence::kWindowPrimary+i*304)||
           !Pointer(Evidence::kResponses,i,Evidence::kResponsePrimary+i*312)||
           !Pointer(Evidence::kFrames,i,Evidence::kFramePrimary+i*124))return false;
    }
    for(unsigned i=0;i<32;++i)if(!Pointer(Evidence::kParameters,slot*32+i,Evidence::kParameterPrimary+slot*256+i*8))return false;
    return true;
}
bool Input(std::uint16_t& held,std::uint16_t& pressed) noexcept {
    std::uint16_t rawHeld=0,rawPressed=0;std::array<unsigned char,16> map{};
    if(!Read(base+Evidence::kHeld,rawHeld)||!Read(base+Evidence::kPressed,rawPressed)||!Read(base+Evidence::kInputMap,map))return false;
    for(auto bit:map)if(bit>=16)return false;
    __try {
        using Normalize=std::uint16_t(__cdecl*)(int,int,std::uint16_t);
        held=Function<Normalize>(Entry::NormalizeInput)(0,0,rawHeld);
        pressed=Function<Normalize>(Entry::NormalizeInput)(0,0,rawPressed);return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool CaptureNative(Snapshot& out) noexcept {
    if(!Read(base+RVA_FFX_SCENE_STATE_OBJECT,out.scene)||
       !Read(base+RVA_FFX_FASTLOAD_ACTIVE_SCENE,out.activeScene)||
       !Read(base+RVA_FFX_CURRENT_MENU_SCREEN_ID,out.menu)||
       !Read(base+RVA_FFX_SCENE_TRANSITION_PENDING,out.transition)||
       !Read(base+RVA_FFX_BATTLE_ACTIVE_FLAG,out.battle)||
       !Read(base+Evidence::kBank,out.bank)||!Read(base+Evidence::kContextPointer,out.context))return false;
    const auto first=base+Evidence::kContexts;
    if(out.context<first||out.context>=first+contextBytes*contextCount||(out.context-first)%contextBytes)return false;
    if(!Read(out.context,out.contextHead)||!Read(base+RVA_FFX_CONTROLLED_CHR_INSTANCE_PTR,out.character))return false;
    out.focused=Focused();
    if(out.bank!=0)return true;
    if(!Read(base+Evidence::kEvents,out.events)||!Input(out.held,out.pressed))return false;
    out.pointers=PrimaryPointers();return true;
}
bool OtherWindows(const Snapshot& sample) noexcept {
    for(unsigned i=0;i<8;++i)if(i!=slot){const auto value=Word(sample.events[i].data()+20);if(value&&value!=4)return true;}
    return false;
}
bool StableField(const Snapshot& sample) noexcept {
    return sample.bank==0&&sample.pointers&&!sample.battle&&!sample.transition&&sample.menu==-1&&
        sample.activeScene==sample.scene&&sample.scene!=23&&(sample.contextHead[0]&1)&&
        (sample.contextHead[1]&8)&&!(sample.contextHead[3]&0x84)&&Word(sample.contextHead.data()+26)==0;
}
bool CanOpen(const Snapshot& sample) noexcept {
    const auto own=Word(sample.events[slot].data()+20);
    return StableField(sample)&&sample.focused&&sample.character&&(sample.contextHead[0]&2)&&
        !(sample.contextHead[1]&4)&&!OtherWindows(sample)&&(own==0||own==4);
}
bool Owns(const Snapshot& sample,bool opening=false) noexcept {
    if(!lease.valid||sample.context!=lease.context||sample.scene!=lease.scene||!sample.pointers)return false;
    const auto* event=sample.events[slot].data();
    if(Dword(event+8)!=lease.text||Dword(event+12)!=lease.text)return false;
    if(!opening&&lease.view.kind==Presentation::Kind::Notice){
        std::uint32_t value=0;
        if(!Read(base+Evidence::kParameterPrimary+slot*256+4,value)||value!=reinterpret_cast<std::uintptr_t>(nameText.bytes.data()))return false;
    }
    return true;
}
void Images(bool show) noexcept {
    if(!bindings.images)return;
    NativeUi::Images image;image.generation=++imageGeneration;
    image.scene=lease.scene;image.session=lease.generation;
    if(show&&lease.view.kind==Presentation::Kind::Introduction&&lease.view.page==0){
        image.count=1;image.quads[0]={lease.view.card,.77f,.31f,.14f,.38f};
    }
    bindings.images(image);
}
bool Restore(bool rollback) noexcept {
    Snapshot now;if(!CaptureNative(now)||now.bank!=0||!Owns(now,rollback))return false;
    // The native field publisher derives input capture from the live window's
    // flag 2. We never acquire/restore a shared player-control bit ourselves.
    const bool ok=Write(base+Evidence::kEvents+slot*eventBytes,lease.event)&&
        Write(base+Evidence::kManagerPrimary+slot*28,lease.manager)&&
        Write(base+Evidence::kWindowPrimary+slot*304,lease.window)&&
        Write(base+Evidence::kResponsePrimary+slot*312,lease.response)&&
        Write(base+Evidence::kFramePrimary+slot*124,lease.frame)&&
        Write(base+Evidence::kParameterPrimary+slot*256,lease.parameters);
    if(rollback&&!OtherWindows(now))for(unsigned i=0;i<8;++i)Write(base+Evidence::kManagerPrimary+i*28+6,lease.selections[i]);
    lease.valid=false;Images(false);
    // Restore ordinary native capture state on the same field thread.
    if(ok&&originals[0])reinterpret_cast<SyncFn>(originals[0])();
    return ok;
}
bool Wrap(const char* input,char* output,std::size_t capacity,unsigned columns) noexcept {
    return FieldDialog::Wrap(input,output,capacity,columns);
}
bool Format(const Presentation::View& view,const char* objective=nullptr) noexcept {
    char wrapped[2048]{};
    if(view.kind==Presentation::Kind::System){
        char literal[1600]{};
        if(!bindings.supplemental.format||!bindings.supplemental.format(view,literal,sizeof(literal))||
           !Wrap(literal,wrapped,sizeof(wrapped),34))return false;
        return bindings.supplemental.encode?
            bindings.supplemental.encode(wrapped,messageText.bytes.data(),messageText.bytes.size(),messageText.size):
            FieldDialog::Encode(wrapped,messageText);
    }
    const auto* card=FindCard(view.card);if(!card)return false;
    if(view.kind==Presentation::Kind::Notice)
        return Wrap(card->name,wrapped,sizeof(wrapped),24)&&FieldDialog::Encode(wrapped,nameText);
    if(view.kind==Presentation::Kind::Objective){
        char literal[1400]{};
        if(!objective)return false;
        const int length=std::snprintf(literal,sizeof(literal),"Tarot exploration\n\n%s\n\nConfirm: continue",objective);
        return length>0&&static_cast<std::size_t>(length)<sizeof(literal)&&
            Wrap(literal,wrapped,sizeof(wrapped),34)&&FieldDialog::Encode(wrapped,messageText);
    }
    const auto* page=Presentation::Guide(view.page);if(!page)return false;
    char literal[1600]{};
    const int length=view.page==0?
        std::snprintf(literal,sizeof(literal),"%s (1/12)\n\n%s\n\n%s\n%s\n\n%s\n\nConfirm: next   Left: previous   Cancel: skip",
            view.legacy?"Your Tarot collection":page->title,
            view.legacy?"You already own Tarot cards. Learn how to equip them and build your collection.":"Your first Tarot card is ready to use.",
            card->name,card->description,page->text):
        std::snprintf(literal,sizeof(literal),"%s (%u/12)\n\n%s\n\n%s",
            page->title,unsigned(view.page)+1,page->text,
            view.page+1==Presentation::kGuidePageCount?"Confirm: finish   Left: previous   Cancel: skip":"Confirm: next   Left: previous   Cancel: skip");
    return length>0&&static_cast<std::size_t>(length)<sizeof(literal)&&
        Wrap(literal,wrapped,sizeof(wrapped),48)&&FieldDialog::Encode(wrapped,messageText);
}
bool SaveLease(const Snapshot& sample,const Presentation::View& view,std::uint64_t generation) noexcept {
    lease={};lease.scene=sample.scene;lease.context=sample.context;lease.view=view;lease.generation=generation;
    if(!Read(base+Evidence::kEvents+slot*eventBytes,lease.event)||
       !Read(base+Evidence::kManagerPrimary+slot*28,lease.manager)||
       !Read(base+Evidence::kWindowPrimary+slot*304,lease.window)||
       !Read(base+Evidence::kResponsePrimary+slot*312,lease.response)||
       !Read(base+Evidence::kFramePrimary+slot*124,lease.frame)||
       !Read(base+Evidence::kParameterPrimary+slot*256,lease.parameters))return false;
    for(unsigned i=0;i<8;++i)if(!Read(base+Evidence::kManagerPrimary+i*28+6,lease.selections[i]))return false;
    return true;
}
bool OpenNative() noexcept {
    const char* stage="template";
    __try {
        const auto event=base+Evidence::kEvents+slot*eventBytes;
        using Param=void(__cdecl*)(int,int,int);
        if(lease.view.kind==Presentation::Kind::Notice){
            // Exact singular received-item template and native variable types.
            // No native item ID, inventory grant or treasure-state bit is used.
            lease.text=Function<std::uintptr_t(__cdecl*)(int)>(Entry::Template)(16408);
        }else lease.text=reinterpret_cast<std::uintptr_t>(messageText.bytes.data());
        if(!lease.text)return false;
        stage="event";
        *reinterpret_cast<std::int16_t*>(event)=256;*reinterpret_cast<std::int16_t*>(event+2)=208;
        *reinterpret_cast<std::uintptr_t*>(event+8)=lease.text;*reinterpret_cast<std::uintptr_t*>(event+12)=lease.text;
        *reinterpret_cast<std::uint16_t*>(event+20)=1;*reinterpret_cast<std::uint16_t*>(event+22)=0;
        *reinterpret_cast<std::uint32_t*>(event+24)=0;*reinterpret_cast<std::uint32_t*>(event+40)=0;
        *reinterpret_cast<unsigned char*>(event+28)=4;*reinterpret_cast<unsigned char*>(event+29)=0x12;
        *reinterpret_cast<unsigned char*>(event+32)=0;
        lease.valid=true;
        if(lease.view.kind==Presentation::Kind::Notice){
            stage="parameters";
            Function<Param>(Entry::ParamKind)(slot,0,0);Function<Param>(Entry::ParamKind)(slot,1,1);
            Function<Param>(Entry::ParamValue)(slot,0,reinterpret_cast<int>(nameText.bytes.data()));
            Function<Param>(Entry::ParamValue)(slot,1,1);
        }
        stage="slot initialization";Function<void(__cdecl*)(int)>(Entry::InitSlot)(slot);
        stage="frame colors";
        Function<void(__cdecl*)(int,char,char,char,char)>(Entry::SetColors)(slot,
            static_cast<char>(lease.event[18]),static_cast<char>(lease.event[17]),
            static_cast<char>(lease.event[16]),static_cast<char>(lease.event[19]));
        stage="frame style";Function<void(__cdecl*)(int,std::int16_t,int)>(Entry::SetStyle)(slot,0,0);
        stage="native text";Function<void(__cdecl*)(int,std::uintptr_t,std::uintptr_t)>(Entry::SetText)(slot,lease.text,lease.text);
        stage="native layout";
        // Use the same native content measurement as ordinary field notices.
        // A fixed 480x380 rectangle nearly fills the 512x412 logical canvas,
        // even when the actual text uses only a small fraction of that space.
        Function<void(__cdecl*)(int,int,int,int)>(Entry::Layout)(slot,4,256,208);
        // Historical symbol calls this "width"; the native caller passes the
        // slot index. Preserve that observed channel-link contract exactly.
        Function<void(__cdecl*)(int,std::int16_t)>(Entry::SetWidth)(slot,slot);
        Function<void(__cdecl*)(int)>(Entry::SelectSlot)(slot);
        stage="native activation";Function<void(__cdecl*)(int)>(Entry::Open)(slot);
        stage="native control publication";
        reinterpret_cast<SyncFn>(originals[0])();
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){
        if(logger){char message[144]{};std::snprintf(message,sizeof(message),"[ffx-hooks] Arcana field: native open failed during %s; receipt remains pending\n",stage);logger(message);}
        return false;
    }
}
bool CloseNative() noexcept {
    __try {Function<void(__cdecl*)(int)>(Entry::Close)(slot);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
FieldPrompt World(const Snapshot& sample,std::uint64_t generation,bool available) noexcept {
    FieldPrompt prompt;
    if(!bindings.world)return prompt;
    FieldContext context;context.scene=sample.scene;context.generation=generation;
    context.held=sample.held;context.pressed=sample.pressed;
    float position[3]{};
    if(available&&sample.character&&NativeUiSupport::Copy(position,reinterpret_cast<const void*>(sample.character+0xC),sizeof(position))&&
       std::isfinite(position[0])&&std::isfinite(position[1])&&std::isfinite(position[2])){
        context.x=position[0];context.y=position[1];context.z=position[2];context.available=true;
    }
    bindings.world(context,prompt);return prompt;
}
void Pump() noexcept {
    if(!installed.load())return;
    if(ownerThread&&ownerThread!=GetCurrentThreadId())return;
    Snapshot sample;if(!CaptureNative(sample))return;
    if(bindings.maintenance&&!stopping.load())bindings.maintenance();
    Presentation::State pending;std::uint64_t primaryGeneration=0,supplementalGeneration=0;
    bool primaryCaptured=bindings.capture&&bindings.capture(pending,primaryGeneration);
    Presentation::View supplementalView{};
    bool supplementalCaptured=bindings.supplemental.capture&&bindings.supplemental.capture(
        sample.scene,CanOpen(sample),supplementalView,supplementalGeneration);
    bool supplemental=controller.Active()?lease.view.kind==Presentation::Kind::System:
        supplementalCaptured&&(!primaryCaptured||
            (Presentation::Current(pending).kind==Presentation::Kind::None&&supplementalView.kind==Presentation::Kind::System));
    bool captured=supplemental?supplementalCaptured:primaryCaptured;
    std::uint64_t generation=supplemental?supplementalGeneration:primaryGeneration;
    if(captured)observedGeneration=generation;
    if(captured&&!ownerThread)ownerThread=GetCurrentThreadId();
    if(controller.Active()){
        World(sample,primaryGeneration,false);
        FieldDialog::Sample input;input.generation=captured?generation:lease.generation;input.scene=sample.scene;
        input.suspended=sample.bank!=0||sample.menu!=-1||sample.battle||sample.transition;
        input.owned=input.suspended||Owns(sample);input.focused=sample.focused;
        input.available=captured&&StableField(sample);input.foreign=sample.bank==0&&OtherWindows(sample);
        input.stopping=stopping.load();input.confirm=(sample.held&0x20)!=0;input.back=(sample.held&0x40)!=0;
        input.previous=(sample.held&0x8000)!=0;
        if(!input.suspended){
            input.window=Word(sample.events[slot].data()+20);
            if(sample.pointers)Read(base+Evidence::kWindowPrimary+slot*304+46,input.text);
        }
        const auto decision=controller.Poll(input);
        Images(!input.suspended&&input.owned&&input.focused&&!input.stopping&&
               decision.action==FieldDialog::Action::None&&input.window!=3);
        if(decision.action==FieldDialog::Action::Close){
            // Native actor-script processing clears its transient confirm flag
            // each tick. This adapter only closes its own window; it never
            // edits the player's control flags or replays a native input.
            if(!CloseNative())status=Code::NativeFault;
        }else if(decision.action==FieldDialog::Action::Release){
            if(Restore(false)&&decision.view.kind!=Presentation::Kind::Objective&&decision.navigation!=Presentation::Navigation::None&&captured&&!stopping.load()){
                if(decision.view.kind==Presentation::Kind::System){
                    if(bindings.supplemental.acknowledge)bindings.supplemental.acknowledge(decision.generation,decision.view,decision.navigation);
                }else if(bindings.acknowledge)bindings.acknowledge(decision.generation,decision.view,decision.navigation);
            }
            status=stopping.load()?Code::Stopped:Code::Ready;
        }else if(decision.action==FieldDialog::Action::Abandon){lease.valid=false;Images(false);status=Code::Waiting;}
        return;
    }
    if(stopping.load()){World(sample,generation,false);status=Code::Stopped;return;}
    if(!captured){World(sample,generation,false);return;}
    auto view=supplemental?supplementalView:Presentation::Current(pending);
    const auto prompt=World(sample,primaryGeneration,primaryCaptured&&view.kind==Presentation::Kind::None&&CanOpen(sample));
    // A world interaction can have produced an award in this same field tick.
    if(!supplemental&&bindings.capture&&bindings.capture(pending,generation))view=Presentation::Current(pending);
    if(view.kind==Presentation::Kind::None&&prompt.text)view={Presentation::Kind::Objective,prompt.card,0,false};
    if(view.kind==Presentation::Kind::None){status=Code::Ready;return;}
    if(!CanOpen(sample)){status=Code::Waiting;return;}
    if(!Format(view,prompt.text)||!SaveLease(sample,view,generation)){status=Code::NativeFault;return;}
    if(!OpenNative()){
        if(lease.valid)Restore(true);
        status=Code::NativeFault;return;
    }
    if(!controller.Begin(generation,sample.scene,view)){CloseNative();status=Code::NativeFault;return;}
    Images(true);status=Code::Showing;
}
int __cdecl SyncShim(){
    const auto original=reinterpret_cast<SyncFn>(originals[0]);
    const int result=original?original():0;
    if(!inside){inside=true;Pump();inside=false;}
    return result;
}
}
bool Profile(std::uintptr_t module) noexcept {
    return module&&NativeUiSupport::Profile(module,Evidence::spans);
}
bool Start(std::uintptr_t module,bool requested,bool validateOnly,const Callbacks& callbacks,void(*log)(const char*)){
    if(!requested||validateOnly)return false;
    const bool primary=callbacks.capture&&callbacks.acknowledge;
    const bool supplemental=callbacks.supplemental.capture&&callbacks.supplemental.acknowledge&&callbacks.supplemental.format;
    if(!primary&&!supplemental)return false;
    if(attempted)return installed.load()&&!stopping.load();
    if(!Profile(module)){status=Code::Unsupported;return false;}
    attempted=true;base=module;bindings=callbacks;logger=log;stopping=false;
    const std::uint32_t targets[]={Evidence::Rva(Entry::Sync)};
    void* replacements[]={reinterpret_cast<void*>(SyncShim)};
    if(!NativeUiSupport::Install(base,targets,replacements,originals,MinHookBatch::Owner::ArcanaField,reinterpret_cast<const void*>(&Start))){
        stopping=true;status=Code::Unsupported;return false;
    }
    installed=true;status=Code::Waiting;
    if(logger)logger("[ffx-hooks] Arcana: native field message producer armed; waiting for the owning save/field thread\n");
    return true;
}
void Stop() noexcept {stopping=true;}
bool Active() noexcept {return installed.load()&&!stopping.load();}
bool OverlayAllowed(const NativeUi::Images& images) noexcept {
    if(!installed.load()||stopping.load())return false;
    unsigned char bank=255,battle=1;std::int32_t menu=0;std::uint32_t transition=1;
    std::uint16_t scene=0;std::uint32_t activeScene=0;
    return images.session&&images.session==observedGeneration.load()&&
        Read(base+RVA_FFX_SCENE_STATE_OBJECT,scene)&&scene==images.scene&&
        Read(base+RVA_FFX_FASTLOAD_ACTIVE_SCENE,activeScene)&&activeScene==scene&&
        Read(base+Evidence::kBank,bank)&&bank==0&&
        Read(base+RVA_FFX_BATTLE_ACTIVE_FLAG,battle)&&!battle&&
        Read(base+RVA_FFX_CURRENT_MENU_SCREEN_ID,menu)&&menu==-1&&
        Read(base+RVA_FFX_SCENE_TRANSITION_PENDING,transition)&&!transition;
}
bool Project(float x,float y,float z,float& screenX,float& screenY) noexcept {
    if(!Active()||ownerThread!=GetCurrentThreadId()||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return false;
    std::uint32_t owner=0,camera=0;
    std::array<float,16> matrix{};
    if(!Read(base+Evidence::kProjectionOwner,owner)||!owner||!Read(owner+220,camera)||!camera||
       !Read(camera+116,matrix))return false;
    for(float value:matrix)if(!std::isfinite(value))return false;
    __try {
        float point[]={x,y,z};int pixel[2]{},width=0,height=0;
        const float reciprocal=Function<float(__cdecl*)(int*,float*)>(Entry::Project)(pixel,point);
        Function<void(__cdecl*)(int*,int*)>(Entry::Viewport)(&width,&height);
        if(!std::isfinite(reciprocal)||reciprocal<=0||width<=0||height<=0)return false;
        const float sx=.5f+float(pixel[0])/float(width),sy=.5f+float(pixel[1])/float(height);
        if(sx<0||sy<0||sx>1||sy>1)return false;
        screenX=sx;screenY=sy;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
Code Status() noexcept {return status.load();}
}
