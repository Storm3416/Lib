#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include "../config.h"

#ifdef OVERLAY_USE_DX9
#include <d3d9.h>
#pragma comment(lib, "d3d9.lib")
#endif

#ifdef OVERLAY_USE_DX10
#include <d3d10.h>
#include <dxgi.h>
#pragma comment(lib, "d3d10.lib")
#pragma comment(lib, "dxgi.lib")
#endif

#ifdef OVERLAY_USE_DX11
#include <d3d11.h>
#include <dxgi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif

#ifdef OVERLAY_USE_DX12
#include <d3d12.h>
#include <dxgi1_4.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#endif

namespace ext::overlay::vtable {

inline HWND make_dummy_window()
{
    return CreateWindowExA(0, "STATIC", "tmp", WS_OVERLAPPED, 0, 0, 1, 1,
        nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
}

#ifdef OVERLAY_USE_DX9

inline void* get_d3d9_vtable_entry(int index)
{
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return nullptr;

    HWND hwnd = make_dummy_window();
    if (!hwnd) { d3d->Release(); return nullptr; }

    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed         = TRUE;
    pp.SwapEffect       = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow    = hwnd;

    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES, &pp, &dev);
    if (FAILED(hr) || !dev)
    {
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES, &pp, &dev);
    }
    if (FAILED(hr) || !dev)
    {
        d3d->Release();
        DestroyWindow(hwnd);
        return nullptr;
    }

    void** vt = *reinterpret_cast<void***>(dev);
    void* fn = vt[index];

    dev->Release();
    d3d->Release();
    DestroyWindow(hwnd);
    return fn;
}

inline void* get_d3d9_present()        { return get_d3d9_vtable_entry(17); }
inline void* get_d3d9_reset()          { return get_d3d9_vtable_entry(16); }
inline void* get_d3d9_endscene()       { return get_d3d9_vtable_entry(42); }
inline void* get_d3d9_drawindexedprim(){ return get_d3d9_vtable_entry(82); }

#endif

#ifdef OVERLAY_USE_DX10

inline void* get_d3d10_vtable_entry(int index)
{
    HWND hwnd = make_dummy_window();
    if (!hwnd) return nullptr;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount       = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow      = hwnd;
    sd.SampleDesc.Count  = 1;
    sd.Windowed          = TRUE;
    sd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* sc  = nullptr;
    ID3D10Device*   dev = nullptr;

    HRESULT hr = D3D10CreateDeviceAndSwapChain(
        nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, 0,
        D3D10_SDK_VERSION, &sd, &sc, &dev);
    if (FAILED(hr) || !sc)
    {
        if (dev) dev->Release();
        DestroyWindow(hwnd);
        return nullptr;
    }

    void** vt = *reinterpret_cast<void***>(sc);
    void* fn = vt[index];

    sc->Release();
    dev->Release();
    DestroyWindow(hwnd);
    return fn;
}

inline void* get_d3d10_present()        { return get_d3d10_vtable_entry(8);  }
inline void* get_d3d10_resize_buffers() { return get_d3d10_vtable_entry(13); }

#endif

#ifdef OVERLAY_USE_DX11

inline void* get_d3d11_vtable_entry(int index)
{
    HWND hwnd = make_dummy_window();
    if (!hwnd) return nullptr;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount       = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow      = hwnd;
    sd.SampleDesc.Count  = 1;
    sd.Windowed          = TRUE;
    sd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain*      sc  = nullptr;
    ID3D11Device*        dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL    fl;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &sd,
        &sc, &dev, &fl, &ctx);
    if (FAILED(hr) || !sc)
    {
        if (dev) dev->Release();
        if (ctx) ctx->Release();
        DestroyWindow(hwnd);
        return nullptr;
    }

    void** vt = *reinterpret_cast<void***>(sc);
    void* fn = vt[index];

    sc->Release();
    ctx->Release();
    dev->Release();
    DestroyWindow(hwnd);
    return fn;
}

inline void* get_d3d11_present()        { return get_d3d11_vtable_entry(8);  }
inline void* get_d3d11_resize_buffers() { return get_d3d11_vtable_entry(13); }

#endif

#ifdef OVERLAY_USE_DX12

struct d3d12_vtable_pair
{
    void* swapchain_fn = nullptr;
    void* queue_fn     = nullptr;
};

inline d3d12_vtable_pair get_d3d12_vtable_entries(int swapchain_index, int queue_index)
{
    d3d12_vtable_pair Out;

    HWND hwnd = make_dummy_window();
    if (!hwnd) return Out;

    ID3D12Device* dev = nullptr;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev))))
    {
        DestroyWindow(hwnd);
        return Out;
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    ID3D12CommandQueue* q = nullptr;
    if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q))))
    {
        dev->Release();
        DestroyWindow(hwnd);
        return Out;
    }

    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    {
        q->Release();
        dev->Release();
        DestroyWindow(hwnd);
        return Out;
    }

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount         = 2;
    sd.BufferDesc.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.Width    = 100;
    sd.BufferDesc.Height   = 100;
    sd.BufferUsage         = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect          = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.OutputWindow        = hwnd;
    sd.SampleDesc.Count    = 1;
    sd.Windowed            = TRUE;

    IDXGISwapChain* sc = nullptr;
    if (FAILED(factory->CreateSwapChain(q, &sd, &sc)))
    {
        factory->Release();
        q->Release();
        dev->Release();
        DestroyWindow(hwnd);
        return Out;
    }

    void** swapchain_vt = *reinterpret_cast<void***>(sc);
    void** queue_vt     = *reinterpret_cast<void***>(q);

    Out.swapchain_fn = swapchain_vt[swapchain_index];
    Out.queue_fn     = queue_vt[queue_index];

    sc->Release();
    factory->Release();
    q->Release();
    dev->Release();
    DestroyWindow(hwnd);
    return Out;
}

inline void* get_d3d12_swapchain_present()
{
    return get_d3d12_vtable_entries(8, 10).swapchain_fn;
}

inline void* get_d3d12_swapchain_resize_buffers()
{
    return get_d3d12_vtable_entries(13, 10).swapchain_fn;
}

inline void* get_d3d12_command_queue_execute()
{
    return get_d3d12_vtable_entries(8, 10).queue_fn;
}

#endif

}
