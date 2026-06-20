#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <functional>
#include <atomic>
#include <memory>

namespace ext::overlay {

enum class Backend { Auto, DX9, DX10, DX11, DX12 };

struct Flags
{
    bool    topmost      = true;
    bool    layered      = true;
    bool    clickthrough = true;
    bool    toolwindow   = true;
    bool    fullscreen   = true;
    Backend backend      = Backend::Auto;
    bool    streamproof  = false;
    int     toggle_key   = VK_INSERT;
    bool    inprocess    = false;
    bool    stealth      = false;
};

class IBackend;

class Overlay
{
public:
    using RenderFn = std::function<void()>;

    Overlay& set_flags(const Flags& f);
    Overlay& set_streamproof(bool on);
    Overlay& set_clickthrough(bool on);
    Overlay& set_toggle_key(int vk);
    Overlay& set_backend(Backend b);
    Overlay& set_inprocess(bool on);
    Overlay& set_stealth(bool on);

    bool     set_internal(bool on);
    Backend  detect_api() const;

    bool     hook();
    void     unhook();
    bool     is_hooked() const;

    Overlay& on_frame(RenderFn fn);
    Overlay& on_menu(RenderFn fn);
    Overlay& on_esp(RenderFn fn);

    bool menu_open() const { return m_menu_open; }
    HWND hwnd()       const { return m_hwnd; }

    void run();
    void stop() { m_stop.store(true); }

    void on_resize(UINT w, UINT h) { m_resize_w = w; m_resize_h = h; }
    void drive_hook_callbacks();

    bool create_window(const wchar_t* class_name);

private:
    void apply_clickthrough(bool ct);
    void apply_streamproof(bool on);
    void apply_stealth(bool on);
    void pump_messages(bool& done);
    void poll_toggle();

    Flags             m_flags{};
    HWND              m_hwnd = nullptr;
    bool              m_menu_open = false;
    std::atomic<bool> m_stop{ false };

    RenderFn          m_on_frame;
    RenderFn          m_on_menu;
    RenderFn          m_on_esp;

    UINT              m_resize_w = 0;
    UINT              m_resize_h = 0;

    std::unique_ptr<IBackend> m_backend;
};

Overlay& create(const wchar_t* class_name);
Overlay& instance();

}
