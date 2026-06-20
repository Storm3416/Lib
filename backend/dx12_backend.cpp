#include "../config.h"
#ifdef OVERLAY_USE_DX12

#include "dx12_backend.h"
#include <imgui_impl_dx12.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace ext::overlay {

bool DX12Backend::init(HWND hwnd)
{
    m_hwnd = hwnd;

    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;

    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_dev)))) {
        factory->Release();
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(m_dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue)))) {
        factory->Release();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.BufferCount      = kFrameCount;
    sd.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.SampleDesc.Count = 1;

    IDXGISwapChain1* sc1 = nullptr;
    if (FAILED(factory->CreateSwapChainForHwnd(m_queue, hwnd, &sd, nullptr, nullptr, &sc1))) {
        factory->Release();
        return false;
    }
    sc1->QueryInterface(IID_PPV_ARGS(&m_swap));
    sc1->Release();
    factory->Release();

    D3D12_DESCRIPTOR_HEAP_DESC rtvh{};
    rtvh.NumDescriptors = kFrameCount;
    rtvh.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(m_dev->CreateDescriptorHeap(&rtvh, IID_PPV_ARGS(&m_rtv_heap)))) return false;

    m_rtv_inc = m_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kFrameCount; ++i) {
        m_rtv_handles[i] = cpu;
        cpu.ptr += m_rtv_inc;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvh{};
    srvh.NumDescriptors = 1;
    srvh.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvh.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_dev->CreateDescriptorHeap(&srvh, IID_PPV_ARGS(&m_srv_heap)))) return false;

    for (UINT i = 0; i < kFrameCount; ++i) {
        if (FAILED(m_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_frames[i].alloc))))
            return false;
    }

    if (FAILED(m_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frames[0].alloc, nullptr, IID_PPV_ARGS(&m_cmd_list))))
        return false;
    m_cmd_list->Close();

    if (FAILED(m_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) return false;
    m_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fence_event) return false;

    if (!create_rtvs()) return false;

    return ImGui_ImplDX12_Init(m_dev, kFrameCount, DXGI_FORMAT_R8G8B8A8_UNORM,
                                m_srv_heap,
                                m_srv_heap->GetCPUDescriptorHandleForHeapStart(),
                                m_srv_heap->GetGPUDescriptorHandleForHeapStart());
}

bool DX12Backend::create_rtvs()
{
    for (UINT i = 0; i < kFrameCount; ++i) {
        if (FAILED(m_swap->GetBuffer(i, IID_PPV_ARGS(&m_rt[i])))) return false;
        m_dev->CreateRenderTargetView(m_rt[i], nullptr, m_rtv_handles[i]);
    }
    return true;
}

void DX12Backend::destroy_rtvs()
{
    for (UINT i = 0; i < kFrameCount; ++i) {
        if (m_rt[i]) { m_rt[i]->Release(); m_rt[i] = nullptr; }
    }
}

void DX12Backend::shutdown()
{
    wait_for_gpu();
    ImGui_ImplDX12_Shutdown();
    destroy_rtvs();
    if (m_fence_event) { CloseHandle(m_fence_event); m_fence_event = nullptr; }
    if (m_fence)       { m_fence->Release();    m_fence    = nullptr; }
    if (m_cmd_list)    { m_cmd_list->Release(); m_cmd_list = nullptr; }
    for (auto& f : m_frames)
        if (f.alloc) { f.alloc->Release(); f.alloc = nullptr; }
    if (m_srv_heap)    { m_srv_heap->Release(); m_srv_heap = nullptr; }
    if (m_rtv_heap)    { m_rtv_heap->Release(); m_rtv_heap = nullptr; }
    if (m_swap)        { m_swap->Release();     m_swap     = nullptr; }
    if (m_queue)       { m_queue->Release();    m_queue    = nullptr; }
    if (m_dev)         { m_dev->Release();      m_dev      = nullptr; }
}

void DX12Backend::resize(UINT w, UINT h)
{
    if (!m_swap) return;
    wait_for_gpu();
    destroy_rtvs();
    m_swap->ResizeBuffers(kFrameCount, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    create_rtvs();
}

void DX12Backend::new_frame()
{
    ImGui_ImplDX12_NewFrame();
}

void DX12Backend::wait_for_gpu()
{
    if (!m_queue || !m_fence) return;
    m_queue->Signal(m_fence, ++m_fence_last);
    if (m_fence->GetCompletedValue() < m_fence_last) {
        m_fence->SetEventOnCompletion(m_fence_last, m_fence_event);
        WaitForSingleObject(m_fence_event, INFINITE);
    }
}

void DX12Backend::wait_frame(UINT idx)
{
    if (m_frames[idx].fence_val == 0) return;
    if (m_fence->GetCompletedValue() < m_frames[idx].fence_val) {
        m_fence->SetEventOnCompletion(m_frames[idx].fence_val, m_fence_event);
        WaitForSingleObject(m_fence_event, INFINITE);
    }
}

void DX12Backend::render(ImDrawData* d)
{
    const UINT idx = m_swap->GetCurrentBackBufferIndex();
    wait_frame(idx);

    auto& frame = m_frames[idx];
    frame.alloc->Reset();
    m_cmd_list->Reset(frame.alloc, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = m_rt[idx];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_cmd_list->ResourceBarrier(1, &barrier);

    constexpr float clear[4] = { 0.f, 0.f, 0.f, 0.f };
    m_cmd_list->ClearRenderTargetView(m_rtv_handles[idx], clear, 0, nullptr);
    m_cmd_list->OMSetRenderTargets(1, &m_rtv_handles[idx], FALSE, nullptr);
    m_cmd_list->SetDescriptorHeaps(1, &m_srv_heap);

    ImGui_ImplDX12_RenderDrawData(d, m_cmd_list);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    m_cmd_list->ResourceBarrier(1, &barrier);

    m_cmd_list->Close();
    ID3D12CommandList* lists[] = { m_cmd_list };
    m_queue->ExecuteCommandLists(1, lists);

    m_swap->Present(1, 0);

    frame.fence_val = ++m_fence_last;
    m_queue->Signal(m_fence, frame.fence_val);
}

}

#endif
