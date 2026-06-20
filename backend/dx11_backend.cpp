#include "../config.h"
#ifdef OVERLAY_USE_DX11

#include "dx11_backend.h"
#include <imgui_impl_dx11.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

ID3D11Device*        g_pd3dDevice        = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;

namespace ext::overlay {

bool DX11Backend::init(HWND hwnd)
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

    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL lvls[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        lvls, 2, D3D11_SDK_VERSION, &sd,
        &m_swap, &m_device, &fl, &m_ctx);

    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            lvls, 2, D3D11_SDK_VERSION, &sd,
            &m_swap, &m_device, &fl, &m_ctx);
    }
    if (FAILED(hr)) return false;

    create_rtv();

    g_pd3dDevice        = m_device;
    g_pd3dDeviceContext = m_ctx;

    return ImGui_ImplDX11_Init(m_device, m_ctx);
}

void DX11Backend::shutdown()
{
    ImGui_ImplDX11_Shutdown();
    destroy_rtv();
    if (m_swap)   { m_swap->Release();   m_swap   = nullptr; }
    if (m_ctx)    { m_ctx->Release();    m_ctx    = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }

    g_pd3dDevice        = nullptr;
    g_pd3dDeviceContext = nullptr;
}

void DX11Backend::resize(UINT w, UINT h)
{
    if (!m_swap) return;
    destroy_rtv();
    m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    create_rtv();
}

void DX11Backend::new_frame()
{
    ImGui_ImplDX11_NewFrame();
}

void DX11Backend::render(ImDrawData* d)
{
    constexpr float clear[4] = { 0.f, 0.f, 0.f, 0.f };
    m_ctx->OMSetRenderTargets(1, &m_rtv, nullptr);
    m_ctx->ClearRenderTargetView(m_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(d);
    m_swap->Present(1, 0);
}

void DX11Backend::create_rtv()
{
    ID3D11Texture2D* back = nullptr;
    m_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        m_device->CreateRenderTargetView(back, nullptr, &m_rtv);
        back->Release();
    }
}

void DX11Backend::destroy_rtv()
{
    if (m_rtv) { m_rtv->Release(); m_rtv = nullptr; }
}

}

#endif
