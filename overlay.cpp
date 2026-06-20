#include "overlay.h"
#include "config.h"
#include "backend/ibackend.h"

#include <cstdio>
#include <cwchar>

#include <console.hpp>
#include <error_handler.hpp>

#if __has_include(<minhook.h>)
#  include <minhook.h>
#  define OVERLAY_HAS_MINHOOK 1
#else
#  define OVERLAY_HAS_MINHOOK 0
#endif

#ifdef OVERLAY_USE_DX9
#include "backend/dx9_backend.h"
#endif
#ifdef OVERLAY_USE_DX10
#include "backend/dx10_backend.h"
#endif
#ifdef OVERLAY_USE_DX11
#include "backend/dx11_backend.h"
#endif
#ifdef OVERLAY_USE_DX12
#include "backend/dx12_backend.h"
#endif

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <dwmapi.h>

#ifdef OVERLAY_USE_DX11
#include <d3d11.h>
#include <dxgi.h>
#include <imgui_impl_dx11.h>
#endif

#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace ext::overlay {

static Overlay g_overlay;

Overlay& create(const wchar_t* class_name)
{
    g_overlay.create_window(class_name);
    return g_overlay;
}

Overlay& instance() { return g_overlay; }

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, w, l)) return TRUE;

    switch (msg) {
    case WM_SIZE:
        if (w != SIZE_MINIMIZED)
            g_overlay.on_resize((UINT)LOWORD(l), (UINT)HIWORD(l));
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, w, l);
}

static void make_random_name(wchar_t* out, size_t cap)
{
    LARGE_INTEGER c{};
    ::QueryPerformanceCounter(&c);
    swprintf_s(out, cap, L"_w%llx_%lx", (unsigned long long)c.QuadPart, ::GetCurrentProcessId());
}

bool Overlay::create_window(const wchar_t* class_name)
{
    if (m_hwnd) return true;

    wchar_t rname[40];
    const wchar_t* effective_name = class_name ? class_name : L"Overlay";
    const wchar_t* window_title   = effective_name;
    if (m_flags.stealth) {
        make_random_name(rname, 40);
        effective_name = rname;
        window_title   = L"";
    }

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
                       GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
                       effective_name, nullptr };
    ::RegisterClassExW(&wc);

    DWORD ex = 0;
    if (m_flags.topmost)      ex |= WS_EX_TOPMOST;
    if (m_flags.layered)      ex |= WS_EX_LAYERED;
    if (m_flags.clickthrough) ex |= WS_EX_TRANSPARENT;
    if (m_flags.toolwindow)   ex |= WS_EX_TOOLWINDOW;
    if (m_flags.stealth)      ex |= WS_EX_NOACTIVATE;

    const int cx = m_flags.fullscreen ? GetSystemMetrics(SM_CXSCREEN) : 1280;
    const int cy = m_flags.fullscreen ? GetSystemMetrics(SM_CYSCREEN) : 720;

    m_hwnd = ::CreateWindowExW(ex, effective_name, window_title, WS_POPUP,
                                0, 0, cx, cy, nullptr, nullptr, wc.hInstance, nullptr);
    if (!m_hwnd) return false;

    ::SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
    MARGINS m = { -1 };
    ::DwmExtendFrameIntoClientArea(m_hwnd, &m);

    if (m_flags.stealth) apply_stealth(true);
    return true;
}

Overlay& Overlay::set_flags(const Flags& f)
{
    const bool ct_changed = (f.clickthrough != m_flags.clickthrough);
    const bool sp_changed = (f.streamproof  != m_flags.streamproof);
    const bool st_changed = (f.stealth      != m_flags.stealth);
    m_flags = f;
    if (m_hwnd && ct_changed) apply_clickthrough(f.clickthrough);
    if (m_hwnd && sp_changed) apply_streamproof(f.streamproof);
    if (m_hwnd && st_changed) apply_stealth(f.stealth);
    return *this;
}

Overlay& Overlay::set_streamproof(bool on)
{
    m_flags.streamproof = on;
    if (m_hwnd) apply_streamproof(on);
    return *this;
}

Overlay& Overlay::set_clickthrough(bool on)
{
    m_flags.clickthrough = on;
    if (m_hwnd) apply_clickthrough(on);
    return *this;
}

