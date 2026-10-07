#include "../shared/ExecutableProfile.h"
#include "ArcanaNativeUi.h"
#include "ArcanaAcquisition.h"
#include "ArcanaEvidence.generated.h"
#include "EquipmentWorkshopNativeUi.h"
#include "NativeUiHookSupport.h"
#include <atomic>
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace FfxHooks::Arcana::NativeUi {
namespace {
enum Hook {Control,Draw,Enter,Leave,Cursor,ScaleY,ScaleX,Overview,Count};
constexpr std::uint32_t rvas[Count]={(::FfxHooks::ExecutableProfile::Rva<0x4CEFF0>()),(::FfxHooks::ExecutableProfile::Rva<0x4CF640>()),(::FfxHooks::ExecutableProfile::Rva<0x4CF800>()),(::FfxHooks::ExecutableProfile::Rva<0x4CF8E0>()),(::FfxHooks::ExecutableProfile::Rva<0x4CF960>()),(::FfxHooks::ExecutableProfile::Rva<0x2449D0>()),(::FfxHooks::ExecutableProfile::Rva<0x244990>()),(::FfxHooks::ExecutableProfile::Rva<0x4D26E0>())};
void* originals[Count]{};
void* statusOriginal[1]{};
std::uintptr_t module=0;
std::atomic<bool> active{false},attempted{false};
Callbacks callbacks{};
thread_local void* nativeContext=nullptr;
thread_local unsigned ownerThread=0;
thread_local std::uint64_t menuGeneration=0,lastSession=0;
thread_local std::uint64_t imageGeneration=0;
thread_local bool imageVisible=false;
thread_local bool textDrawing=false;
thread_local bool statusDrawing=false;
thread_local Ui::View view{};
thread_local bool layout=false;
thread_local Mode layoutMode=Mode::Twin;
using ControlFn=int(__cdecl*)(void*);
using VoidFn=int(__cdecl*)();
using ScaleFn=float(__cdecl*)(float);
using PadFn=unsigned short(__cdecl*)();
const char* const actorNames[]={"Tidus","Yuna","Auron","Kimahri","Wakka","Lulu","Rikku"};
namespace NativeApi {
using Fn_ScaleX=float(__cdecl*)(float);
using Fn_ScaleY=float(__cdecl*)(float);
using Fn_DrawWindow=void(__cdecl*)(float,float,float,float,int);
using Fn_DrawString=int(__cdecl*)(int,const unsigned char*,float,float,char,float,float);
using Fn_DrawCursor=int(__cdecl*)(float,float,int);
using Fn_DrawColorQuad=void(__cdecl*)(float,float,float,float,unsigned,unsigned);
// Same recovered FFX font alphabet as NativeMenuShell, without importing its
// unrelated translation-unit-local menu callbacks into this native adapter.
void EncodeLabel(const char* text,unsigned char* output,int capacity){
    constexpr char alphabet[]="0123456789 !\"#$%&'()*+,-./:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz";
    int at=0;
    while(*text&&at<capacity-1){const char* found=std::strchr(alphabet,*text++);output[at++]=static_cast<unsigned char>(0x30+(found?found-alphabet:10));}
    if(capacity>0)output[at]=0;
}
unsigned Argb2Abgr(unsigned color){return (color&0xFF00FF00u)|((color>>16)&0xFFu)|((color&0xFFu)<<16);}
}
namespace DrawNative {
// Use the admitted module explicitly. The same code then exercises the real
// functions in a privately mapped PE fixture, without assuming the main EXE.
float SX(float value){return reinterpret_cast<NativeApi::Fn_ScaleX>(module + (::FfxHooks::ExecutableProfile::Rva<0x244990>()))(value);}
float SY(float value){return reinterpret_cast<NativeApi::Fn_ScaleY>(module + (::FfxHooks::ExecutableProfile::Rva<0x2449D0>()))(value);}
float MenuPhysW(){return SX(1920.f);}
void DrawWindow(float x,float y,float width,float height,int style){reinterpret_cast<NativeApi::Fn_DrawWindow>(module + (::FfxHooks::ExecutableProfile::Rva<0x4F5F70>()))(x,y,width,height,style);}
void DrawString(const unsigned char* text,float x,float y){reinterpret_cast<NativeApi::Fn_DrawString>(module + (::FfxHooks::ExecutableProfile::Rva<0x5016B0>()))(0,text,x,y,0,.78f,1.f);}
void DrawStringSub(const unsigned char* text,float x,float y){reinterpret_cast<NativeApi::Fn_DrawString>(module + (::FfxHooks::ExecutableProfile::Rva<0x5016B0>()))(0,text,x,y,0,.52f,.7f);}
void DrawCursor(float x,float y){reinterpret_cast<NativeApi::Fn_DrawCursor>(module + (::FfxHooks::ExecutableProfile::Rva<0x4C0640>()))(x,y,0);}
void DrawSolidRect(float x,float y,float width,float height,unsigned top,unsigned bottom){
    reinterpret_cast<NativeApi::Fn_DrawColorQuad>(module + (::FfxHooks::ExecutableProfile::Rva<0x4F4B20>()))(x,y,width,height,NativeApi::Argb2Abgr(top),NativeApi::Argb2Abgr(bottom));
}
} // namespace DrawNative

void PublishImages(Images images) noexcept {
    if(!callbacks.images)return;
    // A modal can hide artwork without closing the native menu. Reopening it
    // must advance the graphics epoch without resetting the picker transaction.
    if(images.count&&!imageVisible)++imageGeneration;
    imageVisible=images.count!=0;images.generation=imageGeneration;
    callbacks.images(images);
}
void Hide() noexcept {PublishImages({});}
void ClearView() noexcept {if(view.page!=Ui::Page::Closed)++menuGeneration;view={};Hide();}
bool Idle() noexcept {
    unsigned state=0;
    return nativeContext&&ownerThread==GetCurrentThreadId()&&
        NativeUiSupport::Copy(&state,static_cast<unsigned char*>(nativeContext)+0x1C,4)&&state==10;
}
bool Snapshot(State& state,std::uint64_t& session,unsigned& actor) noexcept {
    if(!callbacks.capture(state,session)||!session)return false;
    actor=static_cast<unsigned>(reinterpret_cast<VoidFn>(module + (::FfxHooks::ExecutableProfile::Rva<0x4A9810>()))());
    if(actor>=kActorCount||Validate(state)!=Error::None)return false;
    if(lastSession!=session){lastSession=session;++menuGeneration;view={};Hide();}
    else if(view.page!=Ui::Page::Closed&&view.actor!=actor){
        const auto category=view.category;
        ++menuGeneration;view={};view.category=category;Hide();
    }
    return menuGeneration!=0;
}
void Text(const char* text,float x,float y,bool small=false) {
    unsigned char encoded[512]{};
    NativeApi::EncodeLabel(text,encoded,static_cast<int>(sizeof(encoded)));
    if(small)DrawNative::DrawStringSub(encoded,x,DrawNative::SY(y));
    else DrawNative::DrawString(encoded,x,DrawNative::SY(y));
}
float NativeTextFit(const char* text,float width,float factor) {
    unsigned char encoded[192]{};NativeApi::EncodeLabel(text,encoded,sizeof(encoded));float measured=0;
    using Measure=int(__cdecl*)(const unsigned char*,float*,unsigned,float,float);
    reinterpret_cast<Measure>(module + (::FfxHooks::ExecutableProfile::Rva<0x505290>()))(encoded,&measured,0,.78f*factor,factor);
    if(std::isfinite(measured)&&measured>width&&width>0)factor*=width/measured;
    return factor;
}
void NativeText(const char* text,float x,float y,float factor=1.f) {
    unsigned char encoded[192]{};NativeApi::EncodeLabel(text,encoded,sizeof(encoded));
    using Draw=int(__cdecl*)(const unsigned char*,float,float,unsigned,float,float);
    reinterpret_cast<Draw>(module + (::FfxHooks::ExecutableProfile::Rva<0x505AB0>()))(encoded,x,DrawNative::SY(y),0,.78f*factor,factor);
}
void Box(float x,float y,float width,float height) {
    DrawNative::DrawSolidRect(DrawNative::SX(x),DrawNative::SY(y),DrawNative::SX(width),DrawNative::SY(height),0xFF303451u,0xFF22283Fu);
    DrawNative::DrawWindow(DrawNative::SX(x),DrawNative::SY(y),DrawNative::SX(width),DrawNative::SY(height),2);
}
void Description(const char* source,float x,float y,unsigned columns=52,unsigned maximumLines=5,float pitch=30.f) {
    if(!source)return;
    for(unsigned line=0;*source&&line<maximumLines;++line){
        char text[96]{};unsigned length=0;
        while(source[length]&&length<columns)++length;
        if(source[length]){unsigned space=length;while(space&&source[space]!=' ')--space;if(space)length=space;}
        std::memcpy(text,source,length);Text(text,x,y+float(line)*pitch,true);source+=length;
        while(*source==' ')++source;
    }
}
void Image(Images& images,unsigned resource,float x,float y,float width,float height) {
    if(images.count>=images.quads.size())return;
    images.quads[images.count++]={resource,x/1920.f,y/1080.f,width/1920.f,height/1080.f};
}
void TarotRow(float y,bool selected,bool locked) {
    const float x=DrawNative::SX(570.f),width=DrawNative::SX(1140.f);
    const unsigned top=locked?0xB02A3048u:selected?0xC07783A2u:0x98515E7Du;
    const unsigned bottom=locked?0xB01A2035u:0xA02E3956u;
    DrawNative::DrawSolidRect(x,DrawNative::SY(y-5.f),width,DrawNative::SY(54.f),top,bottom);
    DrawNative::DrawSolidRect(x,DrawNative::SY(y-5.f),width,DrawNative::SY(1.f),0xB5B8BED2u,0xB5B8BED2u);
    DrawNative::DrawSolidRect(x,DrawNative::SY(y+49.f),width,DrawNative::SY(2.f),0xD0091020u,0xD0091020u);
}
const char* ErrorText(Error error) {
    switch(error){
    case Error::None:return "";
    case Error::NotAcquired:return "This card has not been acquired.";
    case Error::Capacity:return "Constellation allows two Majors, one Major + two Minors, or three Minors.";
    case Error::Stale:return "The collection changed. Review your selection again.";
    case Error::TransferRequired:return "Confirm the transfer from the current owner.";
    case Error::ResolutionRequired:return "Review the third Tarot slots before changing mode.";
    default:return "The selection is unavailable. Return to Equip and try again.";
    }
}
void Root(const State& state,Images& images) {
    const unsigned slots=state.mode==Mode::Twin?2u:3u;
    const float cursorX=static_cast<float>(reinterpret_cast<VoidFn>(module + (::FfxHooks::ExecutableProfile::Rva<0x4D5470>()))());
    for(unsigned slot=0;slot<slots;++slot){
        const float y=492.f+float(slot)*60.f;
        const bool locked=Ui::SlotLocked(state,view.actor,slot);
        TarotRow(y,view.category==slot+2,locked);
        char label[32]{};std::snprintf(label,sizeof(label),"Tarot %s",slot==0?"I":slot==1?"II":"III");
        Text(label,cursorX+DrawNative::SX(55.f),y);
        const auto id=state.slots[view.actor][slot];
        const auto* card=id==kEmpty?nullptr:FindCard(static_cast<unsigned>(id));
        Text(locked?"Locked - two Major Arcana":card?card->name:"Empty",cursorX+DrawNative::SX(420.f),y,true);
        const float iconX=(cursorX+DrawNative::SX(370.f))/DrawNative::MenuPhysW()*1920.f;
        // The square icon master contains a narrow, padded card. Widen its
        // transparent quad while retaining the visible icon's center.
        Image(images,78,iconX-21.f,y-7.f,75.f,54.f);
    }
    if(view.category>=2){
        const float top=state.mode==Mode::Twin?708.f:768.f;
        Box(200.f,top-5.f,1520.f,1033.f-top);
        const auto id=Ui::Preview(view,state);const auto* card=id==kEmpty?nullptr:FindCard(static_cast<unsigned>(id));
        const bool locked=Ui::SlotLocked(state,view.actor,view.category-2);
        const float height=1022.f-top;
        Image(images,card?card->id:79u,400.f,top,height*2.f/3.f,height);
        Text(locked?"Tarot slot locked":card?card->name:"Empty Tarot slot",DrawNative::SX(960.f),top+10.f,true);
        if(locked)Description("Two Major Arcana use all four capacity. Unequip one Major to use this slot.",DrawNative::SX(960.f),top+52.f,48);
        else if(card)Description(card->description,DrawNative::SX(960.f),top+52.f,48);
        else Text("Choose a card to equip.",DrawNative::SX(960.f),top+52.f,true);
    }
    Text(callbacks.detail?callbacks.detail():(state.mode==Mode::Twin?"Twin Arcana - two Tarot slots":"Constellation - Major 2 / Minor 1 - capacity 4"),
         DrawNative::SX(210.f),1045.f,true);
    // Native Equip paints its cursor before this extension's opaque rows.
    // Emit the Tarot cursor last so the row highlight cannot cover it.
    if(view.category>=2)DrawNative::DrawCursor(cursorX,DrawNative::SY(492.f+float(view.category-2)*60.f));
}
void Picker(const State& state,Images& images) {
    Box(180.f,250.f,1540.f,790.f);
    char title[128]{};std::snprintf(title,sizeof(title),"%s - Tarot %s",actorNames[view.actor],view.slot==0?"I":view.slot==1?"II":"III");
    Text(title,DrawNative::SX(220.f),275.f);
    Text(state.mode==Mode::Twin?"Twin Arcana":"Constellation: 2 Majors / 1 Major + 2 Minors / 3 Minors",DrawNative::SX(220.f),327.f,true);
    for(unsigned row=0;row<Ui::kVisibleRows&&view.top+row<Ui::kPickerRows;++row){
        const unsigned index=view.top+row;const float y=390.f+float(row)*68.f;
        const auto cardId=Ui::PickerCard(index);
        const char* name="Unequip";
        if(index==Ui::kModeRow)name=state.mode==Mode::Twin?"Switch to Constellation":"Switch to Twin Arcana";
        else if(cardId!=kEmpty)name=FindCard(static_cast<unsigned>(cardId))->name;
        Text(name,DrawNative::SX(250.f),y,true);
        if(cardId!=kEmpty){
            char status[96]{};const int owner=Owner(state,static_cast<unsigned>(cardId));
            if(!state.acquired[cardId])std::snprintf(status,sizeof(status),"Not acquired");
            else if(owner>=0)std::snprintf(status,sizeof(status),"Equipped by %s",actorNames[owner]);
            else std::snprintf(status,sizeof(status),"Available");
            Text(status,DrawNative::SX(280.f),y+31.f,true);
        }
        if(index==view.cursor)DrawNative::DrawCursor(DrawNative::SX(202.f),DrawNative::SY(y));
    }
    const auto id=Ui::Preview(view,state);
    if(id!=kEmpty)Image(images,static_cast<unsigned>(id),1230.f,328.f,440.f,660.f);
    else Image(images,79,1230.f,328.f,440.f,660.f);
    if(view.error!=Error::None)Description(ErrorText(view.error),DrawNative::SX(230.f),930.f,68,2);
    else if(id!=kEmpty)Description(state.acquired[id]?FindCard(static_cast<unsigned>(id))->description:Acquisition::Requirement(static_cast<unsigned>(id)),DrawNative::SX(230.f),880.f,68,4,28.f);
    Text("Confirm: choose   Back: Equip   Up/Down: browse   Page: skip seven",DrawNative::SX(230.f),1000.f,true);
}
void Modal(const State& state) {
    Box(240.f,360.f,1440.f,430.f);
    if(view.page==Ui::Page::SlotBlocked){
        Text("Tarot slot locked",DrawNative::SX(310.f),395.f);
        Description("This character already has two Major Arcana equipped. Unequip one Major to use this slot.",DrawNative::SX(310.f),470.f,70,3,34.f);
        Description(ErrorText(Error::Capacity),DrawNative::SX(310.f),585.f,70,2,34.f);
        Text("Confirm / Back: return to Equip",DrawNative::SX(310.f),690.f,true);return;
    }else if(view.page==Ui::Page::Transfer){
        const auto* card=FindCard(static_cast<unsigned>(view.pending));const int owner=Owner(state,static_cast<unsigned>(view.pending));
        Text("Transfer Tarot card?",DrawNative::SX(310.f),395.f);
        Text(card?card->name:"Unavailable",DrawNative::SX(310.f),470.f,true);
        char message[128]{};std::snprintf(message,sizeof(message),"Move from %s to %s? The previous slot will become empty.",owner>=0?actorNames[owner]:"Unknown",actorNames[view.actor]);
        Text(message,DrawNative::SX(310.f),535.f,true);
    }else{
        Text(view.proposed==Mode::Twin?"Switch to Twin Arcana?":"Switch to Constellation?",DrawNative::SX(310.f),395.f);
        Text(view.proposed==Mode::Twin?"Third Tarot slots will be unequipped for every character.":"Each character may use up to three slots within capacity four.",DrawNative::SX(310.f),490.f,true);
        Text("All cards remain in your collection.",DrawNative::SX(310.f),545.f,true);
    }
    Text("Confirm: accept   Back: cancel",DrawNative::SX(310.f),690.f,true);
}
float WorkshopY(float value) {
    return active.load()&&layout?Ui::WorkshopLabelY(value,layoutMode):value;
}
float __cdecl ScaleShim(float value) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if(active.load()&&layout&&caller>=module&&caller-module<0x237D000u)
        value=Ui::LayoutY(static_cast<std::uint32_t>(caller-module),value,layoutMode);
    return reinterpret_cast<ScaleFn>(originals[ScaleY])(value);
}
float __cdecl ScaleXShim(float value) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if(active.load()&&layout&&caller>=module&&caller-module<0x237D000u)
        value=Ui::LayoutX(static_cast<std::uint32_t>(caller-module),value,layoutMode);
    return reinterpret_cast<ScaleFn>(originals[ScaleX])(value);
}
bool NativePageLive(void* object) noexcept {
    unsigned char retiring=1;
    return object&&NativeUiSupport::Copy(&retiring,static_cast<unsigned char*>(object)+0x41,sizeof(retiring))&&!retiring;
}
void StatusOverlay(void* object,unsigned nativeCapacity) noexcept {
    if(!active.load()||statusDrawing||!NativePageLive(object))return;
    unsigned context[5]{};State state;std::uint64_t session=0;
    if(!NativeUiSupport::Copy(context,static_cast<unsigned char*>(object)+0x58,sizeof(context))||
       context[0]!=0||context[2]!=0||context[3]!=0||
       !callbacks.capture(state,session)||!session)return;
    // Capture admits only the save-owning thread outside battle. Status can be
    // entered before Equip, so it must not borrow Equip's context/generation.
    const auto actor=static_cast<unsigned>(reinterpret_cast<VoidFn>(module + (::FfxHooks::ExecutableProfile::Rva<0x4A9810>()))());
    const auto rows=Ui::BuildStatusRows(state,actor);
    const auto geometry=Ui::StatusLayout(nativeCapacity,rows.count);
    if(!geometry.pitch)return;
    // The ordinary ability font owns its native outline and proportional
    // metrics. It must not enter the legacy small-font outline workaround.
    const bool previous=textDrawing;statusDrawing=true;textDrawing=false;
    __try {
        __try {
            DrawNative::DrawSolidRect(DrawNative::SX(210.f),DrawNative::SY(geometry.header),DrawNative::SX(1500.f),DrawNative::SY(48.f),0xC024283Du,0xC015192Au);
            NativeText("Arcana",DrawNative::SX(250.f),geometry.header+7.f);
            // Status supplies the real Auto-Abilities atlas caption ID. Reuse
            // that artwork at its native 430x36 geometry, rather than a font imitation.
            using Caption=void(__cdecl*)(unsigned,float,float,float,float,unsigned);
            reinterpret_cast<Caption>(module + (::FfxHooks::ExecutableProfile::Rva<0x4F8D50>()))(context[4],DrawNative::SX(745.f),DrawNative::SY(geometry.header+6.f),DrawNative::SX(430.f),DrawNative::SY(36.f),128);
            float factor=(std::min)(1.f,(geometry.height-6.f)/32.f);
            for(unsigned i=0;i<rows.count;++i)factor=NativeTextFit(rows.rows[i].text.data(),DrawNative::SX(494.f),factor);
            for(unsigned i=0;i<rows.count;++i){
                const float x=i%2?970.f:210.f,y=geometry.top+float(i/2)*geometry.pitch;
                DrawNative::DrawWindow(DrawNative::SX(x),DrawNative::SY(y),DrawNative::SX(740.f),DrawNative::SY(geometry.height),14);
                // A small gold cross marks the Arcana source without retaining
                // a graphics overlay after leaving this native Status page.
                const float center=y+geometry.height*.5f;
                DrawNative::DrawSolidRect(DrawNative::SX(x+184.f),DrawNative::SY(center-8.f),DrawNative::SX(3.f),DrawNative::SY(16.f),0xFFE9D599u,0xFFB09860u);
                DrawNative::DrawSolidRect(DrawNative::SX(x+178.f),DrawNative::SY(center-2.f),DrawNative::SX(15.f),DrawNative::SY(3.f),0xFFE9D599u,0xFFB09860u);
                NativeText(rows.rows[i].text.data(),DrawNative::SX(x+236.f),y+3.f,factor);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER){active=false;}
    } __finally {statusDrawing=false;textDrawing=previous;}
}
int __cdecl StatusShim(void* object) {
    const int result=reinterpret_cast<ControlFn>(statusOriginal[0])(object);
    StatusOverlay(object,8);return result;
}
int __cdecl OverviewShim(void* object) {
    const int result=reinterpret_cast<ControlFn>(originals[Overview])(object);
    State state;std::uint64_t session=0;short phase=0,cover=0,direction=0;
    if(!active.load()||!NativePageLive(object)||!callbacks.capture(state,session)||!session||
       !NativeUiSupport::Copy(&cover,reinterpret_cast<const void*>(module + (::FfxHooks::ExecutableProfile::Rva<0x146A9C0>())),sizeof(cover))||
       !NativeUiSupport::Copy(&direction,reinterpret_cast<const void*>(module + (::FfxHooks::ExecutableProfile::Rva<0x146A9BC>())),sizeof(direction))||
       cover!=0||direction>0||
       !NativeUiSupport::Copy(&phase,static_cast<unsigned char*>(object)+0x52,sizeof(phase)))return result;
    // The cached overview still draws under the native page-cover transition.
    // Its object animation is independent: only the global cover being fully
    // gone admits additions after the original draw, including on return.
    const auto actor=static_cast<unsigned>(reinterpret_cast<VoidFn>(module + (::FfxHooks::ExecutableProfile::Rva<0x4A9810>()))());
    const auto slots=Ui::BuildEquippedSlots(state,actor);if(!slots.count)return result;
    const int animation=reinterpret_cast<int(__cdecl*)(int,int)>(module + (::FfxHooks::ExecutableProfile::Rva<0x4D3090>()))(phase,1);
    const float offset=static_cast<float>(std::int64_t(animation)*341/4096);
    const float width=(1240.f-12.f*float(slots.count-1))/float(slots.count);
    const bool previous=textDrawing;textDrawing=false;
    __try {
        __try {
            float factor=1.f;
            for(unsigned i=0;i<slots.count;++i)factor=NativeTextFit(slots.slots[i].text.data(),DrawNative::SX(width-50.f),factor);
            for(unsigned i=0;i<slots.count;++i){
                const float x=DrawNative::SX(470.f+float(i)*(width+12.f))+offset;
                DrawNative::DrawWindow(x,DrawNative::SY(490.f),DrawNative::SX(width),DrawNative::SY(44.f),14);
                const unsigned color=slots.slots[i].locked?0xFF9198ABu:0xFFE9D599u;
                DrawNative::DrawSolidRect(x+DrawNative::SX(12.f),DrawNative::SY(500.f),DrawNative::SX(15.f),DrawNative::SY(24.f),color,0xFF384466u);
                NativeText(slots.slots[i].text.data(),x+DrawNative::SX(38.f),496.f,factor);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER){active=false;}
    } __finally {textDrawing=previous;}
    return result;
}
int __cdecl CursorShim() {
    if(active.load()&&layout&&view.page==Ui::Page::Root&&view.category>=2)return 0;
    return reinterpret_cast<VoidFn>(originals[Cursor])();
}
int __cdecl EnterShim(void* context) {
    const int result=reinterpret_cast<ControlFn>(originals[Enter])(context);
    if(active.load()){nativeContext=context;ownerThread=GetCurrentThreadId();++menuGeneration;lastSession=0;ClearView();}
    return result;
}
int __cdecl LeaveShim() {
    ClearView();nativeContext=nullptr;++menuGeneration;
    return reinterpret_cast<VoidFn>(originals[Leave])();
}
void AdvanceNativePortrait() {
    // The original Equip controller performs this on every frame before its
    // input state machine. Private Tarot input must not freeze that transition.
    using Fn=void(__cdecl*)(int,int*,int*,int*,int*);
    reinterpret_cast<Fn>(module + (::FfxHooks::ExecutableProfile::Rva<0x4BF720>()))(0,reinterpret_cast<int*>(module + (::FfxHooks::ExecutableProfile::Rva<0x1FCC3C8>())),
        reinterpret_cast<int*>(module + (::FfxHooks::ExecutableProfile::Rva<0x1FCC3C4>())),reinterpret_cast<int*>(module + (::FfxHooks::ExecutableProfile::Rva<0x1FCC3C0>())),
        reinterpret_cast<int*>(module + (::FfxHooks::ExecutableProfile::Rva<0x1FCC3BC>())));
}
int __cdecl ControlShim(void* context) {
    if(!active.load()||context!=nativeContext||ownerThread!=GetCurrentThreadId())
        return reinterpret_cast<ControlFn>(originals[Control])(context);
    if(!Idle()){ClearView();return reinterpret_cast<ControlFn>(originals[Control])(context);}
    if(callbacks.tick)callbacks.tick();
    State state;std::uint64_t session=0;unsigned actor=0;
    if(!Snapshot(state,session,actor)){
        const bool captured=view.page!=Ui::Page::Closed;ClearView();
        if(captured){AdvanceNativePortrait();return 0;}
        return reinterpret_cast<ControlFn>(originals[Control])(context);
    }
    Ui::Observe(view,state,reinterpret_cast<std::uintptr_t>(context),menuGeneration,actor,true);
    const unsigned direction=reinterpret_cast<PadFn>(module + (::FfxHooks::ExecutableProfile::Rva<0x4BE440>()))();
    const unsigned edge=reinterpret_cast<PadFn>(module + (::FfxHooks::ExecutableProfile::Rva<0x4BE480>()))();
    if(view.page==Ui::Page::Root&&(direction&12u)&&!(direction&0x5000u)&&!(edge&0x60u))
        return reinterpret_cast<ControlFn>(originals[Control])(context);
    Ui::Key key=Ui::Key::None;
    if(edge&0x40)key=Ui::Key::Cancel;
    else if(edge&0x20)key=Ui::Key::Confirm;
    else if(direction&0x1000)key=Ui::Key::Up;
    else if(direction&0x4000)key=Ui::Key::Down;
    else if(direction&1)key=Ui::Key::PageUp;
    else if(direction&2)key=Ui::Key::PageDown;
    const auto before=view;
    const auto command=Ui::Input(view,state,key);
    if(view.page==Ui::Page::Root&&view.category<2){
        const unsigned selected=view.category;
        if(!NativeUiSupport::Copy(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0x146A5E4>())),&selected,4)){ClearView();return 0;}
        using GearFn=unsigned(__cdecl*)(unsigned);
        const unsigned gear=reinterpret_cast<GearFn>(module+(selected?(::FfxHooks::ExecutableProfile::Rva<0x4A97D0u>()):(::FfxHooks::ExecutableProfile::Rva<0x4A9C20u>())))(actor);
        if(gear>0xFFFFu||!NativeUiSupport::Copy(reinterpret_cast<void*>(module + (::FfxHooks::ExecutableProfile::Rva<0x146A5F0>())),&gear,4)){ClearView();return 0;}
    }
    if(command.action==Ui::Action::NativeWeapon||command.action==Ui::Action::NativeArmor||command.action==Ui::Action::NativeBack){
        if(command.action==Ui::Action::NativeBack)ClearView();
        return reinterpret_cast<ControlFn>(originals[Control])(context);
    }
    AdvanceNativePortrait();
    Error result=Error::None;
    if(command.action==Ui::Action::Equip)
        result=callbacks.equip(session,command.revision,command.actor,command.slot,command.card,command.transfer);
    else if(command.action==Ui::Action::Mode)
        result=callbacks.mode(session,command.revision,command.mode,command.mode==Mode::Twin);
    if(command.action==Ui::Action::Equip||command.action==Ui::Action::Mode){
        State after;std::uint64_t afterSession=0;
        if(callbacks.capture(after,afterSession)&&afterSession==session)Ui::Result(view,after,result);
        else {result=Error::Stale;ClearView();}
    }
    // The native controller owns sounds on delegated paths. Only a consumed
    // private input emits one cue, after its outcome is known, never from Draw.
    const auto sound=Ui::Feedback(before,view,key,command,result);
    if(sound!=Ui::Sound::None)reinterpret_cast<int(__cdecl*)(int)>(module + (::FfxHooks::ExecutableProfile::Rva<0x486B00>()))(static_cast<int>(sound));
    return 0;
}
int __cdecl DrawShim() {
    if(!active.load()||!Idle())return reinterpret_cast<VoidFn>(originals[Draw])();
    State state;std::uint64_t session=0;unsigned actor=0;
    if(!Snapshot(state,session,actor)){ClearView();return reinterpret_cast<VoidFn>(originals[Draw])();}
    Ui::Observe(view,state,reinterpret_cast<std::uintptr_t>(nativeContext),menuGeneration,actor,true);
    const bool previous=layout;const Mode previousMode=layoutMode;
    layout=view.page==Ui::Page::Root;layoutMode=state.mode;int result=0;
    const bool previousDrawing=textDrawing;textDrawing=true;
    __try {
        __try {result=reinterpret_cast<VoidFn>(originals[Draw])();}
        __finally {layout=previous;layoutMode=previousMode;}
        Images images;images.generation=menuGeneration;
        __try {
            if(view.page==Ui::Page::Root)Root(state,images);
            else if(view.page==Ui::Page::Picker)Picker(state,images);
            else if(view.page==Ui::Page::Transfer||view.page==Ui::Page::ModeReview||view.page==Ui::Page::SlotBlocked)Modal(state);
            PublishImages(images);
        } __except(EXCEPTION_EXECUTE_HANDLER){active=false;Hide();}
    } __finally {textDrawing=previousDrawing;}
    return result;
}
}
bool Start(std::uintptr_t base,bool requested,bool validateOnly,const Callbacks& bindings,void(*log)(const char*)) {
    if(!requested)return false;
    if(attempted.load())return active.load();
    const bool sharedStatus=EquipmentWorkshop::NativeUi::StatusBridgeInstalled(base);
    if(!base||!bindings.capture||!bindings.equip||!bindings.mode||!bindings.images||
       !NativeUiSupport::Profile(base,Evidence::spans)||
       (!sharedStatus&&!NativeUiSupport::Profile(base,Evidence::statusSpans))){
        if(log)log("[ffx-hooks] Arcana Equip: profile or bindings rejected\n");return false;
    }
    if(validateOnly)return false;
    if(attempted.exchange(true))return active.load();
    module=base;callbacks=bindings;
    void* replacements[Count]={reinterpret_cast<void*>(&ControlShim),reinterpret_cast<void*>(&DrawShim),
        reinterpret_cast<void*>(&EnterShim),reinterpret_cast<void*>(&LeaveShim),reinterpret_cast<void*>(&CursorShim),reinterpret_cast<void*>(&ScaleShim),reinterpret_cast<void*>(&ScaleXShim),reinterpret_cast<void*>(&OverviewShim)};
    if(!NativeUiSupport::Install(base,rvas,replacements,originals,MinHookBatch::Owner::ArcanaUi,reinterpret_cast<const void*>(&Start))){
        if(log)log("[ffx-hooks] Arcana Equip: native installation rejected; admission remains OFF\n");
        return false;
    }
    if(sharedStatus)EquipmentWorkshop::NativeUi::SetStatusObserver(StatusOverlay);
    else {
        constexpr std::uint32_t statusRva[]={::FfxHooks::ExecutableProfile::Rva<0x4D2760>()};void* replacement[]={reinterpret_cast<void*>(&StatusShim)};
        if(!NativeUiSupport::Install(base,statusRva,replacement,statusOriginal,MinHookBatch::Owner::ArcanaUi,reinterpret_cast<const void*>(&Start))){
            if(log)log("[ffx-hooks] Arcana Status: native installation rejected; admission remains OFF\n");return false;
        }
    }
    EquipmentWorkshop::NativeUi::SetEquipmentLayout(WorkshopY);active=true;
    if(log)log("[ffx-hooks] Arcana Equip/Status: native category, picker and auto-ability callbacks installed; RT2 not implied\n");
    return true;
}
void Stop() noexcept {active=false;EquipmentWorkshop::NativeUi::SetEquipmentLayout(nullptr);EquipmentWorkshop::NativeUi::SetStatusObserver(nullptr);Hide();}
bool Active() noexcept {return active.load();}
bool TextDrawingActive() noexcept {return active.load()&&textDrawing;}
}
