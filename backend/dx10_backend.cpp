#include "../config.h"
#ifdef OVERLAY_USE_DX10

#include "dx10_backend.h"
#include <imgui_impl_dx10.h>

#pragma comment(lib, "d3d10.lib")
#pragma comment(lib, "dxgi.lib")

namespace ext::overlay {

bool DX10Backend::init(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    HRESULT hr = D3D10CreateDeviceAndSwapChain(
        nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, 0,
        D3D10_SDK_VERSION, &sd, &m_swap, &m_dev);

    if (FAILED(hr)) {
        hr = D3D10CreateDeviceAndSwapChain(
            nullptr, D3D10_DRIVER_TYPE_WARP, nullptr, 0,
            D3D10_SDK_VERSION, &sd, &m_swap, &m_dev);
    }
    if (FAILED(hr)) return false;

    create_rtv();
    return ImGui_ImplDX10_Init(m_dev);
}

void DX10Backend::shutdown()
{
    ImGui_ImplDX10_Shutdown();
    destroy_rtv();
    if (m_swap) { m_swap->Release(); m_swap = nullptr; }
    if (m_dev)  { m_dev->Release();  m_dev  = nullptr; }
}

void DX10Backend::resize(UINT w, UINT h)
{
    if (!m_swap) return;
    destroy_rtv();
    m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    create_rtv();
}

void DX10Backend::new_frame()
{
    ImGui_ImplDX10_NewFrame();
}

void DX10Backend::render(ImDrawData* d)
{
    constexpr float clear[4] = { 0.f, 0.f, 0.f, 0.f };
    m_dev->OMSetRenderTargets(1, &m_rtv, nullptr);
    m_dev->ClearRenderTargetView(m_rtv, clear);
    ImGui_ImplDX10_RenderDrawData(d);
    m_swap->Present(1, 0);
}

void DX10Backend::create_rtv()
{
    ID3D10Texture2D* back = nullptr;
    m_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        m_dev->CreateRenderTargetView(back, nullptr, &m_rtv);
        back->Release();
    }
}

void DX10Backend::destroy_rtv()
{
    if (m_rtv) { m_rtv->Release(); m_rtv = nullptr; }
}

}

#endif