Overlay& Overlay::set_toggle_key(int vk)  { m_flags.toggle_key = vk; return *this; }
Overlay& Overlay::set_backend(Backend b)  { m_flags.backend    = b;  return *this; }
Overlay& Overlay::set_inprocess(bool on)  { m_flags.inprocess  = on; return *this; }

Backend Overlay::detect_api() const
{
#if defined(OVERLAY_USE_DX11)
    if (::GetModuleHandleA("d3d11.dll")) return Backend::DX11;
    if (::GetModuleHandleA("d3d12.dll")) return Backend::DX12;
#elif defined(OVERLAY_USE_DX12)
    if (::GetModuleHandleA("d3d12.dll")) return Backend::DX12;
    if (::GetModuleHandleA("d3d11.dll")) return Backend::DX11;
#else
    if (::GetModuleHandleA("d3d12.dll")) return Backend::DX12;
    if (::GetModuleHandleA("d3d11.dll")) return Backend::DX11;
#endif
    if (::GetModuleHandleA("d3d10.dll")) return Backend::DX10;
    if (::GetModuleHandleA("d3d9.dll"))  return Backend::DX9;
    return Backend::Auto;
}

static const char* api_name(Backend b)
{
    switch (b) {
        case Backend::DX9:  return "DirectX 9";
        case Backend::DX10: return "DirectX 10";
        case Backend::DX11: return "DirectX 11";
        case Backend::DX12: return "DirectX 12";
        default:            return "None";
    }
}

bool Overlay::set_internal(bool on)
{
    if (!on) {
        if (is_hooked()) unhook();
        m_flags.inprocess = false;
        console.info("[overlay] internal mode disabled");
        return true;
    }

#if !OVERLAY_HAS_MINHOOK
    console.error("[overlay] MinHook header not detected (<minhook.h> missing from include path)");
    console.error("[overlay] Internal hook mode requires MinHook for cross-API VTable detours");
    console.warning("[overlay] If you intended external mode, leave set_internal(false) and use create()+run() instead");
    return false;
#else
    constexpr DWORD kWaitTimeoutMs = 120000;
    constexpr DWORD kLogIntervalMs = 5000;

    console.info("[overlay] waiting for Direct3D module to load (max %lus)...", kWaitTimeoutMs / 1000);

    const DWORD WaitStart = GetTickCount();
    DWORD LastLog = WaitStart;
    Backend Detected = Backend::Auto;

    while ((GetTickCount() - WaitStart) < kWaitTimeoutMs)
    {
        Detected = detect_api();
        if (Detected != Backend::Auto) break;

        if ((GetTickCount() - LastLog) >= kLogIntervalMs)
        {
            const DWORD Elapsed = (GetTickCount() - WaitStart) / 1000;
            console.info("[overlay] still waiting for D3D module... (%lus elapsed)", Elapsed);
            LastLog = GetTickCount();
        }
        Sleep(250);
    }

    if (Detected == Backend::Auto) {
        console.error("[overlay] timeout: no Direct3D module loaded after %lus", kWaitTimeoutMs / 1000);
        return false;
    }

    const DWORD WaitMs = GetTickCount() - WaitStart;
    console.success("[overlay] Direct3D ready (%s, after %lums)", api_name(Detected), WaitMs);

    Sleep(500);

    m_flags.backend   = Detected;
    m_flags.inprocess = true;

    const bool Ok = hook();
    if (!Ok) {
        console.error("[overlay] hook() failed for %s (backend not compiled in this config.h)", api_name(Detected));
        m_flags.inprocess = false;
        return false;
    }

    console.success("[overlay] internal mode enabled, %s hooked", api_name(Detected));
    return true;
#endif
}

Overlay& Overlay::set_stealth(bool on)
{
    m_flags.stealth = on;
    if (m_hwnd) apply_stealth(on);
    return *this;
}

void Overlay::apply_stealth(bool on)
{
    if (!m_hwnd) return;
    apply_streamproof(on);
    constexpr DWORD DWMWA_EXCLUDED_FROM_PEEK_LOCAL = 12;
    BOOL v = on ? TRUE : FALSE;
    ::DwmSetWindowAttribute(m_hwnd, DWMWA_EXCLUDED_FROM_PEEK_LOCAL, &v, sizeof(v));
}

