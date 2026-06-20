#pragma once
#include "../config.h"
#ifdef OVERLAY_USE_DX12

#include "ibackend.h"
#include <d3d12.h>
#include <dxgi1_4.h>

namespace ext::overlay {

class DX12Backend final : public IBackend
{
public:
    bool init(HWND hwnd) override;
    void shutdown() override;
    void resize(UINT w, UINT h) override;
    void new_frame() override;
    void render(ImDrawData* draw_data) override;

private:
    static constexpr UINT kFrameCount = 3;

    struct FrameCtx
    {
        ID3D12CommandAllocator* alloc     = nullptr;
        UINT64                  fence_val = 0;
    };

    bool create_rtvs();
    void destroy_rtvs();
    void wait_for_gpu();
    void wait_frame(UINT idx);

    ID3D12Device*               m_dev          = nullptr;
    ID3D12CommandQueue*         m_queue        = nullptr;
    IDXGISwapChain3*            m_swap         = nullptr;
    ID3D12DescriptorHeap*       m_rtv_heap     = nullptr;
    ID3D12DescriptorHeap*       m_srv_heap     = nullptr;
    ID3D12GraphicsCommandList*  m_cmd_list     = nullptr;
    ID3D12Resource*             m_rt[kFrameCount]           {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtv_handles[kFrameCount]  {};
    FrameCtx                    m_frames[kFrameCount]       {};
    ID3D12Fence*                m_fence        = nullptr;
    HANDLE                      m_fence_event  = nullptr;
    UINT64                      m_fence_last   = 0;
    UINT                        m_rtv_inc      = 0;
    HWND                        m_hwnd         = nullptr;
};

}

#endif
