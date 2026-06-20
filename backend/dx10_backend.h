#pragma once
#include "../config.h"
#ifdef OVERLAY_USE_DX10

#include "ibackend.h"
#include <d3d10.h>
#include <dxgi.h>

namespace ext::overlay {

class DX10Backend final : public IBackend
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

    ID3D10Device*           m_dev  = nullptr;
    IDXGISwapChain*         m_swap = nullptr;
    ID3D10RenderTargetView* m_rtv  = nullptr;
};

}

#endif
