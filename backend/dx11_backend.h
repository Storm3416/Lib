#pragma once
#include "../config.h"
#ifdef OVERLAY_USE_DX11

#include "ibackend.h"
#include <d3d11.h>
#include <dxgi.h>

namespace ext::overlay {

class DX11Backend final : public IBackend
{
public:
    bool init(HWND hwnd) override;
    void shutdown() override;
    void resize(UINT w, UINT h) override;
    void new_frame() override;
    void render(ImDrawData* draw_data) override;

private:
    void create_rtv();
    void destroy_rtv();

    ID3D11Device*           m_device = nullptr;
    ID3D11DeviceContext*    m_ctx    = nullptr;
    IDXGISwapChain*         m_swap   = nullptr;
    ID3D11RenderTargetView* m_rtv    = nullptr;
};

}

#endif
