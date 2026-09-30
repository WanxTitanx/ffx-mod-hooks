#pragma once
// Jarvis-HOOK: D3D11.1 context states preserve the peer's complete pipeline.
// Caller owns the bridge frame/resize gate. No destructor runs COM under loader lock.
#include <d3d11_1.h>

namespace FfxHooks::Coexistence {
class D3DState {
    ID3D11DeviceContext1* context_=nullptr;
    ID3DDeviceContextState* isolated_=nullptr;
    ID3DDeviceContextState* previous_=nullptr;
    template<class T> static void Release(T*& value) noexcept {
        if(value){value->Release();value=nullptr;}
    }
public:
    D3DState()=default;
    D3DState(const D3DState&)=delete;
    D3DState& operator=(const D3DState&)=delete;
    bool Begin(ID3D11DeviceContext* input) noexcept {
        if(!input||previous_||input->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
        ID3D11DeviceContext1* candidate=nullptr;
        if(FAILED(input->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&candidate))))return false;
        if(context_){
            const bool same=context_==candidate;Release(candidate);
            if(!same)return false;
        }else{
            ID3D11Device* device=nullptr;ID3D11Device1* device1=nullptr;
            candidate->GetDevice(&device);
            if(!device||FAILED(device->QueryInterface(__uuidof(ID3D11Device1),reinterpret_cast<void**>(&device1)))){
                Release(device);Release(candidate);return false;
            }
            // Keep the feature level and threading contract chosen by the game.
            const auto level=device->GetFeatureLevel();
            const UINT flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)
                ?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
            const HRESULT created=device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,
                __uuidof(ID3D11Device),nullptr,&isolated_);
            Release(device1);Release(device);
            if(FAILED(created)||!isolated_){Release(isolated_);Release(candidate);return false;}
            context_=candidate;
        }
        context_->SwapDeviceContextState(isolated_,&previous_);
        return previous_!=nullptr;
    }
    void End() noexcept {
        if(!previous_)return;
        // Do not let the cached overlay state retain backbuffers across resize.
        // This clear applies only to our isolated state, before restoring the peer.
        context_->ClearState();
        context_->SwapDeviceContextState(previous_,nullptr);
        Release(previous_);
    }
    bool Reset() noexcept {
        if(previous_)return false;
        Release(isolated_);Release(context_);return true;
    }
};
}
