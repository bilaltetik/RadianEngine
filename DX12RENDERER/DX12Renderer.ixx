module;

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dx12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <array>
#include <span>
#include <IRenderer.h>

#include "Utilities/D3D12CoreHelper.h"

#include "Components/Vertex.h"
#include "Components/Texture.h"

#include "Utilities/Buffer.h"
#include "Utilities/ShaderCompiler.h"
#include "Utilities/PipelineState.h"
#include "Utilities/ConstantBuffer.h"
    
export module Radian.DX12;
import std;

struct alignas(256) TransformData {
    DirectX::XMFLOAT4X4 world;
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 projection;
};

using Microsoft::WRL::ComPtr;
using namespace Radian::Renderer;

struct CommandObjects {
    ComPtr<ID3D12CommandQueue>          queue;
    ComPtr<ID3D12CommandAllocator>      allocator;
    ComPtr<ID3D12GraphicsCommandList10> list;
};

class DX12Renderer final : public IRenderer {
public:
    [[nodiscard]] std::expected<void, std::string>
    Init(const RendererCreateInfo& info) override
    {
        m_info = info;

        PrepareDevice();
        ViewSetup();     
        GraphicLoaderSetup();
		m_pso = PipelineState::Create(m_device.Get(), m_rootSig.Get(), IE_POS_TEX, 2, m_vsBlob.Get(), m_psBlob.Get(), D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        return {};
    }

    void BeginFrame() {
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        m_cmd.allocator->Reset();
        m_cmd.list->Reset(m_cmd.allocator.Get(), m_pso.Get());
    }

    void RenderFrame() override {
        BeginFrame();
        BeginBarrier();

        auto* list = m_cmd.list.Get();
        list->RSSetViewports(1, &m_viewport);
        list->RSSetScissorRects(1, &m_scissor);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtv.m_heap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += static_cast<SIZE_T>(m_frameIndex) * rtv.m_size;
        list->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        constexpr std::array<float, 4> clearCol{ 0.05f, 0.05f, 0.15f, 1.0f };
        list->ClearRenderTargetView(rtvHandle, clearCol.data(), 0, nullptr);

        list->SetGraphicsRootSignature(m_rootSig.Get());

        TransformData transform{};

        DirectX::XMStoreFloat4x4(&transform.world,
            DirectX::XMMatrixTranspose(
                DirectX::XMMatrixIdentity()
            ));

        DirectX::XMStoreFloat4x4(&transform.view,
            DirectX::XMMatrixTranspose(
                DirectX::XMMatrixLookAtLH(
                    DirectX::XMVectorSet(0, 0, -3, 1),
                    DirectX::XMVectorSet(0, 0, 0, 1),
                    DirectX::XMVectorSet(0, 1, 0, 0)
                )
            ));

        DirectX::XMStoreFloat4x4(&transform.projection,
            DirectX::XMMatrixTranspose(
                DirectX::XMMatrixPerspectiveFovLH(
                    DirectX::XMConvertToRadians(60.0f),
                    static_cast<float>(m_info.width) / m_info.height,
                    0.1f, 1000.0f
                )
            ));

        m_transformBuffer.Update(m_frameIndex, transform);

        ID3D12DescriptorHeap* heaps[] = { m_srvAllocator.GetHeap() };
        list->SetDescriptorHeaps(1, heaps);

        list->SetGraphicsRootDescriptorTable(0, diffuse.srvHandle);
        list->SetGraphicsRootDescriptorTable(1, m_transformBuffer.GetGPUHandle(m_frameIndex));

        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->IASetVertexBuffers(0, 1, &m_vbView);
        list->DrawInstanced(6, 1, 0, 0);

        EndBarrier();
        EndFrame(false);
    }

    void EndFrame(bool vsync) {
        m_cmd.list->Close();
        const std::array<ID3D12CommandList*, 1> lists{ m_cmd.list.Get() };
        m_cmd.queue->ExecuteCommandLists(
            static_cast<UINT>(lists.size()), lists.data()
        );
        m_swapChain->Present(vsync, 0);
        WaitForGPU();
    }

    [[nodiscard]] std::wstring_view GetGPUName() const noexcept override {
        return m_gpu.m_name;
    }
    [[nodiscard]] size_t GetVRAMMB() const noexcept override {
        return m_gpu.m_videoMemory / (1024 * 1024);
    }

    ~DX12Renderer() override { Shutdown(); }

    void Shutdown() override {
        if (m_fenceEvent) {
            WaitForGPU();
            CloseHandle(m_fenceEvent);
            m_fenceEvent = nullptr;
        }
    }

private:

    static DX12Renderer* Get()
    {
        static DX12Renderer r;
        return &r;
    }

    std::expected<void, std::string> PrepareDevice() {

        m_factory = D3D12CoreHelper::CreateFactory();
        auto gpus = D3D12CoreHelper::QueryGPUs(m_factory.Get());
        m_device = D3D12CoreHelper::CreateDevice(gpus[0]);

        m_gpu    = std::move(gpus[0]);
        if (!m_device)
            return std::unexpected("D3D12 device oluşturulamadı");

        m_cmd.queue = D3D12CoreHelper::CreateCommandQueue(m_device.Get());
        m_cmd.allocator = D3D12CoreHelper::CreateCommandAllocator(m_device.Get());
        m_cmd.list = D3D12CoreHelper::CreateGraphicsCommandList(m_device.Get(), m_cmd.allocator.Get());

        return {};
    }

    bool CreateRTV() {
        rtv = D3D12CoreHelper::CreateDescriptorHeap(
            m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3);

        CD3DX12_CPU_DESCRIPTOR_HANDLE h(rtv.m_heap->GetCPUDescriptorHandleForHeapStart());
        m_renderTargets.resize(3);
        for (UINT i = 0; i < 3; ++i) {
            m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
            m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, h);
            h.Offset(1, rtv.m_size);
        }

        m_srvAllocator.Init(m_device.Get(), 64);

        return true;
    }

