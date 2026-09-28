#pragma once
#include "ArcanaNativeUi.h"
#include <filesystem>
struct ID3D11Device;struct ID3D11DeviceContext;struct ID3D11RenderTargetView;
namespace FfxHooks::Arcana::Assets {
bool Start(const std::filesystem::path& root,void(*log)(const char*));
void Publish(const NativeUi::Images&) noexcept;
void Stop() noexcept;
bool Decoded(unsigned resource) noexcept;
bool Render(ID3D11Device*,ID3D11DeviceContext*,ID3D11RenderTargetView*,unsigned width,unsigned height) noexcept;
void Present(void* swapChain) noexcept;
}