Overlay& Overlay::on_frame(RenderFn fn) { m_on_frame = std::move(fn); return *this; }
Overlay& Overlay::on_menu (RenderFn fn) { m_on_menu  = std::move(fn); return *this; }
Overlay& Overlay::on_esp  (RenderFn fn) { m_on_esp   = std::move(fn); return *this; }

void Overlay::apply_clickthrough(bool ct)
{
    if (!m_hwnd) return;
    LONG_PTR s = ::GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if (ct) s |=  WS_EX_TRANSPARENT;
    else    s &= ~WS_EX_TRANSPARENT;
    ::SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, s);
    if (!ct) ::SetForegroundWindow(m_hwnd);
}

void Overlay::apply_streamproof(bool on)
{
    if (!m_hwnd) return;
    constexpr DWORD WDA_NONE_LOCAL              = 0x00;
    constexpr DWORD WDA_MONITOR_LOCAL           = 0x01;
    constexpr DWORD WDA_EXCLUDEFROMCAPTURE_LOCAL = 0x11;
    if (on) {
        if (!::SetWindowDisplayAffinity(m_hwnd, WDA_EXCLUDEFROMCAPTURE_LOCAL))
            ::SetWindowDisplayAffinity(m_hwnd, WDA_MONITOR_LOCAL);
    } else {
        ::SetWindowDisplayAffinity(m_hwnd, WDA_NONE_LOCAL);
    }
}

void Overlay::poll_toggle()
{
    static bool prev = false;
    const bool down = (::GetAsyncKeyState(m_flags.toggle_key) & 0x8000) != 0;
    if (down && !prev) {
        m_menu_open = !m_menu_open;
        apply_clickthrough(!m_menu_open);
    }
    prev = down;
}

void Overlay::pump_messages(bool& done)
{
    MSG msg;
    while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
        if (msg.message == WM_QUIT) done = true;
    }
}

static std::unique_ptr<IBackend> make_backend(Backend b)
{
#if defined(OVERLAY_USE_DX11)
    constexpr Backend kDefault = Backend::DX11;
#elif defined(OVERLAY_USE_DX12)
    constexpr Backend kDefault = Backend::DX12;
#elif defined(OVERLAY_USE_DX10)
    constexpr Backend kDefault = Backend::DX10;
#elif defined(OVERLAY_USE_DX9)
    constexpr Backend kDefault = Backend::DX9;
#else
    #error "No backend enabled in config.h"
#endif

    if (b == Backend::Auto) b = kDefault;

    switch (b) {
#ifdef OVERLAY_USE_DX9
    case Backend::DX9:  return std::make_unique<DX9Backend>();
#endif
#ifdef OVERLAY_USE_DX10
    case Backend::DX10: return std::make_unique<DX10Backend>();
#endif
#ifdef OVERLAY_USE_DX11
    case Backend::DX11: return std::make_unique<DX11Backend>();
#endif
#ifdef OVERLAY_USE_DX12
    case Backend::DX12: return std::make_unique<DX12Backend>();
#endif
    default: break;
    }

    switch (kDefault) {
#ifdef OVERLAY_USE_DX9
    case Backend::DX9:  return std::make_unique<DX9Backend>();
#endif
#ifdef OVERLAY_USE_DX10
    case Backend::DX10: return std::make_unique<DX10Backend>();
#endif
#ifdef OVERLAY_USE_DX11
    case Backend::DX11: return std::make_unique<DX11Backend>();
#endif
#ifdef OVERLAY_USE_DX12
    case Backend::DX12: return std::make_unique<DX12Backend>();
#endif
    default: return nullptr;
    }
}

void Overlay::run()
{
    if (m_flags.inprocess) {
        if (!hook()) return;
        while (is_hooked() && !m_stop.load()) ::Sleep(20);
        unhook();
        return;
    }

    if (!m_hwnd) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();

    m_backend = make_backend(m_flags.backend);
    if (!m_backend || !m_backend->init(m_hwnd)) {
        ImGui::DestroyContext();
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return;
    }

    ImGui_ImplWin32_Init(m_hwnd);

    if (m_flags.streamproof) apply_streamproof(true);
    if (m_flags.stealth)     apply_stealth(true);
    apply_clickthrough(!m_menu_open);

    ::ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(m_hwnd);

    bool done = false;
    while (!done && !m_stop.load())
    {
        pump_messages(done);
        if (done) break;

        poll_toggle();

        if (m_resize_w && m_resize_h) {
            m_backend->resize(m_resize_w, m_resize_h);
            m_resize_w = m_resize_h = 0;
        }

        m_backend->new_frame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (m_on_frame) m_on_frame();
        if (m_on_esp)   m_on_esp();
        if (m_menu_open && m_on_menu) m_on_menu();

        ImGui::Render();
        m_backend->render(ImGui::GetDrawData());
    }

    m_backend->shutdown();
    m_backend.reset();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    ::DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
}

}

