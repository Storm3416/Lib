#pragma once
#include "../config.h"
#ifdef OVERLAY_USE_DX9

#include "ibackend.h"
#include <d3d9.h>

namespace ext::overlay {

class DX9Backend final : public IBackend
{
public:
    bool init(HWND hwnd) override;
    void shutdown() override;
    void resize(UINT w, UINT h) override;
    void new_frame() override;
    void render(ImDrawData* draw_data) override;

private:
    void reset_device();

    IDirect3D9*           m_d3d  = nullptr;
    IDirect3DDevice9*     m_dev  = nullptr;
    D3DPRESENT_PARAMETERS m_pp{};
    HWND                  m_hwnd = nullptr;
};

}

#endif