    std::expected<void, std::string> ViewSetup() {
        if (!m_factory) return std::unexpected("DXGI Factory nesnesi oluşturulmamış.");

        m_swapChain = D3D12CoreHelper::CreateSwapChain(m_factory.Get(), m_cmd.queue.Get(), m_info.hwnd, m_info.width, m_info.height, 3);

        if (!CreateRTV())  return std::unexpected("RTV oluşturulamadı");
        if (!InitSync())   return std::unexpected("Fence/sync nesnesi oluşturulamadı");

        m_viewport = {
            .TopLeftX = 0.0f,  .TopLeftY = 0.0f,
            .Width    = static_cast<float>(m_info.width),
            .Height   = static_cast<float>(m_info.height),
            .MinDepth = 0.0f,  .MaxDepth = 1.0f
        };
        m_scissor = {
            .left = 0, .top = 0,
            .right  = static_cast<LONG>(m_info.width),
            .bottom = static_cast<LONG>(m_info.height)
        };

        return PipelineSetup()
            ? std::expected<void, std::string>{}
            : std::unexpected("Root signature oluşturulamadı");
            
    }

    std::expected<void, std::string> GraphicLoaderSetup() {
        m_vsBlob = ShaderCompiler::Compile(L"common.hlsl", L"VSMain", L"vs_6_0");
        m_psBlob = ShaderCompiler::Compile(L"common.hlsl", L"PSMain", L"ps_6_0");


        VertexPosTex triangleVerts[6] = {
            VertexPosTex{{-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f}},
            VertexPosTex{{ 0.5f,  0.5f, 0.0f}, {1.0f, 0.0f}},
            VertexPosTex{{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}},
            VertexPosTex{{ 0.5f,  0.5f, 0.0f}, {1.0f, 0.0f}},
            VertexPosTex{{ 0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}},
            VertexPosTex{{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}},
        };

        m_vb = Buffer::Create(m_device.Get(), sizeof(triangleVerts));
        Buffer::Upload(m_vb.Get(), triangleVerts, sizeof(triangleVerts));

        m_vbView = {
            .BufferLocation = m_vb->GetGPUVirtualAddress(),
            .SizeInBytes    = static_cast<UINT>(sizeof(triangleVerts)),
            .StrideInBytes  = sizeof(VertexPosTex)
        };

        texLoader.Init(m_device.Get(), m_cmd.queue.Get(), m_srvAllocator.GetHeap(), m_srvAllocator.GetIncrementSize(), &m_srvAllocator); 
        diffuse = texLoader.Load(L"Texture.bmp");

       m_transformBuffer.Init(m_device.Get(), &m_srvAllocator, 3);
        return {};
    }