#ifdef OVERLAY_USE_DX11

#include "backend/vtable_lookup.h"
#include "../esplib/backend_bridge.h"

namespace ext::overlay::dxhook {

static ImTextureID dx11_create_texture(const std::uint8_t* pixels, int width, int height, void* user);
static void        dx11_destroy_texture(ImTextureID tex, void* user);


using Present_t       = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffers_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

static Present_t                g_oPresent      = nullptr;
static ResizeBuffers_t          g_oResize       = nullptr;
static WNDPROC                  g_oWndProc      = nullptr;
static ID3D11Device*            g_dev           = nullptr;
static ID3D11DeviceContext*     g_ctx           = nullptr;
static ID3D11RenderTargetView*  g_rtv           = nullptr;
static IDXGISwapChain*          g_rtv_sc        = nullptr;
static IDXGISwapChain*          g_target_sc     = nullptr;
static HWND                     g_hwnd          = nullptr;
static bool                     g_imgui_ready   = false;
static bool                     g_installed     = false;
static void*                    g_present_target = nullptr;
static void*                    g_resize_target  = nullptr;

static LRESULT CALLBACK hkWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return TRUE;
    return ::CallWindowProcW(g_oWndProc, h, m, w, l);
}

static bool make_rtv(IDXGISwapChain* sc)
{
    if (g_rtv && g_rtv_sc == sc) return true;

    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; g_rtv_sc = nullptr; }

    ID3D11Texture2D* back = nullptr;
    if (FAILED(sc->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return false;
    HRESULT hr = g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();

    if (FAILED(hr)) return false;
    g_rtv_sc = sc;
    return true;
}

static void release_rtv()
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    g_rtv_sc = nullptr;
}

static bool init_imgui(IDXGISwapChain* sc)
{
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_dev))))
        return false;
    g_dev->GetImmediateContext(&g_ctx);

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc->GetDesc(&desc))) return false;
    g_hwnd = desc.OutputWindow;
    if (!g_hwnd) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.MouseDrawCursor = false;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(g_hwnd))
    {
        ImGui::DestroyContext();
        return false;
    }

    if (!ImGui_ImplDX11_Init(g_dev, g_ctx))
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    esp::bridge::set_texture_factory(&dx11_create_texture, &dx11_destroy_texture, nullptr);

    g_oWndProc = reinterpret_cast<WNDPROC>(
        ::SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hkWndProc)));

    return true;
}

static ImTextureID dx11_create_texture(const std::uint8_t* pixels, int width, int height, void*)
{
    if (!g_dev || !pixels || width <= 0 || height <= 0) return 0;

    D3D11_TEXTURE2D_DESC td{};
    td.Width            = width;
    td.Height           = height;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem     = pixels;
    sd.SysMemPitch = static_cast<UINT>(width) * 4u;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&td, &sd, &tex)) || !tex) return 0;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format                    = td.Format;
    srvd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels       = 1;
    srvd.Texture2D.MostDetailedMip = 0;

    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = g_dev->CreateShaderResourceView(tex, &srvd, &srv);
    tex->Release();
    if (FAILED(hr) || !srv) return 0;

    return reinterpret_cast<ImTextureID>(srv);
}

static void dx11_destroy_texture(ImTextureID tex, void*)
{
    if (!tex) return;
    reinterpret_cast<ID3D11ShaderResourceView*>(tex)->Release();
}

static bool is_target_swapchain(IDXGISwapChain* sc)
{
    if (!sc) return false;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc->GetDesc(&desc))) return false;
    HWND fg = ::GetForegroundWindow();
    return desc.OutputWindow != nullptr && desc.OutputWindow == fg;
}

