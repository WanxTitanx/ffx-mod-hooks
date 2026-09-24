#include "RonsoPoolSave.h"
namespace FfxHooks::RonsoPool {
namespace {
constexpr std::array<uint16_t,256> CrcTable() noexcept {
    std::array<uint16_t,256> table{};
    // The supported game's generator stops before index255; that entry stays0.
    for(unsigned i=0;i<255;++i) {
        uint16_t value=static_cast<uint16_t>(i<<8);
        for(unsigned bit=0;bit<8;++bit)value=static_cast<uint16_t>(
            (value&0x8000u)?(value<<1)^0x1021u:value<<1);
        table[i]=value;
    }
    return table;
}
constexpr auto kCrcTable=CrcTable();
uint16_t Word(const SaveImage& image,size_t offset) noexcept {
    return static_cast<uint16_t>(image[offset]|(static_cast<uint16_t>(image[offset+1])<<8));
}
bool Coherent(const SaveImage& image) noexcept {
    return image[kSaveMaximum]>0 && image[kSaveMaximum]<=kCapacity &&
        image[kSaveCharge]<=image[kSaveMaximum];
}
}
uint16_t SaveChecksum(const SaveImage& image) noexcept {
    uint16_t crc=0xFFFFu;
    for(size_t i=64;i<kPayloadSize;++i) {
        const uint8_t value=i>=25844?0:image[i];
        crc=static_cast<uint16_t>((crc<<8)^kCrcTable[((crc>>8)^value)&0xFFu]);
    }
    return crc^0xFFFFu;
}
bool IsValidSave(const SaveImage& image) noexcept {
    return image[25846]==0 && image[25847]==0 && Word(image,26)==Word(image,25844) &&
        Word(image,26)==SaveChecksum(image);
}
void SealSave(SaveImage& image) noexcept {
    const uint16_t crc=SaveChecksum(image);
    image[26]=image[25844]=static_cast<uint8_t>(crc);
    image[27]=image[25845]=static_cast<uint8_t>(crc>>8);
    image[25846]=image[25847]=0;
}
SaveDecision LoadPool(bool active,const SaveImage& input,const SavedOwner* owner,uint16_t scene,
                      SaveSession* session,SaveImage* output) noexcept {
    if(!session || !output)return SaveDecision::Invalid;
    *session={};session->scene=scene;*output=input;
    if(!IsValidSave(input))return SaveDecision::Invalid;
    if(owner && (owner->originalMax==0 || owner->originalMax>kCapacity ||
        owner->maximum!=kCapacity || owner->charge>kCapacity ||
        owner->maximum!=input[kSaveMaximum] || owner->charge!=input[kSaveCharge]))
        return SaveDecision::OwnershipConflict;
    if(!active && !owner)return SaveDecision::Native;
    if(!Coherent(input))return SaveDecision::OwnershipConflict;
    session->originalMax=owner?owner->originalMax:input[kSaveMaximum];
    session->owned=true;
    if(active) {
        (*output)[kSaveMaximum]=kCapacity;
    } else {
        const uint8_t full=input[kSaveCharge];
        const uint8_t visible=full>session->originalMax?session->originalMax:full;
        session->dormant=static_cast<uint8_t>(full-visible);
        (*output)[kSaveCharge]=visible;(*output)[kSaveMaximum]=session->originalMax;
    }
    SealSave(*output);return SaveDecision::Converted;
}
SaveDecision SavePool(bool active,const SaveSession& session,const SaveImage& input,
                      SaveImage* output,SavedOwner* owner,bool* needsOwner) noexcept {
    if(!output || !owner || !needsOwner)return SaveDecision::Invalid;
    *output=input;*owner={};*needsOwner=false;
    if(!IsValidSave(input))return SaveDecision::Invalid;
    if(!active && (!session.owned || session.dormant==0))return SaveDecision::Native;
    if(!Coherent(input))return SaveDecision::OwnershipConflict;
    const uint8_t original=session.owned?session.originalMax:input[kSaveMaximum];
    if(original==0 || original>kCapacity)return SaveDecision::OwnershipConflict;
    unsigned full=input[kSaveCharge];
    if(!active) {
        if(input[kSaveMaximum]!=original)return SaveDecision::OwnershipConflict;
        full+=session.dormant;
        if(full>kCapacity)return SaveDecision::OwnershipConflict;
    }
    (*output)[kSaveCharge]=static_cast<uint8_t>(full);(*output)[kSaveMaximum]=kCapacity;
    SealSave(*output);
    *owner={original,static_cast<uint8_t>(full),kCapacity};*needsOwner=true;
    return SaveDecision::Converted;
}
void ObserveScene(SaveSession* session,uint16_t scene) noexcept {
    if(!session)return;
    if(scene==23 && session->scene!=23 && session->scene!=UINT16_MAX && session->scene!=0)
        *session={};
    session->scene=scene;
}
}
