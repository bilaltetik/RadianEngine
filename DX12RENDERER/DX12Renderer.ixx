// DX12Renderer.ixx
// DLL'in birincil modül arayüz birimi (primary module interface unit).
// DX12Renderer sınıfı bu modülde tanımlanır; dışa aktarılmaz —
// yalnızca DX12Plugin.cpp (aynı modülün implementation unit'i) kullanır.

module;

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")

// --- Global module fragment: modül sisteminin dışındaki başlıklar ---
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
#include <IRenderer.h> // paylaşılan arayüz

export module Radian.DX12;
// "export" edilecek bir şey yok — sınıf modül-internal;
// fabrika fonksiyonları DX12Plugin.cpp'de extern "C" olarak dışa aktarılır.

import std;

using Microsoft::WRL::ComPtr;
using namespace Radian::Renderer;

// ====================================================================
// Veri yapıları
// ====================================================================

struct Vertex {
    float pos[3];
    float col[4];
};

struct GPUInfo {
    std::wstring          name;
    size_t                videoMemory = 0;
    ComPtr<IDXGIAdapter1> adapter;
};

struct CommandObjects {
    ComPtr<ID3D12CommandQueue>          queue;
    ComPtr<ID3D12CommandAllocator>      allocator;
    ComPtr<ID3D12GraphicsCommandList10> list;
};

// ====================================================================
// DX12Renderer — IRenderer implementasyonu
// ====================================================================

class DX12Renderer final : public IRenderer {
public:
    // -----------------------------------------------------------------
    // IRenderer::Init
    // DevicePrepare → ViewSetup → GraphicLoaderSetup → CreatePSO
    // [C++23] .and_then() zinciri: her adım başarılıysa bir sonrakine geç
    // -----------------------------------------------------------------
    [[nodiscard]] std::expected<void, std::string>
    Init(const RendererCreateInfo& info) override
    {
        m_info = info;  // HWND + boyutları sakla

        return DevicePrepare()
            .and_then([&] { return ViewSetup();           })
            .and_then([&] { return GraphicLoaderSetup();  })
            .and_then([&] { return BuildPSO();            });
    }

    // -----------------------------------------------------------------
    // IRenderer::RenderFrame — her frame'de main döngüsünden çağrılır
    // -----------------------------------------------------------------
    void RenderFrame() override {
        BeginFrame();
        ApplyBarrier(true);

        auto* list = m_cmd.list.Get();
        list->RSSetViewports(1, &m_viewport);
        list->RSSetScissorRects(1, &m_scissor);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvDescSize;
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

        // [C++20] constexpr std::array — magic C dizisi yok
        constexpr std::array<float, 4> clearCol{ 0.05f, 0.05f, 0.15f, 1.0f };
        list->ClearRenderTargetView(rtv, clearCol.data(), 0, nullptr);

        list->SetGraphicsRootSignature(m_rootSig.Get());
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->IASetVertexBuffers(0, 1, &m_vbView);
        list->DrawInstanced(3, 1, 0, 0);

        ApplyBarrier(false);
        EndFrame(/*vsync=*/false);
    }

    // -----------------------------------------------------------------
    // IRenderer::Shutdown — idempotent; destructor da çağırır
    // -----------------------------------------------------------------
    void Shutdown() override {
        if (m_fenceEvent) {
            WaitForGPU();
            CloseHandle(m_fenceEvent);
            m_fenceEvent = nullptr;
        }
    }

    // -----------------------------------------------------------------
    // IRenderer sorgulama arayüzü
    // -----------------------------------------------------------------
    [[nodiscard]] std::wstring_view GetGPUName() const noexcept override {
        return m_gpu.name;
    }
    [[nodiscard]] size_t GetVRAMMB() const noexcept override {
        return m_gpu.videoMemory / (1024 * 1024);
    }

