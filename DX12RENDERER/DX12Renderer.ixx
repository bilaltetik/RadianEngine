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

#include "Mesh/Vertex.h"
#include "Material/Texture.h"

export module Radian.DX12;
import std;

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
        m_info = info;  // HWND + boyutları sakla

        return PrepareDevice()
            .and_then([&] { return ViewSetup();           })
            .and_then([&] { return GraphicLoaderSetup();  })
            .and_then([&] { return BuildPSO();            });
    }

    void BeginFrame() {
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        m_cmd.allocator->Reset();
        m_cmd.list->Reset(m_cmd.allocator.Get(), m_pso.Get());
        rtvHeap = rtv.heap;
    }

    void RenderFrame() override {
        BeginFrame();
        BeginBarrier();

        auto* list = m_cmd.list.Get();
        list->RSSetViewports(1, &m_viewport);
        list->RSSetScissorRects(1, &m_scissor);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtv.heap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += static_cast<SIZE_T>(m_frameIndex) * rtv.size;
        list->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        // [C++20] constexpr std::array — magic C dizisi yok
        constexpr std::array<float, 4> clearCol{ 0.05f, 0.05f, 0.15f, 1.0f };
        list->ClearRenderTargetView(rtvHandle, clearCol.data(), 0, nullptr);

        list->SetGraphicsRootSignature(m_rootSig.Get());

        ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
        list->SetDescriptorHeaps(1, heaps);

        list->SetGraphicsRootDescriptorTable(
            0,
            diffuse.srvHandle
        );

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
        return m_gpu.name;
    }
    [[nodiscard]] size_t GetVRAMMB() const noexcept override {
        return m_gpu.videoMemory / (1024 * 1024);
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
    std::expected<void, std::string> PrepareDevice() {

        m_factory = D3D12CoreHelper::CreateFactory();
        auto gpus = D3D12CoreHelper::QueryGPUs(m_factory.Get());
        m_device = D3D12CoreHelper::CreateDevice(gpus[0]);

        m_gpu    = std::move(gpus[0]);
        if (!m_device)
            return std::unexpected("D3D12 device oluşturulamadı");

        CommandObjects c;
        c.queue = D3D12CoreHelper::CreateCommandQueue(m_device.Get());
        c.allocator = D3D12CoreHelper::CreateCommandAllocator(m_device.Get());
        c.list = D3D12CoreHelper::CreateGraphicsCommandList(m_device.Get(), c.allocator.Get());

        return {};
    }

    bool CreateRTV() {
        rtv = D3D12CoreHelper::CreateDescriptorHeap(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3);

        CD3DX12_CPU_DESCRIPTOR_HANDLE h(rtv.heap->GetCPUDescriptorHandleForHeapStart());
        m_renderTargets.resize(3);
        for (UINT i = 0; i < 3; ++i) {
            m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
            m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, h);
            h.Offset(1, rtv.size);
        }

        D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
        srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.NumDescriptors = 1;
        srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        m_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap));

        srvSize =
            m_device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );

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

    std::expected<void, std::string> BuildPSO() {
        constexpr std::array inputDescs = {
            D3D12_INPUT_ELEMENT_DESC {
                "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
                0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            },
            D3D12_INPUT_ELEMENT_DESC{
                "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
                0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            }
        };

        const D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{
            .pRootSignature = m_rootSig.Get(),
            .VS = { m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize() },
            .PS = { m_psBlob->GetBufferPointer(), m_psBlob->GetBufferSize() },
            .BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
            .SampleMask = UINT_MAX,
            .RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
            .DepthStencilState = {.DepthEnable = FALSE },
            .InputLayout = {
                inputDescs.data(),
                static_cast<UINT>(inputDescs.size())
            },
            .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .NumRenderTargets = 1,
            .RTVFormats = { DXGI_FORMAT_R8G8B8A8_UNORM },
            .SampleDesc = {.Count = 1 }
        };

        ComPtr<ID3DBlob> error;

        HRESULT hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso));

        if (FAILED(hr)) {
            OutputDebugStringA("PSO FAILED!\n");
            return std::unexpected("PSO oluşturulamadı");
        }
        return {};
    }

    std::expected<void, std::string> GraphicLoaderSetup() {
        if (!CompileShaderModern(L"common.hlsl", L"VSMain", L"vs_6_0", &m_vsBlob))
            return std::unexpected("Vertex shader derlenemedi");
        if (!CompileShaderModern(L"common.hlsl", L"PSMain", L"ps_6_0", &m_psBlob))
            return std::unexpected("Pixel shader derlenemedi");

        constexpr std::array triangleVerts = {
            // Üst sol üçgen
            VertexPosTex{{-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f}},
            VertexPosTex{{ 0.5f,  0.5f, 0.0f}, {1.0f, 0.0f}},
            VertexPosTex{{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}},
            // Alt sağ üçgen
            VertexPosTex{{ 0.5f,  0.5f, 0.0f}, {1.0f, 0.0f}},
            VertexPosTex{{ 0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}},
            VertexPosTex{{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}},
        };

        m_vb = CreateUploadBuffer(sizeof(triangleVerts));
        if (!m_vb) return std::unexpected("Vertex buffer oluşturulamadı");

        UploadToBuffer(m_vb.Get(), std::span{ triangleVerts });

        m_vbView = {
            .BufferLocation = m_vb->GetGPUVirtualAddress(),
            .SizeInBytes    = static_cast<UINT>(sizeof(triangleVerts)),
            .StrideInBytes  = sizeof(VertexPosTex)
        };


        texLoader.Init(
            m_device.Get(),
            m_cmd.queue.Get(),
            srvHeap.Get(),
            srvSize
        );

       diffuse = texLoader.Load(L"Texture.bmp", 0);

       rtvSize = rtv.size;
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

        CD3DX12_DESCRIPTOR_RANGE1 range;
        range.Init(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            1,
            0
        );

        CD3DX12_ROOT_PARAMETER1 param;
        param.InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_STATIC_SAMPLER_DESC sampler(
            0,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP
        );

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rsDesc.Desc_1_1.NumParameters = 1;
        rsDesc.Desc_1_1.pParameters = &param;
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

    ComPtr<ID3D12Resource2> CreateUploadBuffer(size_t size) {
        ComPtr<ID3D12Resource2> r;
        const auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const auto rd = CD3DX12_RESOURCE_DESC::Buffer(size);
        m_device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&r)
        );
        return r;
    }

    void UploadToBuffer(ID3D12Resource2* resource, std::span<const VertexPosTex> data) {
        void* ptr = nullptr;
        resource->Map(0, nullptr, &ptr);
        std::memcpy(ptr, data.data(), data.size_bytes());
        resource->Unmap(0, nullptr);
    }

    bool CompileShaderModern(LPCWSTR file, LPCWSTR entry, LPCWSTR target, ID3DBlob** outBlob) {
        ComPtr<IDxcUtils>    utils;
        ComPtr<IDxcCompiler3> compiler;
        DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
        DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

        ComPtr<IDxcBlobEncoding> src;
        if (FAILED(utils->LoadFile(file, nullptr, &src))) return false;

        const DxcBuffer buf{
            .Ptr = src->GetBufferPointer(),
            .Size = src->GetBufferSize(),
            .Encoding = DXC_CP_UTF8
        };

        std::array<LPCWSTR, 6> args{ file, L"-E", entry, L"-T", target, L"-Zi" };

        ComPtr<IDxcResult> result;
        compiler->Compile(
            &buf,
            args.data(), static_cast<UINT32>(args.size()),
            nullptr,
            IID_PPV_ARGS(&result)
        );

        if (ComPtr<IDxcBlobUtf8> errors;
            SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr))
            && errors && errors->GetStringLength() > 0)
        {
            OutputDebugStringA(errors->GetStringPointer());
        }
        result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(reinterpret_cast<IDxcBlob**>(outBlob)), nullptr);
        return true;
    }

    RendererCreateInfo      m_info{}; //
    GPUInfo                 m_gpu{}; //

    ComPtr<IDXGIFactory6>   m_factory; //
    ComPtr<ID3D12Device>    m_device; //
    CommandObjects          m_cmd;  //

    ComPtr<IDXGISwapChain3>             m_swapChain; //

    UINT rtvSize;
    DescriptorHeap rtv; //
    ComPtr<ID3D12DescriptorHeap> rtvHeap; 

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

    ComPtr<ID3D12DescriptorHeap> srvHeap;
    UINT srvSize;

    WICTextureLoaderDX12 texLoader;
    WICTextureLoaderDX12::Texture diffuse;

};
