// Included by dllmain after the shared native input/drawing primitives. This menu
// owns its own object, state and close drain; it is not an F7/F8/F9 submenu.
#include "EquipmentWorkshopNames.h"
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
static unsigned transfer=0;
static std::uint64_t selectedIdentity=0;
static bool cursorOwned=false;
static DWORD menuThread=0;
static char notice[144]{};
static float eased=-1;
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
    unsigned rank=p.rank;if(p.mode==2){rank=0;for(auto value:p.ranks)rank+=value;}
    _snprintf_s(out,capacity,_TRUNCATE,"%s %s #%llu +%u%s",Owner(p),p.native[5]?"Armor":"Weapon",static_cast<unsigned long long>(p.id),rank,p.native[6]!=255?" [Equipped]":"");
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
        Add(Action::Refine,0,"Refine this piece");Add(Action::Mode,0,piece->mode==1?"Refinement: A - whole piece":piece->mode==2?"Refinement: B - one ability":"Choose refinement A / B");
        Add(piece->fifthUnlocked?Action::Fifth:Action::Unlock,0,piece->fifthUnlocked?"Choose fifth ability":"Unlock fifth ability");
        Add(Action::Reforge,0,"Reforge owner / type");Add(Action::Fuse,0,"Fuse equipment");Add(Action::Expand,0,"Expand original slots");
        Add(Action::Clear,0,"Remove an ability");Add(Action::Evolve,0,"Evolve an ability");Add(Action::Back,0,"Equipment list");break;
    case Page::Mode:Add(Action::Value,1,"A: improve every ability (+10)");Add(Action::Value,2,"B: improve one eligible ability (+40 / +50)");Add(Action::Back,0,"Back");break;
    case Page::Fifth:
        for(unsigned id=0;id<131;++id)if(workshop::SupportedRefinement(static_cast<std::uint16_t>(0x8000+id)))Add(Action::Value,0x8000+id,N::Ability(0x8000+id));Add(Action::Back,0,"Back");break;
    case Page::Models:
        for(unsigned i=0;i<200;++i){const auto& p=snapshot.pieces[i];if(!Regular(p)||(p.native[3]&12))continue;
            bool duplicate=false;for(unsigned j=0;j<i;++j){const auto& q=snapshot.pieces[j];if(Regular(q)&&!(q.native[3]&12)&&q.native[4]==p.native[4]&&q.native[5]==p.native[5]&&q.native[12]==p.native[12]&&q.native[13]==p.native[13]){duplicate=true;break;}}
            if(!duplicate){_snprintf_s(line,sizeof(line),_TRUNCATE,"%s %s - look %u",Owner(p),p.native[5]?"Armor":"Weapon",i+1);Add(Action::Value,i,line);}}
        Add(Action::Back,0,"Back");break;
    case Page::Donor:
        for(unsigned i=0;i<200;++i){const auto& p=snapshot.pieces[i];if(i==static_cast<unsigned>(selectedSlot)||!Regular(p)||(p.native[3]&12)||p.native[6]!=255||!piece||p.mode!=piece->mode)continue;PieceLabel(p,line,sizeof(line));Add(Action::Value,i,line);}
        Add(Action::Back,0,"Back");break;
    case Page::Source:{const auto& p=snapshot.pieces[draft.other];
        for(unsigned i=0;i<4u+p.fifthUnlocked;++i){const auto id=workshop::Ability(p,i);if(id==255||(transfer&&i==draft.from[0]))continue;_snprintf_s(line,sizeof(line),_TRUNCATE,"Slot %u: %s",i+1,N::Ability(id));Add(Action::Value,i,line);}Add(Action::Back,0,"Back");break;}
    case Page::Destination:
        if(piece)for(unsigned i=0;i<piece->native[11]+piece->fifthUnlocked;++i){if(transfer&&i==draft.to[0])continue;_snprintf_s(line,sizeof(line),_TRUNCATE,"Replace slot %u: %s",i+1,N::Ability(workshop::Ability(*piece,i)));Add(Action::Value,i,line);}Add(Action::Back,0,"Back");break;
    case Page::TransferCount:Add(Action::Value,0,"Review fusion with one ability");Add(Action::Second,0,"Transfer a second ability");Add(Action::Back,0,"Cancel fusion");break;
    case Page::Expand:
        if(piece)for(unsigned cap=piece->native[11]+1;cap<=4;++cap)for(unsigned policy=0;policy<2;++policy){_snprintf_s(line,sizeof(line),_TRUNCATE,"%u slots - %s Key Sphere recipe",cap,policy?"1 / 2 / 3 / 4":"one each");Add(Action::Value,cap|(policy<<8),line);}Add(Action::Back,0,"Back");break;
    case Page::Clear:case Page::Evolve:
        if(piece)for(unsigned i=0;i<4u+piece->fifthUnlocked;++i){const unsigned word=workshop::Ability(*piece,i);if(word==255)continue;const unsigned id=word-0x8000;
            if(page==Page::Evolve && !((id>=98&&id<121&&(id-98)%4<3)||(id>=47&&id<=75&&(id-47)%4==0)))continue;
            _snprintf_s(line,sizeof(line),_TRUNCATE,"Slot %u: %s",i+1,N::Ability(word));Add(Action::Value,i,line);}Add(Action::Back,0,"Back");break;
    case Page::Confirm:Add(Action::Confirm,0,"Confirm this change");Add(Action::Back,0,"Keep as is");break;
    case Page::Info:Add(Action::Close,0,"Return to game");break;
    }
    if(menu.obj)Geometry(menu.obj);
}
static void SetNotice(const char* text){strncpy_s(notice,text,_TRUNCATE);}
static void NewRequest(workshop::Op op){draft={};draft.op=op;draft.slot=static_cast<std::uint16_t>(selectedSlot);draft.pieceId=snapshot.pieces[selectedSlot].id;draft.revision=snapshot.revision;}
static void Review(){
    const auto error=W::Preview(draft,preview);
    if(error!=workshop::Error::Ok){SetNotice(workshop::Message(error));Build(Page::Piece);return;}
    SetNotice(draft.op==workshop::Op::Fuse?"The donor is consumed. Only selected abilities transfer.":"Review the materials before confirming.");Build(Page::Confirm);
}
static void Choose(const Row& row){
    if(row.action==Action::Close){InterlockedExchange(&wantClose,1);return;}
    if(row.action==Action::Back){Build(page==Page::Piece?Page::Inventory:Page::Piece);return;}
    if(page==Page::Inventory){if(row.action==Action::Filter){ownerFilter=ownerFilter==6?-1:ownerFilter+1;Build(Page::Inventory);}else{selectedSlot=static_cast<int>(row.value);selectedIdentity=snapshot.pieces[selectedSlot].id;SetNotice("");Build(Page::Piece);}return;}
    const auto displayedRevision=snapshot.revision;
    if(!W::Capture(snapshot)){SetNotice(W::Detail());Build(Page::Info);return;}
    if(snapshot.revision!=displayedRevision){selectedSlot=-1;SetNotice("Inventory changed. Select the piece again.");Build(Page::Inventory);return;}
    if(selectedSlot<0||snapshot.pieces[selectedSlot].id!=selectedIdentity){selectedSlot=-1;SetNotice("The selected piece changed. Choose it again.");Build(Page::Inventory);return;}
    const auto& p=snapshot.pieces[selectedSlot];
    if(page==Page::Piece){
        SetNotice("");
        switch(row.action){
        case Action::Refine:if(!p.mode){Build(Page::Mode);return;}NewRequest(workshop::Op::Refine);Review();return;
        case Action::Mode:Build(Page::Mode);return;
        case Action::Unlock:NewRequest(workshop::Op::UnlockFifth);Review();return;
        case Action::Fifth:Build(Page::Fifth);return;
        case Action::Reforge:Build(Page::Models);return;
        case Action::Fuse:NewRequest(workshop::Op::Fuse);transfer=0;Build(Page::Donor);return;
        case Action::Expand:Build(Page::Expand);return;
        case Action::Clear:Build(Page::Clear);return;
        case Action::Evolve:Build(Page::Evolve);return;
        default:return;
        }
    }
    if(page==Page::Confirm){
        if(W::Commit(draft,preview)){W::Capture(snapshot);SetNotice("Applied. Save the game normally to keep the change.");PlaySfx(1);}
        else{SetNotice("The inventory changed. Review a new preview.");PlaySfx(3);}
        Build(Page::Piece);return;
    }
    switch(page){
    case Page::Mode:NewRequest(workshop::Op::Mode);draft.value=static_cast<std::uint16_t>(row.value);Review();break;
    case Page::Fifth:NewRequest(workshop::Op::SetFifth);draft.value=static_cast<std::uint16_t>(row.value);Review();break;
    case Page::Models:NewRequest(workshop::Op::Reforge);std::memcpy(draft.gearTemplate,snapshot.pieces[row.value].native,22);draft.gearTemplate[6]=255;Review();break;
    case Page::Donor:draft.other=static_cast<std::uint16_t>(row.value);draft.otherId=snapshot.pieces[row.value].id;Build(Page::Source);break;
    case Page::Source:draft.from[transfer]=static_cast<unsigned char>(row.value);Build(Page::Destination);break;
    case Page::Destination:draft.to[transfer]=static_cast<unsigned char>(row.value);draft.count=static_cast<unsigned char>(transfer+1);if(!transfer)Build(Page::TransferCount);else Review();break;
    case Page::TransferCount:if(row.action==Action::Second){transfer=1;Build(Page::Source);}else Review();break;
    case Page::Expand:NewRequest(workshop::Op::Expand);draft.value=static_cast<std::uint16_t>(row.value&255);draft.policy=static_cast<unsigned char>(row.value>>8);Review();break;
    case Page::Clear:NewRequest(workshop::Op::Clear);draft.value=static_cast<std::uint16_t>(row.value);Review();break;
    case Page::Evolve:{NewRequest(workshop::Op::Evolve);draft.to[0]=static_cast<unsigned char>(row.value);const unsigned id=workshop::Ability(p,row.value)-0x8000;draft.value=static_cast<std::uint16_t>(0x8000+(id>=98?id+1:id-1));Review();break;}
    default:break;
    }
}
static int __cdecl Input(int obj){
    if(!F7IsForegroundWindow()){InterlockedExchange(&wantClose,1);return obj;}
    if(result!=-2)return obj;
    const auto mouse=F7ListMouseTick(obj,NX(.06f),NY(.24f),NW(.40f),NH(.066f),NH(.058f),count,8);
    const int dir=FfxHooks::F7Ui::ResolveDirectionalInput(PadDir(),mouse.ownsDirectionalFrame),edge=PadEdge();
    int sel=RdW(obj,O_SELECTED),top=RdW(obj,O_TOP);if(delay)--delay;
    if(count){if(dir&0x1000)sel=(sel+count-1)%count;else if(dir&0x4000)sel=(sel+1)%count;}
    if(sel<top)top=sel;if(sel>=top+8)top=sel-7;top=(std::max)(0,(std::min)(top,count-8));
    WrW(obj,O_SELECTED,static_cast<std::int16_t>(sel));WrW(obj,O_TOP,static_cast<std::int16_t>(top));
    if(!delay && (((edge&0x20)&&!(lastEdge&0x20))||mouse.confirm)){result=sel;delay=10;}
    else if(!delay&&(edge&0x40)&&!(lastEdge&0x40)){result=-1;delay=10;}
    lastEdge=edge&0x60;return obj;
}
static int __cdecl Draw(int obj){
    if(InterlockedCompareExchange(&drawSeen,1,0)==0)Log("[ffx-hooks] Workshop UI: first native draw obj=0x%08X\n",static_cast<unsigned>(obj));
    static int frame=0;const int f=++frame,selection=RdW(obj,O_SELECTED),top=RdW(obj,O_TOP);
    auto text=[](const char* value,float x,float y,bool smallFont=true){unsigned char bytes[192]{};EncodeLabel(value,bytes,sizeof(bytes));if(smallFont)DrawStringSub(bytes,x,y);else DrawString(bytes,x,y);};
    DrawMenuBackdrop();DrawMenuNeonFrame(f);DrawMenuGlassPanel(NX(.045f),NY(.055f),NW(.91f),NH(.13f),f,0);
    text("Equipment Workshop",NX(.07f),NY(.082f),false);text("Reforge - refine - build a fifth ability",NX(.07f),NY(.14f));
    const float left=NX(.06f),y=NY(.24f),width=NW(.40f),step=NH(.066f),height=NH(.058f);
    BOOL animations=FALSE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
    const bool motion=animations&&!EnvFlagEnabled("FFXHOOKS_REDUCED_MOTION");
    eased=FfxHooks::F8Ui::ResolveSelectionRowY(!motion,eased,y+(selection-top)*step);
    for(int visible=0;visible<8 && top+visible<count;++visible){const int row=top+visible;const float at=y+visible*step;
        const unsigned tint=page==Page::Confirm?0x40334D49u:0x40354054u;DrawSolidRect(left,at,width,height,tint,tint-0x18000000u);
        if(row==selection){const unsigned alpha=motion?0x44u+static_cast<unsigned>(Osc01(f,44)*32.f):0x58u;DrawSolidRect(left,eased,width,height,(alpha<<24)|0x00305068u,(alpha<<24)|0x00182038u);DrawSolidRect(left,eased+height-MenuBorderPx()*.45f,width,MenuBorderPx()*.45f,kMenuNeonGreenLine,kMenuNeonGreenLineLo);DrawCursor(left-NW(.028f),eased+NH(.002f));}
        text(rows[row].label,left+NW(.013f),at+NH(.017f));
    }
    DrawMenuGlassPanel(NX(.485f),NY(.22f),NW(.46f),NH(.58f),f,1);char line[160]{};
    const bool hiddenRoll=page==Page::Confirm&&draft.op==workshop::Op::Refine&&selectedSlot>=0&&snapshot.pieces[selectedSlot].mode==2;
    const workshop::Piece* p=selectedSlot>=0?((page==Page::Confirm&&!hiddenRoll)?&preview.after.pieces[selectedSlot]:&snapshot.pieces[selectedSlot]):nullptr;
    if(page==Page::Inventory && selection>=0&&selection<count&&rows[selection].action==Action::Piece)p=&snapshot.pieces[rows[selection].value];
    if(p&&p->id){PieceLabel(*p,line,sizeof(line));text(line,NX(.505f),NY(.252f));
        for(unsigned i=0;i<5;++i){const bool open=i<p->native[11]||(i==4&&p->fifthUnlocked);const auto word=workshop::Ability(*p,i);const unsigned rank=p->mode==1&&word!=255?p->rank:p->ranks[i];
            if(open&&word!=255)_snprintf_s(line,sizeof(line),_TRUNCATE,"%u: %s  +%u",i+1,N::Ability(word),rank);else _snprintf_s(line,sizeof(line),_TRUNCATE,"%u: %s",i+1,open?"Empty":"Locked");text(line,NX(.505f),NY(.31f+i*.052f));}
    }else text(W::Detail(),NX(.505f),NY(.255f));
    if(page==Page::Confirm){unsigned row=0;for(unsigned item=0;item<112 && row<6;++item)if(preview.costs[item]){_snprintf_s(line,sizeof(line),_TRUNCATE,"%s x%u  (have %u)",N::Item(item),preview.costs[item],snapshot.items[item]);text(line,NX(.505f),NY(.58f+row*.03f));++row;}if(!row)text("No materials required",NX(.505f),NY(.63f));}
    else if(page==Page::Mode){text("A: every occupied ability gains a rank.",NX(.505f),NY(.63f));text("B: one eligible ability gains a rank.",NX(.505f),NY(.68f));}
    else{text("Edit an unequipped piece.",NX(.505f),NY(.64f));text("Save normally to keep your changes.",NX(.505f),NY(.69f));}
    if(notice[0])text(notice,NX(.06f),NY(.84f));
    if(count>8){_snprintf_s(line,sizeof(line),_TRUNCATE,"%d-%d of %d",top+1,(std::min)(top+8,count),count);text(line,NX(.06f),NY(.79f));}
    DrawMenuGlassPanel(NX(.045f),NY(.905f),NW(.91f),NH(.065f),f,0);text("Arrows / scroll: navigate    Confirm: select    Back: return",NX(.07f),NY(.923f));return obj;
}
static int __cdecl Aux(int){return 1;}
static bool Open(){
    if(menu.obj||closing||!F7IsForegroundWindow())return false;notice[0]=0;selectedSlot=-1;selectedIdentity=0;
    const bool loaded=W::Capture(snapshot);if(!loaded)SetNotice(W::Detail());Build(loaded?Page::Inventory:Page::Info);
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
    if(result!=-2){const int action=result;result=-2;if(action<0){if(page==Page::Inventory||page==Page::Info)Close();else Build(page==Page::Piece?Page::Inventory:Page::Piece);}else if(action<count){const Row chosen=rows[action];Choose(chosen);}}
}
static bool StopReady(){
    if(!Active())return true;
    if(menuThread!=GetCurrentThreadId()){InterlockedExchange(&wantClose,1);return false;}
    Close();if(closing&&StillOwned(closing))Reset(closing);closing=0;Release();return true;
}
}