    ~DX12Renderer() override { Shutdown(); }

private:
    // -----------------------------------------------------------------
    // Init aşamaları — her biri std::expected döndürür
    // -----------------------------------------------------------------

    std::expected<void, std::string> DevicePrepare() {
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory))))
            return std::unexpected("DXGI factory oluşturulamadı");

        auto gpus = QueryGPUs();
        if (gpus.empty())
            return std::unexpected("Uygun GPU bulunamadı");

        m_device = CreateDevice(gpus[0]);
        m_gpu    = std::move(gpus[0]);
        if (!m_device)
            return std::unexpected("D3D12 device oluşturulamadı");

        m_cmd = CreateCommandObjects(m_device.Get());
        if (!m_cmd.queue || !m_cmd.list) {
            return std::unexpected("Hata: Komut nesneleri (v10) oluşturulamadı! Agility SDK (D3D12Core.dll) EXE'nin yanında bulunamadı veya sisteminiz desteklemiyor.");
        }
        return {};
    }

    std::expected<void, std::string> ViewSetup() {
        if (!m_factory) return std::unexpected("DXGI Factory nesnesi oluşturulmamış.");

        auto swResult = CreateSwapChain(m_cmd.queue.Get(), m_info.hwnd, m_info.width, m_info.height);
        if (!swResult) return std::unexpected("SwapChain Hatası: " + swResult.error());
        m_swapChain = swResult.value();

        if (!CreateRTV())  return std::unexpected("RTV oluşturulamadı");
        if (!InitSync())   return std::unexpected("Fence/sync nesnesi oluşturulamadı");

        // [C++20] Designated initializers
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
        if (!CompileShaderModern(L"common.hlsl", L"VSMain", L"vs_6_0", &m_vsBlob))
            return std::unexpected("Vertex shader derlenemedi");
        if (!CompileShaderModern(L"common.hlsl", L"PSMain", L"ps_6_0", &m_psBlob))
            return std::unexpected("Pixel shader derlenemedi");

        // [C++20] constexpr std::array — CTAD ile tür otomatik çıkarılır
        constexpr std::array triangleVerts = {
            Vertex{{ 0.0f,  0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
            Vertex{{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
            Vertex{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}
        };

        m_vb = CreateUploadBuffer(sizeof(triangleVerts));
        if (!m_vb) return std::unexpected("Vertex buffer oluşturulamadı");

        // [C++20] std::span — ham pointer + boyut çifti yerine
        UploadToBuffer(m_vb.Get(), std::span{ triangleVerts });

        m_vbView = {
            .BufferLocation = m_vb->GetGPUVirtualAddress(),
            .SizeInBytes    = static_cast<UINT>(sizeof(triangleVerts)),
            .StrideInBytes  = sizeof(Vertex)
        };
        return {};
    }

    std::expected<void, std::string> BuildPSO() {
        // [C++20] std::array — _countof() macro'su yok
        constexpr std::array inputDescs = {
            D3D12_INPUT_ELEMENT_DESC{
                "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
                0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            },
            D3D12_INPUT_ELEMENT_DESC{
                "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
                0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
            }
        };

        const D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{
            .pRootSignature        = m_rootSig.Get(),
            .VS                    = { m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize() },
            .PS                    = { m_psBlob->GetBufferPointer(), m_psBlob->GetBufferSize() },
            .BlendState            = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
            .SampleMask            = UINT_MAX,
            .RasterizerState       = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
            .DepthStencilState     = { .DepthEnable = FALSE },
            .InputLayout           = {
                inputDescs.data(),
                static_cast<UINT>(inputDescs.size())
            },
            .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .NumRenderTargets      = 1,
            .RTVFormats            = { DXGI_FORMAT_R8G8B8A8_UNORM },
            .SampleDesc            = { .Count = 1 }
        };

        if (FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso))))
            return std::unexpected("PSO oluşturulamadı");
        return {};
    }

    // -----------------------------------------------------------------
    // Frame yardımcıları
    // -----------------------------------------------------------------

    void BeginFrame() {
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        m_cmd.allocator->Reset();
        m_cmd.list->Reset(m_cmd.allocator.Get(), m_pso.Get());
    }

    void ApplyBarrier(bool toRenderTarget) {
        const D3D12_TEXTURE_BARRIER barrier{
            .SyncBefore   = toRenderTarget ? D3D12_BARRIER_SYNC_NONE          : D3D12_BARRIER_SYNC_RENDER_TARGET,
            .SyncAfter    = toRenderTarget ? D3D12_BARRIER_SYNC_RENDER_TARGET : D3D12_BARRIER_SYNC_NONE,
            .AccessBefore = toRenderTarget ? D3D12_BARRIER_ACCESS_NO_ACCESS    : D3D12_BARRIER_ACCESS_RENDER_TARGET,
            .AccessAfter  = toRenderTarget ? D3D12_BARRIER_ACCESS_RENDER_TARGET: D3D12_BARRIER_ACCESS_NO_ACCESS,
            .LayoutBefore = toRenderTarget ? D3D12_BARRIER_LAYOUT_PRESENT      : D3D12_BARRIER_LAYOUT_RENDER_TARGET,
            .LayoutAfter  = toRenderTarget ? D3D12_BARRIER_LAYOUT_RENDER_TARGET: D3D12_BARRIER_LAYOUT_PRESENT,
            .pResource    = m_renderTargets[m_frameIndex].Get()
        };
        const D3D12_BARRIER_GROUP group{
            .Type             = D3D12_BARRIER_TYPE_TEXTURE,
            .NumBarriers      = 1,
            .pTextureBarriers = &barrier
        };
        m_cmd.list->Barrier(1, &group);
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

    void WaitForGPU() {
        const uint64_t waitVal = m_fenceValue++;
        m_cmd.queue->Signal(m_fence.Get(), waitVal);
        if (m_fence->GetCompletedValue() < waitVal) {
            m_fence->SetEventOnCompletion(waitVal, m_fenceEvent);
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }

    // -----------------------------------------------------------------
    // Kurulum yardımcıları
    // -----------------------------------------------------------------

    std::vector<GPUInfo> QueryGPUs() {
        std::vector<GPUInfo> gpus;
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0;
             m_factory->EnumAdapterByGpuPreference(
                 i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)
             ) != DXGI_ERROR_NOT_FOUND;
             ++i)
        {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            gpus.emplace_back(GPUInfo{
                .name        = desc.Description,
                .videoMemory = desc.DedicatedVideoMemory,
                .adapter     = adapter
            });
        }
        return gpus;
    }

    ComPtr<ID3D12Device> CreateDevice(const GPUInfo& gpu) {
        ComPtr<ID3D12Device> d;
#if defined(_DEBUG)
        // [C++17] if-init: dbg değişkeni yalnızca if bloğu kapsamında
        if (ComPtr<ID3D12Debug> dbg;
            SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
        {
            dbg->EnableDebugLayer();
        }
#endif
        D3D12CreateDevice(gpu.adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d));
        return d;
    }

    CommandObjects CreateCommandObjects(ID3D12Device* d) {
        CommandObjects c;
        const D3D12_COMMAND_QUEUE_DESC qd{ .Type = D3D12_COMMAND_LIST_TYPE_DIRECT }; // [cite: 78]

        // Her adımda FAILED kontrolü ekliyoruz
        if (FAILED(d->CreateCommandQueue(&qd, IID_PPV_ARGS(&c.queue)))) return {};
        if (FAILED(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&c.allocator)))) return {};

        // v0 listesini oluşturup v10'a cast etmek yerine doğrudan v10 istemeyi deneyelim
        HRESULT hr = d->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            c.allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&c.list) // Doğrudan ID3D12GraphicsCommandList10 istiyoruz [cite: 25]
        );

        if (FAILED(hr) || !c.list) {
            // Eğer v10 başarısızsa, Agility SDK doğru yüklenmemiş demektir.
            OutputDebugStringW(L"KRİTİK HATA: ID3D12GraphicsCommandList10 oluşturulamadı!\n");
            return {};
        }

        c.list->Close(); // Artık güvenle kapatabiliriz 
        return c;
    }



    std::expected<ComPtr<IDXGISwapChain3>, std::string> CreateSwapChain(ID3D12CommandQueue* q, HWND h, UINT w, UINT ht) {
        if (!h) return std::unexpected("Pencere tutamacı (HWND) geçersiz.");

        const DXGI_SWAP_CHAIN_DESC1 desc{
            .Width = w,  // Eski haline aldık
            .Height = ht, // Eski haline aldık
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .Stereo = FALSE,
            .SampleDesc = {.Count = 1, .Quality = 0 },
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = 3,
            .Scaling = DXGI_SCALING_STRETCH, // KRİTİK DÜZELTME: FLIP_DISCARD modeli sadece NONE kabul eder
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
            .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
            .Flags = 0
        };

        ComPtr<IDXGISwapChain1> s1;
        HRESULT hr = m_factory->CreateSwapChainForHwnd(q, h, &desc, nullptr, nullptr, &s1);

        if (FAILED(hr)) {
            // HRESULT kodunu hex olarak string'e çeviriyoruz
            return std::unexpected(std::format("CreateSwapChainForHwnd başarısız. HRESULT: 0x{:08X}", (uint32_t)hr));
        }

        ComPtr<IDXGISwapChain3> s3;
        if (FAILED(s1.As(&s3))) {
            return std::unexpected("SwapChain3 arayüzü desteklenmiyor.");
        }

        return s3;
    }

    bool InitSync() {
        if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
            return false;
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        return m_fenceEvent != nullptr;
    }

    bool CreateRTV() {
        const D3D12_DESCRIPTOR_HEAP_DESC dhd{
            .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            .NumDescriptors = 3
        };
        m_device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&m_rtvHeap));
        m_rtvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        CD3DX12_CPU_DESCRIPTOR_HANDLE h(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
        m_renderTargets.resize(3);
        for (UINT i = 0; i < 3; ++i) {
            m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
            m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, h);
            h.Offset(1, m_rtvDescSize);
        }
        return true;
    }

    bool PipelineSetup() {
        CD3DX12_ROOT_SIGNATURE_DESC rd(
            0, nullptr, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
        );
        ComPtr<ID3DBlob> s, e;
        D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &s, &e);
        return SUCCEEDED(m_device->CreateRootSignature(
            0, s->GetBufferPointer(), s->GetBufferSize(), IID_PPV_ARGS(&m_rootSig)
        ));
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

    // [C++20] std::span — tip-güvenli, boyut otomatik
    void UploadToBuffer(ID3D12Resource2* resource, std::span<const Vertex> data) {
        void* ptr = nullptr;
        resource->Map(0, nullptr, &ptr);
        std::memcpy(ptr, data.data(), data.size_bytes());
        resource->Unmap(0, nullptr);
    }

    //
    // 

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

        // [C++20] std::array — boyutu sabit, heap alloc yok
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

    // -----------------------------------------------------------------
    // Üye değişkenler
    // -----------------------------------------------------------------
    RendererCreateInfo      m_info{};
    GPUInfo                 m_gpu{};

    ComPtr<IDXGIFactory6>   m_factory;
    ComPtr<ID3D12Device>    m_device;
    CommandObjects          m_cmd;

    ComPtr<IDXGISwapChain3>             m_swapChain;
    ComPtr<ID3D12DescriptorHeap>        m_rtvHeap;
    std::vector<ComPtr<ID3D12Resource>> m_renderTargets;
    UINT                                m_rtvDescSize = 0;
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
};
