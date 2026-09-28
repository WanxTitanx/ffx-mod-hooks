// Private views for shared comparison, Items and Status consumers.
int __cdecl FrameShim(float x,float y,float width,float height,int style){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module;
    if(Enabled()&&scope->admitted&&scope->piece.fifthUnlocked&&
       ((scope->kind==Shared&&caller==0x4D8ACE)||(scope->kind==Inventory&&(caller==0x4BD1AA||caller==0x4BD205)))){
        if(!scope->hasOrigin){scope->originY=y;scope->hasOrigin=true;}
        y=scope->originY+(y-scope->originY)*.8f;height*=.8f;
    }
    return reinterpret_cast<FrameFn>(originals[Frame])(x,y,width,height,style);
}
int __cdecl SharedShim(float x,float y,const unsigned char* gear){
    if(!active.load())return reinterpret_cast<SharedFn>(originals[Shared])(x,y,gear);
    Scope current{};current.kind=Shared;current.originY=y;current.hasOrigin=true;
    auto* previous=scope;current.depth=previous?previous->depth+1:0;scope=&current;int result=0;
    if(Enabled()&&ReadPresentation(gear,current.piece)&&Presentation::BuildGearView(current.piece,current.gear))current.admitted=true;
    __try{
        result=reinterpret_cast<SharedFn>(originals[Shared])(x,y,current.admitted?current.gear.bytes:gear);
        if(Enabled()&&current.admitted&&current.piece.fifthUnlocked&&current.piece.fifth!=workshop::Empty){
            current.nameSlot=4;
            reinterpret_cast<RowFn>(originals[AbilityRow])(current.piece.fifth,x,y+Y(4*68.f*.8f-2.f),0);
        }
    }__finally{scope=previous;}
    return result;
}
int __cdecl InventoryShim(unsigned offset){
    if(!active.load())return reinterpret_cast<CustomizeFn>(originals[Inventory])(offset);
    Scope current{};current.kind=Inventory;auto* previous=scope;current.depth=previous?previous->depth+1:0;scope=&current;int result=0;
    __try{result=reinterpret_cast<CustomizeFn>(originals[Inventory])(offset);}
    __finally{scope=previous;}
    return result;
}
int __cdecl StatusShim(void* object){
    if(!active.load()){
        const int result=reinterpret_cast<StatusFn>(originals[StatusPage])(object);
        // The pinned bridge may still serve an independently enabled Arcana.
        if(auto observer=statusObserver.load())observer(object,8);
        return result;
    }
    Scope current{};current.kind=StatusPage;auto* previous=scope;current.depth=previous?previous->depth+1:0;scope=&current;int result=0;
    __try{
        result=reinterpret_cast<StatusFn>(originals[StatusPage])(object);
        if(auto observer=statusObserver.load())observer(object,current.statusCount?current.statusCount:8);
    }
    __finally{scope=previous;}
    return result;
}
int __cdecl StatusListShim(int page,int mode,int category,int character,unsigned* output){
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-module;
    const int count=reinterpret_cast<ListFn>(originals[StatusList])(page,mode,category,character,output);
    if(!Enabled()||scope->kind!=StatusPage||caller!=0x4D27DC||page!=0||mode==1||character<0||character>6||count!=8||!output)return count;
    workshop::Piece pieces[2]{};unsigned nativeWords[8]{},words[10]{},ranks[10]{},nativeCount=0,total=0,extra=0;
    bool found=false;
    for(unsigned kind=0;kind<2;++kind){
        const unsigned id=reinterpret_cast<CharacterGearFn>(module+(kind?0x4A97D0:0x4A9C20))(character);
        if(id==255)continue;
        const unsigned char* text=nullptr;
        const auto* record=reinterpret_cast<GetGearFn>(module+0x3ABBF0)(id,&text);
        auto& piece=pieces[kind];
        if(!ReadPresentation(record,piece)||piece.native[4]!=character||piece.native[6]!=character||piece.native[5]!=kind)return count;
        found=true;extra+=piece.fifthUnlocked;
        for(unsigned slot=0;slot<4u+piece.fifthUnlocked;++slot){const unsigned word=workshop::Ability(piece,slot);if(word==workshop::Empty)continue;
            if(slot<4)nativeWords[nativeCount++]=word;
            if(total>=10)return count;
            words[total]=word;ranks[total++]=workshop::AbilityRank(piece,slot);
        }
    }
    unsigned actual[8]{};
    if(!found||!NativeUiSupport::Copy(actual,output,sizeof(actual))||std::memcmp(actual,nativeWords,sizeof(actual)))return count;
    // The proven Status caller owns 22 DWORDs; its original list uses eight.
    const unsigned capacity=8+extra;
    if(capacity>10||!NativeUiSupport::Copy(output,words,capacity*sizeof(unsigned)))return count;
    std::memcpy(scope->statusWords,words,sizeof(words));std::memcpy(scope->statusRanks,ranks,sizeof(ranks));
    scope->statusCount=capacity;scope->admitted=true;return static_cast<int>(capacity);
}