static HRESULT STDMETHODCALLTYPE hkPresent(IDXGISwapChain* This, UINT sync, UINT flags)
{
    if (!g_target_sc)
    {
        if (is_target_swapchain(This))
            g_target_sc = This;
    }

    if (This != g_target_sc)
        return g_oPresent(This, sync, flags);

    if (!g_imgui_ready)
    {
        if (init_imgui(This))
            g_imgui_ready = true;
    }

    if (g_imgui_ready)
    {
        if (!make_rtv(This))
            return g_oPresent(This, sync, flags);

        ImGui::GetIO().MouseDrawCursor = instance().menu_open();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        instance().drive_hook_callbacks();

        ImGui::Render();
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    return g_oPresent(This, sync, flags);
}

static HRESULT STDMETHODCALLTYPE hkResizeBuffers(IDXGISwapChain* This, UINT bc, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags)
{
    if (This == g_target_sc) release_rtv();
    return g_oResize(This, bc, w, h, fmt, flags);
}

static bool install()
{
    if (g_installed) return true;

    void* PresentTarget = vtable::get_d3d11_present();
    void* ResizeTarget  = vtable::get_d3d11_resize_buffers();

    if (!PresentTarget || !ResizeTarget)
    {
        console.error("[dx11hook] failed to fetch VTable targets via dummy device");
        return false;
    }

    g_present_target = PresentTarget;
    g_resize_target  = ResizeTarget;

    MH_STATUS InitStatus = MH_Initialize();
    if (InitStatus != MH_OK && InitStatus != MH_ERROR_ALREADY_INITIALIZED)
    {
        console.error("[dx11hook] MH_Initialize failed: %d", InitStatus);
        return false;
    }

    if (MH_CreateHook(PresentTarget, reinterpret_cast<void*>(&hkPresent),
                      reinterpret_cast<void**>(&g_oPresent)) != MH_OK)
    {
        console.error("[dx11hook] MH_CreateHook(Present) failed");
        return false;
    }

    if (MH_CreateHook(ResizeTarget, reinterpret_cast<void*>(&hkResizeBuffers),
                      reinterpret_cast<void**>(&g_oResize)) != MH_OK)
    {
        console.error("[dx11hook] MH_CreateHook(ResizeBuffers) failed");
        MH_RemoveHook(PresentTarget);
        return false;
    }

    if (MH_EnableHook(PresentTarget) != MH_OK)
    {
        console.error("[dx11hook] MH_EnableHook(Present) failed");
        return false;
    }

    if (MH_EnableHook(ResizeTarget) != MH_OK)
    {
        console.error("[dx11hook] MH_EnableHook(ResizeBuffers) failed");
        return false;
    }

    console.success("[dx11hook] MinHook installed Present + ResizeBuffers");
    g_installed = true;
    return true;
}

static void uninstall()
{
    if (!g_installed) return;

    if (g_present_target) { MH_DisableHook(g_present_target); MH_RemoveHook(g_present_target); }
    if (g_resize_target)  { MH_DisableHook(g_resize_target);  MH_RemoveHook(g_resize_target);  }

    esp::bridge::clear_texture_factory();

    if (g_imgui_ready)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imgui_ready = false;
    }

    release_rtv();
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }

    if (g_hwnd && g_oWndProc)
    {
        ::SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_oWndProc);
        g_oWndProc = nullptr;
    }

    g_present_target = nullptr;
    g_resize_target  = nullptr;
    g_target_sc      = nullptr;
    g_installed = false;
}

}

#endif

#ifdef OVERLAY_USE_DX12

#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui_impl_dx12.h>
#include "backend/vtable_lookup.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace ext::overlay::dx12hook {

using Present_t              = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT);
using ResizeBuffers_t        = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteCommandLists_t  = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

static Present_t              g_oPresent           = nullptr;
static ResizeBuffers_t        g_oResize            = nullptr;
static ExecuteCommandLists_t  g_oExecuteCmdLists   = nullptr;
static WNDPROC                g_oWndProc           = nullptr;

static ID3D12Device*          g_dev                = nullptr;
static ID3D12CommandQueue*    g_queue              = nullptr;
static ID3D12GraphicsCommandList* g_cmdList        = nullptr;
static ID3D12DescriptorHeap*  g_rtvHeap            = nullptr;
static ID3D12DescriptorHeap*  g_srvHeap            = nullptr;
static HWND                   g_hwnd               = nullptr;
static UINT                   g_bufferCount        = 0;

struct FrameCtx
{
    ID3D12CommandAllocator*     CommandAllocator = nullptr;
    ID3D12Resource*             RenderTarget     = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle{};
    UINT64                      FenceValue       = 0;
};

