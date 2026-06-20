#include "../config.h"
#ifdef OVERLAY_USE_DX9

#include "dx9_backend.h"
#include <imgui_impl_dx9.h>

#pragma comment(lib, "d3d9.lib")

namespace ext::overlay {

bool DX9Backend::init(HWND hwnd)
{
    m_hwnd = hwnd;
    m_d3d  = Direct3DCreate9(D3D_SDK_VERSION);
    if (!m_d3d) return false;

    m_pp                         = {};
    m_pp.Windowed                = TRUE;
    m_pp.SwapEffect              = D3DSWAPEFFECT_DISCARD;
    m_pp.BackBufferFormat        = D3DFMT_A8R8G8B8;
    m_pp.EnableAutoDepthStencil  = TRUE;
    m_pp.AutoDepthStencilFormat  = D3DFMT_D16;
    m_pp.PresentationInterval    = D3DPRESENT_INTERVAL_ONE;
    m_pp.hDeviceWindow           = hwnd;

    HRESULT hr = m_d3d->CreateDevice(
        D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &m_pp, &m_dev);
    if (FAILED(hr)) {
        m_d3d->Release();
        m_d3d = nullptr;
        return false;
    }
    return ImGui_ImplDX9_Init(m_dev);
}

void DX9Backend::shutdown()
{
    ImGui_ImplDX9_Shutdown();
    if (m_dev) { m_dev->Release(); m_dev = nullptr; }
    if (m_d3d) { m_d3d->Release(); m_d3d = nullptr; }
}

void DX9Backend::reset_device()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = m_dev->Reset(&m_pp);
    if (hr != D3DERR_INVALIDCALL)
        ImGui_ImplDX9_CreateDeviceObjects();
}

void DX9Backend::resize(UINT w, UINT h)
{
    if (!m_dev) return;
    m_pp.BackBufferWidth  = w;
    m_pp.BackBufferHeight = h;
    reset_device();
}

void DX9Backend::new_frame()
{
    ImGui_ImplDX9_NewFrame();
}

void DX9Backend::render(ImDrawData* d)
{
    m_dev->SetRenderState(D3DRS_ZENABLE,           FALSE);
    m_dev->SetRenderState(D3DRS_ALPHABLENDENABLE,  FALSE);
    m_dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);

    m_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_RGBA(0, 0, 0, 0), 1.0f, 0);

    if (m_dev->BeginScene() >= 0) {
        ImGui_ImplDX9_RenderDrawData(d);
        m_dev->EndScene();
    }

    HRESULT hr = m_dev->Present(nullptr, nullptr, nullptr, nullptr);
    if (hr == D3DERR_DEVICELOST && m_dev->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
        reset_device();
}

}

#endif
