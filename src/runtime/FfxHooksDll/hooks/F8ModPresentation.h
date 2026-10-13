#pragma once
#include "F8FlagCatalog.h"
#define FFXHOOKS_MOD_PRESENTATION_V1 1
namespace FfxHooks::F8Ui {
inline const char* ModStateText(const F8FlagSpec& flag,const F8RuntimeStatus& runtime,bool configured) noexcept {
    if(flag.activation==F8Activation::NotWired)return "NOT WIRED";
    switch(runtime.availability){
    case F8RuntimeAvailability::ProducerUnavailable:return "Startup failed";
    case F8RuntimeAvailability::SignatureMismatch:case F8RuntimeAvailability::UnsupportedBuild:return "Startup blocked";
    case F8RuntimeAvailability::Conflict:return "Startup conflict";
    case F8RuntimeAvailability::RestorePending:return "Cleanup pending";
    case F8RuntimeAvailability::PeerOwned:return "Managed by another loader";
    case F8RuntimeAvailability::PlatformLimited:return "Unavailable";
    default:break;
    }
    if(runtime.hasAppliedValue){
        if(flag.activation==F8Activation::RestartRequired&&runtime.appliedValue!=configured)return "Restart required";
        return runtime.appliedValue?"Running ON":"Running OFF";
    }
    return flag.activation==F8Activation::RestartRequired?"Restart required":"Unavailable";
}
}