static constexpr UINT kMaxFrames = 8;
static FrameCtx       g_frames[kMaxFrames]{};

static ID3D12Fence*   g_fence         = nullptr;
static HANDLE         g_fence_event   = nullptr;
static UINT64         g_fence_counter = 0;

static bool g_context_ready = false;
static bool g_render_ready  = false;
static bool g_imgui_ready   = false;
static bool g_installed     = false;
static void* g_present_target = nullptr;
static void* g_resize_target  = nullptr;
static void* g_execute_target = nullptr;

static LRESULT CALLBACK hkWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return TRUE;
    return ::CallWindowProcW(g_oWndProc, h, m, w, l);
}

static void release_frames()
{
    for (UINT i = 0; i < kMaxFrames; ++i)
    {
        if (g_frames[i].CommandAllocator) { g_frames[i].CommandAllocator->Release(); g_frames[i].CommandAllocator = nullptr; }
        if (g_frames[i].RenderTarget)     { g_frames[i].RenderTarget->Release();     g_frames[i].RenderTarget = nullptr; }
    }
}

static bool init_render_targets(IDXGISwapChain3* sc)
{
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc->GetDesc(&desc))) return false;

    g_bufferCount = desc.BufferCount;
    if (g_bufferCount == 0 || g_bufferCount > kMaxFrames) return false;

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = g_bufferCount;
    rtvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvDesc.NodeMask       = 1;
    if (FAILED(g_dev->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap)))) return false;

    const UINT rtvSize = g_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < g_bufferCount; ++i)
    {
        g_frames[i].RtvHandle = rtvHandle;
        if (FAILED(sc->GetBuffer(i, IID_PPV_ARGS(&g_frames[i].RenderTarget)))) return false;
        g_dev->CreateRenderTargetView(g_frames[i].RenderTarget, nullptr, rtvHandle);
        rtvHandle.ptr += rtvSize;

        if (FAILED(g_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 IID_PPV_ARGS(&g_frames[i].CommandAllocator))))
            return false;
    }

    if (FAILED(g_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       g_frames[0].CommandAllocator, nullptr, IID_PPV_ARGS(&g_cmdList))))
        return false;
    g_cmdList->Close();

    if (g_context_ready)
        ImGui_ImplDX12_CreateDeviceObjects();

    return true;
}

static bool init_imgui_context(IDXGISwapChain3* sc)
{
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc->GetDesc(&desc))) return false;

    g_hwnd = desc.OutputWindow;
    g_bufferCount = desc.BufferCount;
    if (g_bufferCount == 0 || g_bufferCount > kMaxFrames) return false;

    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&g_dev)))) return false;

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 64;
    srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvDesc.NodeMask       = 0;
    if (FAILED(g_dev->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&g_srvHeap)))) return false;

    if (FAILED(g_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence))))
        return false;
    g_fence_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!g_fence_event) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    if (!io.Fonts->AddFontDefault())
    {
        ImGui::DestroyContext();
        return false;
    }
    if (!io.Fonts->Build())
    {
        ImGui::DestroyContext();
        return false;
    }

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(g_hwnd))
    {
        ImGui::DestroyContext();
        return false;
    }

    if (!ImGui_ImplDX12_Init(g_dev, g_bufferCount, DXGI_FORMAT_R8G8B8A8_UNORM, g_srvHeap,
                             g_srvHeap->GetCPUDescriptorHandleForHeapStart(),
                             g_srvHeap->GetGPUDescriptorHandleForHeapStart()))
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    g_oWndProc = reinterpret_cast<WNDPROC>(
        ::SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hkWndProc)));

    return true;
}