    void BeginBarrier()
    {
        const D3D12_TEXTURE_BARRIER barrier{
           .SyncBefore = D3D12_BARRIER_SYNC_NONE,
           .SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET,
           .AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS,
           .AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET,
           .LayoutBefore = D3D12_BARRIER_LAYOUT_PRESENT,
           .LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
           .pResource = m_renderTargets[m_frameIndex].Get()
        };
        const D3D12_BARRIER_GROUP group{
            .Type = D3D12_BARRIER_TYPE_TEXTURE,
            .NumBarriers = 1,
            .pTextureBarriers = &barrier
        };
        m_cmd.list->Barrier(1, &group);
    }

    void EndBarrier()
    {
        const D3D12_TEXTURE_BARRIER barrier{
            .SyncBefore   = D3D12_BARRIER_SYNC_RENDER_TARGET,
            .SyncAfter    = D3D12_BARRIER_SYNC_NONE,
            .AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET,
            .AccessAfter  = D3D12_BARRIER_ACCESS_NO_ACCESS,
            .LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            .LayoutAfter  = D3D12_BARRIER_LAYOUT_PRESENT,
            .pResource    = m_renderTargets[m_frameIndex].Get()
        };
        const D3D12_BARRIER_GROUP group{
            .Type = D3D12_BARRIER_TYPE_TEXTURE,
            .NumBarriers = 1,
            .pTextureBarriers = &barrier
        };
        m_cmd.list->Barrier(1, &group);
    }

    void WaitForGPU() {
        const uint64_t waitVal = m_fenceValue++;
        m_cmd.queue->Signal(m_fence.Get(), waitVal);
        if (m_fence->GetCompletedValue() < waitVal) {
            m_fence->SetEventOnCompletion(waitVal, m_fenceEvent);
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }

    bool InitSync() {
        if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
            return false;
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        return m_fenceEvent != nullptr;
    }

    bool PipelineSetup() {

        CD3DX12_DESCRIPTOR_RANGE1 srvRange, cbvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        cbvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

        CD3DX12_ROOT_PARAMETER1 params[2];
        params[0].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
        params[1].InitAsDescriptorTable(1, &cbvRange, D3D12_SHADER_VISIBILITY_VERTEX);

        CD3DX12_STATIC_SAMPLER_DESC sampler(
            0,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP
        );

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rsDesc.Desc_1_1.NumParameters = 2;
        rsDesc.Desc_1_1.pParameters = params;
        rsDesc.Desc_1_1.NumStaticSamplers = 1;
        rsDesc.Desc_1_1.pStaticSamplers = &sampler;
        rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> sig, err;

        D3D12SerializeVersionedRootSignature(&rsDesc, &sig, &err);

        return SUCCEEDED(
            m_device->CreateRootSignature(
                0,
                sig->GetBufferPointer(),
                sig->GetBufferSize(),
                IID_PPV_ARGS(&m_rootSig)
            )
        );
    }

	DescriptorHeapAllocator m_srvAllocator;
    ConstantBuffer<TransformData> m_transformBuffer;

    RendererCreateInfo      m_info{}; //
    GPUInfo                 m_gpu{}; //

    ComPtr<IDXGIFactory6>   m_factory; //
    ComPtr<ID3D12Device>    m_device; //
    CommandObjects          m_cmd;  //

    ComPtr<IDXGISwapChain3>             m_swapChain; //

    DescriptorHeap rtv; //

    std::vector<ComPtr<ID3D12Resource>> m_renderTargets;
    UINT                                m_frameIndex  = 0;

    ComPtr<ID3D12RootSignature> m_rootSig;
    ComPtr<ID3D12PipelineState> m_pso;
    ComPtr<ID3DBlob>            m_vsBlob;
    ComPtr<ID3DBlob>            m_psBlob;

    ComPtr<ID3D12Resource2>     m_vb;
    D3D12_VERTEX_BUFFER_VIEW    m_vbView{};

    ComPtr<ID3D12Fence>         m_fence;
    uint64_t                    m_fenceValue = 1;
    HANDLE                      m_fenceEvent = nullptr;

    D3D12_VIEWPORT  m_viewport{};
    D3D12_RECT      m_scissor{};

    WICTextureLoaderDX12 texLoader;
    WICTextureLoaderDX12::Texture diffuse;

};
