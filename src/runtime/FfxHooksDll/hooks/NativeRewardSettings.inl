// The existing native settings stack owns these pages and their staged edits.
static unsigned g_rewardSpecies=1,g_rewardIdDraft=1,g_rewardRateDraft=1;
static FfxHooks::MonsterRewards::Kind g_rewardKind=FfxHooks::MonsterRewards::Kind::Ap;
static bool g_rewardGlobal=false;
static const FfxHooks::F8FlagSpec* F8RewardFlag(FfxHooks::MonsterRewards::Kind kind){
    return FfxHooks::FindF8Flag(kind==FfxHooks::MonsterRewards::Kind::Ap?"cheats.ap_100x":"cheats.gil_100x");
}
static int F8RewardCount(NativeSettingsPage page){
    switch(page){case NativeSettingsPage::RewardMultipliers:return 8;
        case NativeSettingsPage::MonsterRewardList:return static_cast<int>(FfxHooks::MonsterRewards::Count())+1;
        case NativeSettingsPage::MonsterRewardDetail:return 10;
        case NativeSettingsPage::RewardRate:return 8;
        case NativeSettingsPage::MonsterRewardId:return 6;default:return 0;}
}
static const char* F8RewardTitle(NativeSettingsPage page){
    switch(page){case NativeSettingsPage::RewardMultipliers:return "AP/Gil Multipliers";
        case NativeSettingsPage::MonsterRewardList:return "Per-monster rewards";
        case NativeSettingsPage::MonsterRewardDetail:return "Monster reward preview";
        case NativeSettingsPage::RewardRate:return g_rewardKind==FfxHooks::MonsterRewards::Kind::Ap?"AP multiplier":"Gil multiplier";
        case NativeSettingsPage::MonsterRewardId:return "Find monster by ID";default:return "Rewards";}
}
static const char* F8RewardHelp(NativeSettingsPage page){
    if(page==NativeSettingsPage::MonsterRewardDetail)return "Original > individual > after global. Native bonuses follow. * = capped.";
    if(page==NativeSettingsPage::RewardRate)return g_rewardGlobal?"Global rate: 1-100. Save preserves the enable switch.":"Monster rate: 1-1000. AP also applies to Overkill AP. Cancel discards.";
    if(page==NativeSettingsPage::MonsterRewardId)return "Select the monster file ID (m000-m4095), then open its settings.";
    if(page==NativeSettingsPage::MonsterRewardList)return "Choose one monster. Duplicate actors of that monster share its rate.";
    return "Global and individual rates are independent. Per-monster mode needs a restart.";
}
static void F8RewardLabel(NativeSettingsPage page,int row,char* out,size_t capacity){
    using namespace FfxHooks;using namespace MonsterRewards;
    if(page==NativeSettingsPage::RewardMultipliers){
        if(row==0||row==2||row==4){const auto* flag=row==4?FindF8Flag("cheats.monster_rewards"):F8RewardFlag(row==0?Kind::Ap:Kind::Gil);
            if(!flag){F8Copy(out,capacity,"Control unavailable");return;}
            F8Format(out,capacity,"%s: %s",row==0?"Global AP":row==2?"Global Gil":"Per-monster mode",ResolveF8Flag(*flag).value?"ON":"OFF");return;}
        if(row==1||row==3){const auto* flag=F8RewardFlag(row==1?Kind::Ap:Kind::Gil);const auto scalar=ResolveF8Scalar(*flag);
            if(scalar.state==F8ScalarState::Invalid)F8Format(out,capacity,"Global %s rate: INVALID",row==1?"AP":"Gil");
            else F8Format(out,capacity,"Global %s rate: x%d",row==1?"AP":"Gil",scalar.value);return;}
        F8Copy(out,capacity,row==5?"Choose a monster":"Find monster by ID");return;
    }
    if(page==NativeSettingsPage::RewardRate){
        if(row==0)F8Format(out,capacity,"Multiplier: x%u",g_rewardRateDraft);
        else if(row==5)F8Format(out,capacity,"Use default: x%u",g_rewardGlobal?100u:1u);
        else {const char* labels[]={"","Increase (+1)","Decrease (-1)","Increase (+10)","Decrease (-10)","","Save multiplier"};F8Copy(out,capacity,labels[row]);}return;
    }
    if(page==NativeSettingsPage::MonsterRewardId){
        F8Format(out,capacity,"m%03u: %s",g_rewardIdDraft,row==0?"Increase (+1)":row==1?"Decrease (-1)":row==2?"Increase (+100)":row==3?"Decrease (-100)":"Open this monster");return;
    }
    unsigned id=g_rewardSpecies;if(page==NativeSettingsPage::MonsterRewardList&&!SpeciesAt(static_cast<unsigned>(row),id)){F8Copy(out,capacity,"Monster unavailable");return;}
    Preview preview{};if(!ReadPreview(id,preview)){F8Copy(out,capacity,"Reward data unavailable");return;}
    if(page==NativeSettingsPage::MonsterRewardList||row==0){F8Format(out,capacity,"m%03u  %.52s",id,UiLanguage::Raw{preview.name});return;}
    if(row==1||row==2){const bool ap=row==1;
        if(!(ap?preview.apValid:preview.gilValid))F8Format(out,capacity,"%s multiplier: INVALID",ap?"AP":"Gil");
        else F8Format(out,capacity,"%s multiplier: x%u",ap?"AP":"Gil",ap?preview.apMultiplier:preview.gilMultiplier);return;}
    if(row>=3&&row<=5){const auto& value=row==3?preview.ap:row==4?preview.overkillAp:preview.gil;
        const auto base=row==3?preview.base.ap:row==4?preview.base.overkillAp:preview.base.gil;
        const char* title=row==3?"AP":row==4?"Overkill AP":"Gil";
        if(preview.source==Source::Unavailable)F8Format(out,capacity,"%s: base unavailable until monster data load",title);
        else if(!value.valid)F8Format(out,capacity,"%s: invalid individual or global rate",title);
        else F8Format(out,capacity,"%s: %u > %llu > %u%s",title,static_cast<unsigned>(base),static_cast<unsigned long long>(value.individual),value.applied,value.clamped?" *":"");return;
    }
    if(row==6)F8Copy(out,capacity,"Configured totals, before native character bonuses");
    else if(row==7)F8Copy(out,capacity,preview.source==Source::LiveActor?"Base source: observed battle values":preview.source==Source::ModFile?"Base source: installed monster file":"Base source: not loaded");
    else F8Format(out,capacity,"Per-monster mode: %s",Detail());
}
static int F8RewardOpenRate(int obj,bool global,FfxHooks::MonsterRewards::Kind kind){
    if(g_nativeSettingsDepth>=4)return 3;
    using namespace FfxHooks;g_rewardGlobal=global;g_rewardKind=kind;g_rewardRateDraft=1;
    if(global){const auto scalar=ResolveF8Scalar(*F8RewardFlag(kind));g_rewardRateDraft=scalar.state==F8ScalarState::Invalid?100u:static_cast<unsigned>(scalar.value);}
    else {MonsterRewards::Preview p{};if(MonsterRewards::ReadPreview(g_rewardSpecies,p))g_rewardRateDraft=kind==MonsterRewards::Kind::Ap?p.apMultiplier:p.gilMultiplier;}
    return F8NativeSettingsEnter(obj,NativeSettingsPage::RewardRate);
}
static int F8RewardActivate(int obj,NativeSettingsPage page,int row){
    using namespace FfxHooks;using namespace MonsterRewards;
    if(page==NativeSettingsPage::RewardMultipliers){
        if(row==0||row==2||row==4){const auto* flag=row==4?FindF8Flag("cheats.monster_rewards"):F8RewardFlag(row==0?Kind::Ap:Kind::Gil);if(!flag)return 3;
            const auto result=SetF8FlagValue(*flag,!ResolveF8Flag(*flag).value);
            const bool effective=result.effective.value==result.requestedValue;
            strncpy_s(g_nativeSettingsNotice,result.code==F8EditCode::Saved?
                (!effective?"Preference saved. An external override still controls this feature.":
                 row==4?"Saved. Restart FFX to enable the per-monster hook.":"Saved. Global runtime applies this setting."):
                "Unable to save. Previous value preserved.",_TRUNCATE);return result.code==F8EditCode::Saved&&effective?1:3;}
        if(row==1||row==3)return F8RewardOpenRate(obj,true,row==1?Kind::Ap:Kind::Gil);
        if(row==5)return F8NativeSettingsEnter(obj,NativeSettingsPage::MonsterRewardList);
        g_rewardIdDraft=g_rewardSpecies;return F8NativeSettingsEnter(obj,NativeSettingsPage::MonsterRewardId);
    }
    if(page==NativeSettingsPage::MonsterRewardId){
        if(row==4){g_rewardSpecies=g_rewardIdDraft;return F8NativeSettingsEnter(obj,NativeSettingsPage::MonsterRewardDetail);}
        const int delta=row==0?1:row==1?-1:row==2?100:-100;
        const auto before=g_rewardIdDraft;
        g_rewardIdDraft=static_cast<unsigned>((std::max)(0,(std::min)(int(SpeciesCount-1),int(g_rewardIdDraft)+delta)));return before!=g_rewardIdDraft?1:0;
    }
    if(page==NativeSettingsPage::MonsterRewardList){
        if(!SpeciesAt(static_cast<unsigned>(row),g_rewardSpecies))return 3;return F8NativeSettingsEnter(obj,NativeSettingsPage::MonsterRewardDetail);
    }
    if(page==NativeSettingsPage::MonsterRewardDetail){if(row!=1&&row!=2)return 0;return F8RewardOpenRate(obj,false,row==1?Kind::Ap:Kind::Gil);}
    if(page==NativeSettingsPage::RewardRate){
        const unsigned maximum=g_rewardGlobal?100u:MaximumMultiplier;
        if(row>=1&&row<=4){const int delta=row==1?1:row==2?-1:row==3?10:-10;
            const auto before=g_rewardRateDraft;
            g_rewardRateDraft=static_cast<unsigned>((std::max)(1,(std::min)(int(maximum),int(g_rewardRateDraft)+delta)));return before!=g_rewardRateDraft?1:0;}
        if(row==5){const auto before=g_rewardRateDraft;g_rewardRateDraft=g_rewardGlobal?100u:1u;return before!=g_rewardRateDraft?1:0;}
        if(row==6){const bool saved=g_rewardGlobal?SetF8ScalarValue(*F8RewardFlag(g_rewardKind),static_cast<int>(g_rewardRateDraft)).code==F8ScalarEditCode::Saved:
                SaveMultiplier(g_rewardSpecies,g_rewardKind,g_rewardRateDraft);
            strncpy_s(g_nativeSettingsNotice,saved?"Multiplier saved. Other monsters and reward types are unchanged.":"Unable to save. Previous multiplier preserved.",_TRUNCATE);
            if(saved)F8NativeSettingsPop(obj);return saved?1:3;}
    }
    return 0;
}