static void render_frame_inner(IDXGISwapChain3* This)
{
    if (!g_queue || !g_cmdList || !g_rtvHeap || !g_srvHeap || !g_dev) return;

    static int s_frame_counter = 0;
    s_frame_counter++;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    instance().drive_hook_callbacks();

    ImGui::Render();

    const UINT idx = This->GetCurrentBackBufferIndex();
    if (idx >= g_bufferCount) return;
    if (!g_frames[idx].RenderTarget || !g_frames[idx].CommandAllocator) return;

    FrameCtx& f = g_frames[idx];

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = f.RenderTarget;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;

    if (g_fence && f.FenceValue > 0 && g_fence->GetCompletedValue() < f.FenceValue)
    {
        g_fence->SetEventOnCompletion(f.FenceValue, g_fence_event);
        WaitForSingleObject(g_fence_event, INFINITE);
    }

    f.CommandAllocator->Reset();
    g_cmdList->Reset(f.CommandAllocator, nullptr);
    g_cmdList->ResourceBarrier(1, &barrier);
    g_cmdList->OMSetRenderTargets(1, &f.RtvHandle, FALSE, nullptr);
    g_cmdList->SetDescriptorHeaps(1, &g_srvHeap);

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmdList);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    g_cmdList->ResourceBarrier(1, &barrier);
    g_cmdList->Close();

    ID3D12CommandList* lists[] = { g_cmdList };
    g_queue->ExecuteCommandLists(1, lists);

    if (g_fence)
    {
        const UINT64 NewFenceValue = ++g_fence_counter;
        g_queue->Signal(g_fence, NewFenceValue);
        f.FenceValue = NewFenceValue;
    }

}

static HRESULT STDMETHODCALLTYPE hkPresent(IDXGISwapChain3* This, UINT sync, UINT flags)
{
    static int s_present_counter = 0;
    s_present_counter++;

    if (!g_context_ready)
    {
        if (init_imgui_context(This))
        {
            g_context_ready = true;
        }
        else
        {
        }
    }

    if (g_context_ready && !g_render_ready)
    {
        if (init_render_targets(This))
        {
            g_render_ready = true;
            g_imgui_ready  = true;
        }
        else
        {
        }
    }

    if (g_imgui_ready && g_queue && g_render_ready)
        render_frame_inner(This);

    HRESULT hr = g_oPresent(This, sync, flags);

    if (FAILED(hr))
    {
        static bool s_device_removed_logged = false;
        if (!s_device_removed_logged && (hr == 0x887A0005 || hr == 0x887A0020 || hr == 0x887A0007 || hr == 0x887A0006))
        {
            s_device_removed_logged = true;
            HRESULT Reason = g_dev ? g_dev->GetDeviceRemovedReason() : 0;
        }
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE hkResizeBuffers(IDXGISwapChain3* This, UINT bc, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags)
{

    if (g_render_ready)
    {
        if (g_context_ready)
            ImGui_ImplDX12_InvalidateDeviceObjects();

        release_frames();
        if (g_cmdList) { g_cmdList->Release(); g_cmdList = nullptr; }
        if (g_rtvHeap) { g_rtvHeap->Release(); g_rtvHeap = nullptr; }
        g_bufferCount = 0;
        g_render_ready = false;
        g_imgui_ready  = false;
    }

    g_queue = nullptr;

    HRESULT hr = g_oResize(This, bc, w, h, fmt, flags);
    return hr;
}

static void STDMETHODCALLTYPE hkExecuteCommandLists(ID3D12CommandQueue* This, UINT n, ID3D12CommandList* const* lists)
{
    if (!g_queue && This && This->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        g_queue = This;
    }
    g_oExecuteCmdLists(This, n, lists);
}

static bool install()
{
    if (g_installed) return true;


    auto Pair = vtable::get_d3d12_vtable_entries(8, 10);
    void* PresentTarget = Pair.swapchain_fn;
    void* ExecuteTarget = Pair.queue_fn;

    void* ResizeTarget  = vtable::get_d3d12_swapchain_resize_buffers();


    if (!PresentTarget || !ResizeTarget || !ExecuteTarget)
    {
        console.error("[dx12hook] failed to fetch VTable targets via dummy device");
        return false;
    }

    g_present_target = PresentTarget;
    g_resize_target  = ResizeTarget;
    g_execute_target = ExecuteTarget;

    MH_STATUS InitStatus = MH_Initialize();
    if (InitStatus != MH_OK && InitStatus != MH_ERROR_ALREADY_INITIALIZED)
    {
        console.error("[dx12hook] MH_Initialize failed: %d", InitStatus);
        return false;
    }

    MH_STATUS s = MH_CreateHook(PresentTarget, reinterpret_cast<void*>(&hkPresent),
                                reinterpret_cast<void**>(&g_oPresent));
    if (s != MH_OK)
    {
        console.error("[dx12hook] MH_CreateHook(Present) failed: %d", s);
        return false;
    }

    s = MH_CreateHook(ResizeTarget, reinterpret_cast<void*>(&hkResizeBuffers),
                      reinterpret_cast<void**>(&g_oResize));
    if (s != MH_OK)
    {
        console.error("[dx12hook] MH_CreateHook(ResizeBuffers) failed: %d", s);
        MH_RemoveHook(PresentTarget);
        return false;
    }

    s = MH_CreateHook(ExecuteTarget, reinterpret_cast<void*>(&hkExecuteCommandLists),
                      reinterpret_cast<void**>(&g_oExecuteCmdLists));
    if (s != MH_OK)
    {
        console.error("[dx12hook] MH_CreateHook(ExecuteCommandLists) failed: %d", s);
        MH_RemoveHook(PresentTarget);
        MH_RemoveHook(ResizeTarget);
        return false;
    }

    if (MH_EnableHook(PresentTarget) != MH_OK)
    {
        console.error("[dx12hook] MH_EnableHook(Present) failed");
        return false;
    }

    if (MH_EnableHook(ResizeTarget) != MH_OK)
    {
        return false;
    }

    if (MH_EnableHook(ExecuteTarget) != MH_OK)
    {
        return false;
    }

    console.success("[dx12hook] MinHook installed Present + ResizeBuffers + ExecuteCommandLists");
    g_installed = true;
    return true;
}

static void uninstall()
{
    if (!g_installed) return;

    if (g_present_target) { MH_DisableHook(g_present_target); MH_RemoveHook(g_present_target); }
    if (g_resize_target)  { MH_DisableHook(g_resize_target);  MH_RemoveHook(g_resize_target);  }
    if (g_execute_target) { MH_DisableHook(g_execute_target); MH_RemoveHook(g_execute_target); }

    if (g_imgui_ready)
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imgui_ready = false;
    }

    if (g_hwnd && g_oWndProc)
    {
        ::SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_oWndProc);
        g_oWndProc = nullptr;
    }

    release_frames();
    if (g_cmdList) { g_cmdList->Release(); g_cmdList = nullptr; }
    if (g_rtvHeap) { g_rtvHeap->Release(); g_rtvHeap = nullptr; }
    if (g_srvHeap) { g_srvHeap->Release(); g_srvHeap = nullptr; }
    if (g_fence)   { g_fence->Release();   g_fence = nullptr; }
    if (g_fence_event) { CloseHandle(g_fence_event); g_fence_event = nullptr; }
    g_fence_counter = 0;
    if (g_dev)     { g_dev->Release();     g_dev = nullptr; }
    g_queue = nullptr;
    g_bufferCount = 0;
    g_context_ready = false;
    g_render_ready = false;

    g_installed = false;
}

}

