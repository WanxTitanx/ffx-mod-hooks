// Included by dllmain after the shared native input/drawing primitives. This menu
// owns its own object, state and close drain; it is not an F7/F8/F9 submenu.
#include "EquipmentWorkshopNames.h"
#include "EquipmentWorkshopSettings.h"
#include "EquipmentWorkshopNativeUi.h"
namespace EquipmentMenu {
using namespace NativeMenu;
namespace W=FfxHooks::EquipmentWorkshop;
namespace N=FfxHooks::EquipmentWorkshop::Names;
enum class Page {Inventory,Piece,Mode,Fifth,Models,Donor,Source,Destination,TransferCount,Expand,Clear,Evolve,Confirm,Info};
enum class Action {Close,Back,Filter,Piece,Mode,Refine,Unlock,Fifth,Reforge,Fuse,Expand,Clear,Evolve,Value,Confirm,Second};
struct Row {char label[96]{};Action action=Action::Back;unsigned value=0;};
static Menu menu{};
static Row rows[240]{};
static int count=0,selectedSlot=-1,ownerFilter=-1,result=-2,delay=0,lastEdge=0,closing=0,closePasses=0;
static volatile LONG owned=0,wantOpen=0,wantClose=0;
static volatile LONG drawSeen=0;
static Page page=Page::Inventory;
static workshop::State snapshot{};
static workshop::Request draft{};
static workshop::Plan preview{};
static workshop::Error reviewError=workshop::Error::InvalidRequest;
static unsigned transfer=0;
static std::uint64_t selectedIdentity=0;
static bool cursorOwned=false;
static DWORD menuThread=0;
static char notice[144]{};
static float eased=-1;
struct NavigationFrame {Page page;int selected,top,slot;std::uint64_t identity;workshop::Request request;unsigned transfer;};
static NavigationFrame parents[16]{},origin{};
static unsigned parentCount=0;
static int __cdecl Draw(int obj);
static int __cdecl Input(int obj);
static bool Active(){return InterlockedCompareExchange(&owned,0,0)!=0;}
static bool NeedsPump(){return Active()||InterlockedCompareExchange(&wantOpen,0,0)!=0||InterlockedCompareExchange(&wantClose,0,0)!=0;}
static bool StillOwned(int obj){
    if(!obj)return false;
    const auto address=static_cast<std::uintptr_t>(static_cast<std::uint32_t>(obj));
    const auto first=FfxBase()+(POOL_VA-kImageBase);
    if(address<first||address>=first+POOL_MAX*POOL_STRIDE||(address-first)%POOL_STRIDE)return false;
    return RdD(obj,O_DRAW)==static_cast<int>(reinterpret_cast<std::uintptr_t>(&Draw))&&
           RdD(obj,O_UPDATE)==static_cast<int>(reinterpret_cast<std::uintptr_t>(&Input))&&*Pb(obj,O_ACTIVE);
}
static void Add(Action action,unsigned value,const char* label){
    if(count>=240)return;rows[count].action=action;rows[count].value=value;strncpy_s(rows[count].label,label,_TRUNCATE);++count;
}
static bool Regular(const workshop::Piece& p){return p.id&&p.native[4]<7&&p.native[5]<2;}
static const char* Owner(const workshop::Piece& p){return p.native[4]<7?N::owners[p.native[4]]:"Special";}
static void PieceLabel(const workshop::Piece& p,char* out,std::size_t capacity){
    unsigned rank=0;for(unsigned i=0;i<5;++i)rank+=workshop::AbilityRank(p,i);
    _snprintf_s(out,capacity,_TRUNCATE,"%s %s #%llu +%u%s%s",Owner(p),p.native[5]?"Armor":"Weapon",static_cast<unsigned long long>(p.id),rank,p.native[6]!=255?" [Equipped]":"",p.fifthUnlocked?" [Bound]":"");
}
static void Geometry(int obj){
    WrW(obj,O_COUNT,static_cast<std::int16_t>(count));WrW(obj,O_PAGE,8);WrW(obj,O_SELECTED,0);WrW(obj,O_TOP,0);
    result=-2;delay=12;lastEdge=PadEdge()&0x60;eased=-1;F7SeedPointerForDestination();InterlockedExchange(&g_f7MouseWheelDelta,0);
}
static void Build(Page next){
    page=next;count=0;char line[128]{};
    const auto* piece=selectedSlot>=0&&selectedSlot<200?&snapshot.pieces[selectedSlot]:nullptr;
    switch(page){
    case Page::Inventory:
        _snprintf_s(line,sizeof(line),_TRUNCATE,"Character: %s",ownerFilter<0?"All":N::owners[ownerFilter]);Add(Action::Filter,0,line);
        for(unsigned i=0;i<200;++i)if(Regular(snapshot.pieces[i])&&(ownerFilter<0||snapshot.pieces[i].native[4]==ownerFilter)){PieceLabel(snapshot.pieces[i],line,sizeof(line));Add(Action::Piece,i,line);}
        Add(Action::Close,0,"Return to game");break;
    case Page::Piece:
        if(!piece||!piece->id){Build(Page::Inventory);return;}
        {workshop::Policy policy{};const bool valid=W::Settings::Read(policy);
        _snprintf_s(line,sizeof(line),_TRUNCATE,"Refine: %s",valid?W::Settings::ModeName(policy.mode):"invalid F8 / INI setting");Add(Action::Refine,0,line);}
        Add(piece->fifthUnlocked?Action::Fifth:Action::Unlock,0,piece->fifthUnlocked?"Choose fifth ability":"Unlock fifth slot (four native slots)");
        Add(Action::Reforge,0,"Reforge owner / type");Add(Action::Fuse,0,"Fuse equipment");Add(Action::Expand,0,"Expand original slots");
        Add(Action::Clear,0,"Remove an ability");Add(Action::Evolve,0,"Evolve an ability");Add(Action::Back,0,"Equipment list");break;
    case Page::Mode:Add(Action::Back,0,"Choose the refinement method in F8 > Dev");break;
    case Page::Fifth:
        if(piece&&!workshop::NativeSlotsFilled(*piece)){Add(Action::Back,0,"Fill the four native abilities first");break;}
        {workshop::Policy policy{};if(piece&&W::Settings::Read(policy))for(unsigned id=0;id<131;++id){unsigned item=0,quantity=0;
            if(workshop::FifthCost(*piece,static_cast<std::uint16_t>(0x8000+id),policy,item,quantity)&&
               workshop::CustomizeEligibility(*piece,4,static_cast<std::uint16_t>(0x8000+id))==workshop::Error::Ok){
                bool native=false;unsigned originalItem=0,originalQuantity=0;
                workshop::CustomizeCost(static_cast<std::uint16_t>(0x8000+id),policy,originalItem,originalQuantity,native);
                _snprintf_s(line,sizeof(line),_TRUNCATE,"%s%s",N::Ability(0x8000+id),native?"":" [Mod recipe]");Add(Action::Value,0x8000+id,line);
            }}}
        Add(Action::Back,0,"Back");break;
    case Page::Models:
        for(unsigned i=0;i<200;++i){const auto& p=snapshot.pieces[i];if(!Regular(p)||(p.native[3]&12))continue;
            if(piece&&piece->fifthUnlocked&&(p.native[4]!=piece->native[4]||(piece->fifth!=255&&p.native[5]!=piece->native[5])))continue;
            bool duplicate=false;for(unsigned j=0;j<i;++j){const auto& q=snapshot.pieces[j];if(Regular(q)&&!(q.native[3]&12)&&q.native[4]==p.native[4]&&q.native[5]==p.native[5]&&q.native[12]==p.native[12]&&q.native[13]==p.native[13]){duplicate=true;break;}}
            if(!duplicate){_snprintf_s(line,sizeof(line),_TRUNCATE,"%s %s - look %u",Owner(p),p.native[5]?"Armor":"Weapon",i+1);Add(Action::Value,i,line);}}
        Add(Action::Back,0,"Back");break;
    case Page::Donor:
        for(unsigned i=0;i<200;++i){const auto& p=snapshot.pieces[i];if(i==static_cast<unsigned>(selectedSlot)||!Regular(p)||(p.native[3]&12)||p.native[6]!=255||!piece)continue;PieceLabel(p,line,sizeof(line));Add(Action::Value,i,line);}
        Add(Action::Back,0,"Back");break;
    case Page::Source:{const auto& p=snapshot.pieces[draft.other];
        for(unsigned i=0;i<p.native[11];++i){const auto id=workshop::Ability(p,i);if(id==255||(transfer&&i==draft.from[0]))continue;_snprintf_s(line,sizeof(line),_TRUNCATE,"Slot %u: %s",i+1,N::Ability(id));Add(Action::Value,i,line);}Add(Action::Back,0,"Back");break;}
    case Page::Destination:
        if(piece)for(unsigned i=0;i<piece->native[11];++i){if(transfer&&i==draft.to[0])continue;_snprintf_s(line,sizeof(line),_TRUNCATE,"Replace slot %u: %s",i+1,N::Ability(workshop::Ability(*piece,i)));Add(Action::Value,i,line);}Add(Action::Back,0,"Back");break;
    case Page::TransferCount:Add(Action::Value,0,"Review fusion with one ability");Add(Action::Second,0,"Transfer a second ability");Add(Action::Back,0,"Back to destination");break;
    case Page::Expand:
        {unsigned recipe=0;if(piece&&W::Settings::ReadExpansion(recipe))for(unsigned cap=piece->native[11]+1;cap<=4;++cap){
            _snprintf_s(line,sizeof(line),_TRUNCATE,"%u slots - %s Key Sphere recipe",cap,recipe==2?"1/2/3/4":"one each");Add(Action::Value,cap|((recipe-1)<<8),line);}}
        Add(Action::Back,0,"Back");break;
    case Page::Clear:case Page::Evolve:
        if(piece)for(unsigned i=0;i<4u+piece->fifthUnlocked;++i){const unsigned word=workshop::Ability(*piece,i);if(word==255)continue;const unsigned id=word-0x8000;
            if(page==Page::Evolve && (i==4 || !((id>=98&&id<121&&(id-98)%4<3)||(id>=47&&id<=75&&(id-47)%4==0))))continue;
            _snprintf_s(line,sizeof(line),_TRUNCATE,"Slot %u: %s",i+1,N::Ability(word));Add(Action::Value,i,line);}Add(Action::Back,0,"Back");break;
    case Page::Confirm:if(reviewError==workshop::Error::Ok)Add(Action::Confirm,0,preview.policy.devFreeMaterials||preview.policy.devFreeGil?"Confirm with DEV exemption":"Confirm this change");Add(Action::Back,0,reviewError==workshop::Error::Ok?"Keep as is":"Back - requirements not met");break;
    case Page::Info:Add(Action::Close,0,"Return to game");break;
    }
    if(menu.obj)Geometry(menu.obj);
}
static void SetNotice(const char* text){strncpy_s(notice,text,_TRUNCATE);}
static void Unavailable(const char* text){
    // A failed read must never leave a pre-transaction donor visible/selectable.
    snapshot={};draft={};preview={};selectedSlot=-1;selectedIdentity=0;parentCount=0;transfer=0;
    SetNotice(text);Build(Page::Info);PlaySfx(3);
}
static NavigationFrame Remember(){return {page,menu.obj?RdW(menu.obj,O_SELECTED):0,menu.obj?RdW(menu.obj,O_TOP):0,selectedSlot,selectedIdentity,draft,transfer};}
static void Restore(const NavigationFrame& saved){
    // Inventory keeps the last target as its comparison card; deeper pages
    // restore the exact operation target captured on entry.
    if(saved.page!=Page::Inventory){selectedSlot=saved.slot;selectedIdentity=saved.identity;}
    draft=saved.request;transfer=saved.transfer;
    preview={};reviewError=workshop::Error::InvalidRequest;Build(saved.page);
    if(menu.obj){const int selected=(std::max)(0,(std::min)(saved.selected,count-1));
        const int top=(std::max)(0,(std::min)(saved.top,(std::max)(0,count-8)));
        WrW(menu.obj,O_SELECTED,static_cast<short>(selected));WrW(menu.obj,O_TOP,static_cast<short>(top));}
}
static void Navigate(Page next){
    if(next!=page&&parentCount<16)parents[parentCount++]=origin;
    Build(next);PlaySfx(1);
}
static void ReturnToPiece(){
    while(parentCount){const auto saved=parents[--parentCount];if(saved.page==Page::Piece){Restore(saved);draft={};preview={};transfer=0;PlaySfx(1);return;}}
    draft={};preview={};transfer=0;Build(Page::Piece);PlaySfx(1);
}
static void NewRequest(workshop::Op op){draft={};preview={};reviewError=workshop::Error::InvalidRequest;draft.op=op;draft.slot=static_cast<std::uint16_t>(selectedSlot);draft.pieceId=snapshot.pieces[selectedSlot].id;draft.revision=snapshot.revision;}
static void Review(){
    const auto error=W::Preview(draft,preview);reviewError=error;
    if(error==workshop::Error::Locked){Unavailable(workshop::Message(error));return;}
    if(error!=workshop::Error::Ok){
        Log("[ffx-hooks] Workshop UI: preview rejected op=%u error=%d\n",static_cast<unsigned>(draft.op),static_cast<int>(error));
        SetNotice(error==workshop::Error::InvalidRequest&&draft.op==workshop::Op::UnlockFifth?"Opening the fifth slot requires four OPEN native slots, filled or empty.":
                  error==workshop::Error::InvalidRequest&&draft.op==workshop::Op::SetFifth?"Fill the four native abilities before customizing the fifth slot.":workshop::Message(error));
        if(error==workshop::Error::Materials||error==workshop::Error::Gil){if(parentCount<16)parents[parentCount++]=origin;Build(Page::Confirm);}
        else Restore(origin);PlaySfx(3);return;
    }
    if(preview.policy.devFreeMaterials||preview.policy.devFreeGil){
        SetNotice(draft.op==workshop::Op::Fuse?"DEV exemption: listed resource waivers apply. The donor WILL be destroyed. Confirm to proceed.":"DEV exemption: nominal prices remain visible. Waived resources are not consumed. Confirm to proceed.");Navigate(Page::Confirm);return;
    }
    if(draft.op==workshop::Op::UnlockFifth){SetNotice("Ten owner spheres; Master substitutes 1:1. Unlocking permanently binds this equipment to its owner.");Navigate(Page::Confirm);return;}
    if(draft.op==workshop::Op::Evolve){SetNotice("Half the destination Customize recipe (rounded up) plus 25,000 Gil. The evolved ability starts at +0.");Navigate(Page::Confirm);return;}
    SetNotice(draft.op==workshop::Op::Fuse?"The donor is destroyed. Listed materials and Gil are consumed.":draft.op==workshop::Op::Refine&&preview.policy.mode==2?"Require every possible ingredient; pay only base + rolled result.":"Review the materials before confirming.");Navigate(Page::Confirm);
}
static void Choose(const Row& row){
    if(row.action==Action::Close){PlaySfx(4);InterlockedExchange(&wantClose,1);return;}
    if(page==Page::Info)return;
    const auto displayedRevision=snapshot.revision;
    if(!W::Capture(snapshot)){Unavailable(W::Detail());return;}
    const auto access=W::Access();if(access!=workshop::Error::Ok){Unavailable(workshop::Message(access));return;}
    if(row.action==Action::Back){
        if(snapshot.revision==displayedRevision&&parentCount){Restore(parents[--parentCount]);}
        else {parentCount=0;draft={};preview={};transfer=0;selectedSlot=-1;selectedIdentity=0;Build(Page::Inventory);}
        PlaySfx(4);return;
    }
    if(snapshot.revision!=displayedRevision){parentCount=0;selectedSlot=-1;selectedIdentity=0;draft={};preview={};SetNotice("Inventory changed. Select the piece again.");Build(Page::Inventory);PlaySfx(3);return;}
    origin=Remember();
    if(page==Page::Inventory){
        if(row.action==Action::Filter){ownerFilter=ownerFilter==6?-1:ownerFilter+1;Navigate(Page::Inventory);}
        else if(row.action==Action::Piece&&row.value<200&&Regular(snapshot.pieces[row.value])){selectedSlot=static_cast<int>(row.value);selectedIdentity=snapshot.pieces[selectedSlot].id;SetNotice("");Navigate(Page::Piece);}
        return;
    }
    if(selectedSlot<0||selectedSlot>=200||snapshot.pieces[selectedSlot].id!=selectedIdentity){parentCount=0;selectedSlot=-1;selectedIdentity=0;draft={};preview={};SetNotice("The selected piece changed. Choose it again.");Build(Page::Inventory);PlaySfx(3);return;}
    const auto& p=snapshot.pieces[selectedSlot];
    if(page==Page::Piece){
        SetNotice("");
        switch(row.action){
        case Action::Refine:NewRequest(workshop::Op::Refine);Review();return;
        case Action::Mode:Navigate(Page::Mode);return;
        case Action::Unlock:NewRequest(workshop::Op::UnlockFifth);Review();return;
        case Action::Fifth:Navigate(Page::Fifth);return;
        case Action::Reforge:Navigate(Page::Models);return;
        case Action::Fuse:NewRequest(workshop::Op::Fuse);transfer=0;Navigate(Page::Donor);return;
        case Action::Expand:Navigate(Page::Expand);return;
        case Action::Clear:Navigate(Page::Clear);return;
        case Action::Evolve:Navigate(Page::Evolve);return;
        default:return;
        }
    }
    if(page==Page::Confirm){
        if(reviewError!=workshop::Error::Ok||row.action!=Action::Confirm){PlaySfx(3);return;}
        const auto completed=draft;
        const auto winner=preview.chosenAbility;
        const bool committed=W::Commit(draft,preview);draft={};preview={};
        if(!committed){
            if(!W::Capture(snapshot)){Unavailable(W::Detail());return;}
            const auto currentAccess=W::Access();if(currentAccess!=workshop::Error::Ok){Unavailable(workshop::Message(currentAccess));return;}
            SetNotice("The inventory changed. Review a new preview.");parentCount=0;Build(Page::Piece);PlaySfx(3);return;
        }
        if(!W::Capture(snapshot)){Unavailable("Change committed; inventory unavailable. Close Workshop.");return;}
        if(completed.op==workshop::Op::Fuse){
            if(completed.other>=200||snapshot.pieces[completed.other].id||snapshot.pieces[completed.other].native[2]){
                Unavailable("Fusion committed; donor removal could not be verified. Close Workshop.");return;
            }
            char text[144]{};_snprintf_s(text,sizeof(text),_TRUNCATE,"Fusion applied. Donor #%llu consumed. Save normally.",static_cast<unsigned long long>(completed.otherId));SetNotice(text);
            Log("[ffx-hooks] Workshop UI: fusion readback verified target=%llu donor=%llu consumed=1\n",static_cast<unsigned long long>(completed.pieceId),static_cast<unsigned long long>(completed.otherId));
        }else if(completed.op==workshop::Op::Refine&&winner){
            for(unsigned i=0;i<5;++i)if(snapshot.pieces[selectedSlot].abilities[i]==winner){char message[144]{};
                _snprintf_s(message,sizeof(message),_TRUNCATE,"Refined %s to +%u. Save normally.",N::Ability(workshop::Ability(snapshot.pieces[selectedSlot],i)),workshop::AbilityRank(snapshot.pieces[selectedSlot],i));SetNotice(message);break;}
        }else SetNotice("Applied. Save the game normally to keep the change.");
        ReturnToPiece();return;
    }
    switch(page){
    case Page::Mode:break;
    case Page::Fifth:NewRequest(workshop::Op::SetFifth);draft.value=static_cast<std::uint16_t>(row.value);Review();break;
    case Page::Models:NewRequest(workshop::Op::Reforge);std::memcpy(draft.gearTemplate,snapshot.pieces[row.value].native,22);draft.gearTemplate[6]=255;Review();break;
    case Page::Donor:draft.other=static_cast<std::uint16_t>(row.value);draft.otherId=snapshot.pieces[row.value].id;Navigate(Page::Source);break;
    case Page::Source:draft.from[transfer]=static_cast<unsigned char>(row.value);Navigate(Page::Destination);break;
    case Page::Destination:draft.to[transfer]=static_cast<unsigned char>(row.value);draft.count=static_cast<unsigned char>(transfer+1);if(!transfer)Navigate(Page::TransferCount);else Review();break;
    case Page::TransferCount:if(row.action==Action::Second){transfer=1;Navigate(Page::Source);}else Review();break;
    case Page::Expand:NewRequest(workshop::Op::Expand);draft.value=static_cast<std::uint16_t>(row.value&255);draft.policy=static_cast<unsigned char>(row.value>>8);Review();break;
    case Page::Clear:NewRequest(workshop::Op::Clear);draft.value=static_cast<std::uint16_t>(row.value);Review();break;
    case Page::Evolve:{NewRequest(workshop::Op::Evolve);draft.to[0]=static_cast<unsigned char>(row.value);const unsigned id=workshop::Ability(p,row.value)-0x8000;draft.value=static_cast<std::uint16_t>(0x8000+(id>=98?id+1:id-1));Review();break;}
    default:break;
    }
}
static int __cdecl Input(int obj){
    if(!F7IsForegroundWindow()){InterlockedExchange(&wantClose,1);return obj;}
    if(result!=-2)return obj;
    const int previousSelection=RdW(obj,O_SELECTED),previousTop=RdW(obj,O_TOP);
    const auto mouse=F7ListMouseTick(obj,NX(.06f),NY(.24f),NW(.40f),NH(.066f),NH(.058f),count,8);
    const int dir=FfxHooks::F7Ui::ResolveDirectionalInput(PadDir(),mouse.ownsDirectionalFrame),edge=PadEdge();
    int sel=RdW(obj,O_SELECTED),top=RdW(obj,O_TOP);if(delay)--delay;
    if(count){if(dir&0x1000)sel=(sel+count-1)%count;else if(dir&0x4000)sel=(sel+1)%count;}
    if(sel<top)top=sel;if(sel>=top+8)top=sel-7;top=(std::max)(0,(std::min)(top,count-8));
    WrW(obj,O_SELECTED,static_cast<std::int16_t>(sel));WrW(obj,O_TOP,static_cast<std::int16_t>(top));
    if(!delay && (((edge&0x20)&&!(lastEdge&0x20))||mouse.confirm)){result=sel;delay=10;}
    else if(!delay&&(edge&0x40)&&!(lastEdge&0x40)){result=-1;delay=10;}
    // Confirm/back feedback belongs to the consumed action in Tick/Choose.
    // Do not also play navigation when a click selects and confirms one row.
    else if(sel!=previousSelection||top!=previousTop)PlaySfx(1);
    lastEdge=edge&0x60;return obj;
}
#include "EquipmentWorkshopDraw.inl"
static int __cdecl Aux(int){return 1;}
static bool Open(){
    if(menu.obj||closing||!F7IsForegroundWindow())return false;notice[0]=0;selectedSlot=-1;selectedIdentity=0;parentCount=0;draft={};preview={};transfer=0;
    const bool loaded=W::Capture(snapshot);const auto access=loaded?W::Access():workshop::Error::InvalidState;
    if(!loaded)SetNotice(W::Detail());else if(access!=workshop::Error::Ok)SetNotice(workshop::Message(access));
    Build(loaded&&access==workshop::Error::Ok?Page::Inventory:Page::Info);
    const int obj=Alloc();if(!obj)return false;
    menu={obj};Geometry(obj);WrB(obj,O_SLOTS,1);WrB(obj,O_CANCEL,1);WrB(obj,O_GROUP62,2);WrB(obj,O_GROUP63,1);
    WrP(obj,O_ENTER,nullptr);WrP(obj,O_UPDATE,reinterpret_cast<void*>(&Input));WrP(obj,O_DRAW,reinterpret_cast<void*>(&Draw));WrP(obj,O_AUX,reinterpret_cast<void*>(&Aux));WrP(obj,O_VALIDATOR,nullptr);
    InterlockedExchange(&drawSeen,0);
    Register(obj);ClaimModal(obj);menuThread=GetCurrentThreadId();F7AcquireCursorOwnership();cursorOwned=true;InterlockedExchange(&owned,1);
    Log("[ffx-hooks] Workshop UI: opened obj=0x%08X rows=%d inventory=%d\n",static_cast<unsigned>(obj),count,loaded?1:0);return true;
}
static void Release(){const bool hadOwner=InterlockedExchange(&owned,0)!=0;if(cursorOwned){F7ReleaseCursorOwnership();cursorOwned=false;}if(hadOwner)Log("[ffx-hooks] Workshop UI: modal and cursor released\n");}
static void Close(){
    if(menu.obj&&StillOwned(menu.obj)){ReleaseModalIfOwned(menu.obj);WrB(menu.obj,65,1);closing=menu.obj;closePasses=0;}
    menu.obj=0;result=-2;if(!closing)Release();
}
static void Tick(){
    if(InterlockedExchange(&wantClose,0))Close();
    if(closing){if(!StillOwned(closing)){closing=0;Release();}else if(++closePasses>=3){Reset(closing);closing=0;Release();}return;}
    if(!menu.obj)return;
    if(!StillOwned(menu.obj)){menu.obj=0;Release();return;}
    if(result!=-2){const int action=result;result=-2;if(action<0){if(page==Page::Inventory||page==Page::Info){PlaySfx(4);Close();}else{const Row back{};Choose(back);}}else if(action<count){const Row chosen=rows[action];Choose(chosen);}}
}
static bool StopReady(){
    if(!Active())return true;
    if(menuThread!=GetCurrentThreadId()){InterlockedExchange(&wantClose,1);return false;}
    Close();if(closing&&StillOwned(closing))Reset(closing);closing=0;Release();return true;
}
}
