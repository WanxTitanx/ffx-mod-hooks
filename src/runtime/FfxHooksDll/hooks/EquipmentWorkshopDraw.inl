// Included inside EquipmentMenu. Selection is presentation-only: browsing a
// donor/model never assigns selectedSlot or changes the paid transaction draft.
static const workshop::Piece* VisiblePiece(unsigned slot){
    return slot<200&&snapshot.pieces[slot].id?&snapshot.pieces[slot]:nullptr;
}
static int __cdecl Draw(int obj){
    (void)W::CatalogBridge::Read(catalog);
    if(InterlockedCompareExchange(&drawSeen,1,0)==0)Log("[ffx-hooks] Workshop UI: first native draw obj=0x%08X\n",static_cast<unsigned>(obj));
    static int frame=0;const int f=++frame,selection=RdW(obj,O_SELECTED),top=RdW(obj,O_TOP);
    auto text=[](const char* value,float x,float y,bool compact=true){unsigned char bytes[192]{};EncodeLabel(value,bytes,sizeof(bytes));if(compact)DrawStringSub(bytes,x,y);else DrawString(bytes,x,y);};
    DrawMenuBackdrop();DrawMenuNeonFrame(f);DrawMenuGlassPanel(NX(.045f),NY(.055f),NW(.91f),NH(.13f),f,0);
    text("Equipment Workshop",NX(.07f),NY(.082f),false);
    text("Refine - compare - fuse     Refinement mode: F8 > Dev",NX(.07f),NY(.14f));
    text(W::NativeUi::Active()?"Native detail hook: ON - requires a loaded Workshop save.":
         "Native detail hook: OFF - F8 > Reforge > Native equipment details; restart.",NX(.07f),NY(.20f));
    const float left=NX(.06f),y=NY(.24f),width=NW(.40f),step=NH(.066f),height=NH(.058f);
    BOOL animations=FALSE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);
    const bool motion=animations&&!EnvFlagEnabled("FFXHOOKS_REDUCED_MOTION");
    eased=FfxHooks::F8Ui::ResolveSelectionRowY(!motion,eased,y+(selection-top)*step);
    for(int visible=0;visible<8&&top+visible<count;++visible){const int row=top+visible;const float at=y+visible*step;
        const unsigned tint=page==Page::Confirm?0x40334D49u:0x40354054u;DrawSolidRect(left,at,width,height,tint,tint-0x18000000u);
        if(row==selection){const unsigned alpha=motion?0x44u+static_cast<unsigned>(Osc01(f,44)*32.f):0x58u;
            DrawSolidRect(left,eased,width,height,(alpha<<24)|0x00305068u,(alpha<<24)|0x00182038u);
            DrawSolidRect(left,eased+height-MenuBorderPx()*.45f,width,MenuBorderPx()*.45f,kMenuNeonGreenLine,kMenuNeonGreenLineLo);
            DrawCursor(left-NW(.028f),eased+NH(.002f));}
        text(rows[row].label,left+NW(.013f),at+NH(.017f));
    }
    const Row* hover=selection>=0&&selection<count?&rows[selection]:nullptr;
    const auto* target=selectedSlot>=0?VisiblePiece(static_cast<unsigned>(selectedSlot)):nullptr;
    const workshop::Piece* candidate=nullptr;
    const char* targetTitle="Target equipment";const char* candidateTitle="Candidate equipment";
    int targetHighlight=-1,candidateHighlight=-1;
    const bool random=page==Page::Confirm&&draft.op==workshop::Op::Refine&&preview.policy.mode==2;
    if(page==Page::Inventory){
        targetTitle="Selected equipment";
        if(hover&&hover->action==Action::Piece)candidate=VisiblePiece(hover->value);
    }else if(page==Page::Donor||page==Page::Models){
        if(hover&&hover->action==Action::Value)candidate=VisiblePiece(hover->value);
        candidateTitle=page==Page::Donor?"Donor - consumed on fusion":"Model reference - appearance only";
    }else if(page==Page::Source||page==Page::Destination||page==Page::TransferCount){
        candidate=VisiblePiece(draft.other);candidateTitle="Donor - selected abilities transfer";
        if(page==Page::Source&&hover&&hover->action==Action::Value)candidateHighlight=static_cast<int>(hover->value);
        else candidateHighlight=draft.from[transfer];
        if(page==Page::Destination&&hover&&hover->action==Action::Value)targetHighlight=static_cast<int>(hover->value);
    }else if(page==Page::Confirm){
        if(draft.op==workshop::Op::Fuse){candidate=VisiblePiece(draft.other);candidateTitle="Donor - destroyed after confirmation";}
        else if(!random&&reviewError==workshop::Error::Ok&&selectedSlot>=0){candidate=&preview.after.pieces[selectedSlot];targetTitle="Target before";candidateTitle="After confirmation";}
    }
    char line[160]{};
    auto card=[&](const workshop::Piece* piece,const char* title,float at,int highlight){
        DrawMenuGlassPanel(NX(.485f),NY(at),NW(.46f),NH(.275f),f,1);
        text(title,NX(.502f),NY(at+.012f));
        if(!piece){text("Select an equipment to compare.",NX(.505f),NY(at+.065f));return;}
        PieceLabel(*piece,line,sizeof(line));text(line,NX(.505f),NY(at+.043f));
        for(unsigned i=0;i<5;++i){const bool open=i<piece->native[11]||(i==4&&piece->fifthUnlocked);const auto word=workshop::Ability(*piece,i);
            unsigned effect=2;
            if(workshop::ProtectedAbility(*piece,i))_snprintf_s(line,sizeof(line),_TRUNCATE,"%u: Aeon Immunity [Permanent]",i+1);
            else if(open&&UpgradeWord(word,effect))_snprintf_s(line,sizeof(line),_TRUNCATE,"%u: %s [Cap]",i+1,UpgradeName(effect));
            else if(open&&word!=255)_snprintf_s(line,sizeof(line),_TRUNCATE,workshop::GenericRefinement(word,&catalog)?"%s%u: %.28s +%u%% STR/MAG":"%s%u: %.28s +%u",highlight==static_cast<int>(i)?"> ":"",i+1,N::Ability(word),workshop::AbilityRank(*piece,i));
            else _snprintf_s(line,sizeof(line),_TRUNCATE,"%u: %s",i+1,open?"Empty":"Locked");
            text(line,NX(.505f),NY(at+.083f+i*.035f));
        }
    };
    if(target&&candidate){card(target,targetTitle,.22f,targetHighlight);card(candidate,candidateTitle,.51f,candidateHighlight);}
    else if(target||candidate){card(target?target:candidate,target?targetTitle:candidateTitle,.22f,target?targetHighlight:candidateHighlight);
        DrawMenuGlassPanel(NX(.485f),NY(.51f),NW(.46f),NH(.275f),f,1);
        if(random){
            text("Eligible outcomes - not rolled yet",NX(.505f),NY(.525f));unsigned row=0;
            for(unsigned i=0;i<5&&target;++i){const auto word=workshop::Ability(*target,i);const auto rank=workshop::AbilityRank(*target,i);unsigned item=0,quantity=0;
                if(word==255||rank==10||workshop::ProtectedAbility(*target,i)||!workshop::RefinementCost(word,rank+1,preview.policy,item,quantity,&preview.catalog))continue;
                _snprintf_s(line,sizeof(line),_TRUNCATE,"%.25s +%u: %.20s x%u",N::Ability(word),rank+1,N::Item(item),quantity);
                text(line,NX(.505f),NY(.568f+row*.037f));++row;
            }
        }else{
            workshop::Policy policy{};const bool valid=W::Settings::Read(policy);
            text(valid?W::Settings::ModeName(policy.mode):"Invalid Workshop settings",NX(.505f),NY(.545f));
            text("Choose A / B in F8 > Dev.",NX(.505f),NY(.595f));
            text(workshop::IsAeon(*(target?target:candidate))?"Aeon equipment: Gil x2. Save normally.":"Unequip before editing. Save normally.",NX(.505f),NY(.645f));
            if(page==Page::Donor||page==Page::Models)text("Highlight a candidate to compare it.",NX(.505f),NY(.695f));
        }
    }else{DrawMenuGlassPanel(NX(.485f),NY(.22f),NW(.46f),NH(.565f),f,1);text(page==Page::Info&&notice[0]?notice:W::Detail(),NX(.505f),NY(.26f));}
    if(page==Page::Confirm){
        bool modRecipe=false;
        auto includeRecipe=[&](std::uint16_t word){unsigned item=0,quantity=0;bool native=true;
            if(workshop::CustomizeCost(word,preview.policy,item,quantity,native,&preview.catalog)&&!native)modRecipe=true;
        };
        // Inspect all eligible outcomes, never the hidden winner. Fusion prices
        // only the selected donor abilities, not everything on the donor.
        if(draft.op==workshop::Op::Refine&&target){
            for(unsigned i=0;i<5;++i)if(!workshop::ProtectedAbility(*target,i)&&workshop::AbilityRank(*target,i)<10)includeRecipe(workshop::Ability(*target,i));
        }else if(draft.op==workshop::Op::Fuse){
            const auto* donor=VisiblePiece(draft.other);
            if(donor)for(unsigned i=0;i<draft.count&&i<2;++i)includeRecipe(workshop::Ability(*donor,draft.from[i]));
        }else if(draft.op==workshop::Op::SetFifth)includeRecipe(draft.value);
        DrawMenuGlassPanel(NX(.055f),NY(.405f),NW(.41f),NH(.355f),f,1);
        text(random?"Prerequisites - all outcomes covered":"Materials charged on confirmation",NX(.07f),NY(.423f));
        if(upgradeFlow&&reviewError!=workshop::Error::Ok){_snprintf_s(line,sizeof(line),_TRUNCATE,"Gil required: %u",preview.gilCost);text(line,NX(.07f),NY(.461f));}
        else if(preview.gilCost){_snprintf_s(line,sizeof(line),_TRUNCATE,"Gil: %u / %u%s",preview.gilBefore,preview.gilCost,preview.policy.devFreeGil?" DEV - no debit":preview.gilBefore<preview.gilCost?" MISSING":"");text(line,NX(.07f),NY(.461f));}
        else text(random?"Owned / required (not all are consumed)":"Owned / required",NX(.07f),NY(.461f));
        unsigned row=0;
        for(unsigned item=0;item<112;++item){const unsigned required=random?preview.requirements[item]:preview.costs[item];if(!required)continue;
            _snprintf_s(line,sizeof(line),_TRUNCATE,"%.24s: %u / %u%s",N::Item(item),snapshot.items[item],required,preview.policy.devFreeMaterials?" DEV":snapshot.items[item]<required?" MISSING":"");
            text(line,NX(.07f),NY(.503f+row*.035f));++row;
        }
        if(!row)text("No materials required",NX(.07f),NY(.503f));
        if(modRecipe)text("Includes a mod-only recipe.",NX(.07f),NY(.72f));
    }
    if(notice[0])text(notice,NX(.06f),NY(.84f));
    if(count>8){_snprintf_s(line,sizeof(line),_TRUNCATE,"%d-%d of %d",top+1,(std::min)(top+8,count),count);text(line,NX(.06f),NY(.79f));}
    DrawMenuGlassPanel(NX(.045f),NY(.905f),NW(.91f),NH(.065f),f,0);
    text("Arrows / scroll: navigate    Confirm: select    Back: return",NX(.07f),NY(.923f));return obj;
}