#endif

#if defined(OVERLAY_USE_DX11) || defined(OVERLAY_USE_DX12)

namespace ext::overlay {

bool Overlay::hook()
{
#ifdef OVERLAY_USE_DX11
    if (m_flags.backend == Backend::DX11) return dxhook::install();
#endif
#ifdef OVERLAY_USE_DX12
    if (m_flags.backend == Backend::DX12) return dx12hook::install();
#endif
    return false;
}

void Overlay::unhook()
{
#ifdef OVERLAY_USE_DX11
    if (dxhook::g_installed) dxhook::uninstall();
#endif
#ifdef OVERLAY_USE_DX12
    if (dx12hook::g_installed) dx12hook::uninstall();
#endif
}

bool Overlay::is_hooked() const
{
#ifdef OVERLAY_USE_DX11
    if (dxhook::g_installed) return true;
#endif
#ifdef OVERLAY_USE_DX12
    if (dx12hook::g_installed) return true;
#endif
    return false;
}

void Overlay::drive_hook_callbacks()
{
    if (m_on_frame) m_on_frame();
    if (m_on_esp)   m_on_esp();

    static bool prev = false;
    const bool down = (::GetAsyncKeyState(m_flags.toggle_key) & 0x8000) != 0;
    if (down && !prev) m_menu_open = !m_menu_open;
    prev = down;

    if (m_menu_open && m_on_menu) m_on_menu();
}

}

#endif

#if !defined(OVERLAY_USE_DX11) && !defined(OVERLAY_USE_DX12)

namespace ext::overlay {
bool Overlay::hook()             { return false; }
void Overlay::unhook()           {}
bool Overlay::is_hooked() const  { return false; }
void Overlay::drive_hook_callbacks() {}
}

#endif
