#ifndef UNICODE
#define UNICODE
#endif

// --- Agility SDK Ayarları ---
extern "C" {
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion = 619;
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}

// --- Kütüphaneler ---
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxcompiler.lib")

#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dx12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <vector>
#include <string>

// --- Modüller ---
import std;
import Radian.Platform;

using Microsoft::WRL::ComPtr;

// --- Veri Yapıları ---
struct Vertex {
    float pos[3];
    float col[4];
};

struct GPUInfo {
    std::wstring name;
    size_t videoMemory = 0;
    ComPtr<IDXGIAdapter1> adapter;
};

struct CommandObjects {
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList10> list;
};

// --- RENDERER SINIFI ---
class DX12Renderer {
public:
    CommandObjects commandObjects;
    Radian::Platform::Window* window;
    Radian::Platform::WindowConfig config;

    ComPtr<ID3D12Device> device;
    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    std::vector<ComPtr<ID3D12Resource>> renderTargets;

    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pipelineState;

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;

    ComPtr<ID3D12Resource2> vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vbView{};

    ComPtr<ID3D12Fence> fence;
    uint64_t fenceValue = 1;
    HANDLE fenceEvent = nullptr;
    uint32_t frameIndex = 0;
    uint32_t rtvDescriptorSize = 0;

    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissorRect{};

    DX12Renderer(Radian::Platform::Window* win, Radian::Platform::WindowConfig cfg)
        : window(win), config(cfg) {
        renderTargets.resize(3);
    }

    bool DevicePrepare() {
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;

        auto gpus = QueryAvailableGPUs();
        if (gpus.empty()) return false;

        device = CreateDX12Device(gpus[0]); // En yüksek VRAM'li kartı seç
        if (!device) return false;

        commandObjects = CreateCommandObjects(device.Get());
        return true;
    }

    bool ViewSetup() {
        swapChain = CreateSwapChain(commandObjects.queue.Get(), window->GetHandle(), config.width, config.height);
        if (!swapChain || !CreateRTV() || !InitSync()) return false;

        viewport = { 0.0f, 0.0f, (float)config.width, (float)config.height, 0.0f, 1.0f };
        scissorRect = { 0, 0, (long)config.width, (long)config.height };

        return PipelineSetup();
    }

    bool GraphicLoaderSetup() {
        // 1. Shader Derleme (common.hlsl dosyasının varlığından emin ol)
        if (!CompileShaderModern(L"common.hlsl", L"VSMain", L"vs_6_0", &vsBlob)) return false;
        if (!CompileShaderModern(L"common.hlsl", L"PSMain", L"ps_6_0", &psBlob)) return false;

        // 2. Vertex Datası
        Vertex triangleVertices[] = {
            {{ 0.0f,  0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
            {{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}
        };

        // 3. Buffer İşlemleri
        vertexBuffer = CreateUploadBuffer(sizeof(triangleVertices));
        if (!vertexBuffer) return false;

        UploadToBuffer(vertexBuffer.Get(), triangleVertices, sizeof(triangleVertices));

        // 4. View Hazırlığı
        vbView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
        vbView.StrideInBytes = sizeof(Vertex);
        vbView.SizeInBytes = sizeof(triangleVertices);

        return true;
    }

    bool CreatePSO() {
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = rootSignature.Get();
        psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;

        return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));
    }

    void BeginFrame() {
        frameIndex = swapChain->GetCurrentBackBufferIndex();
        commandObjects.allocator->Reset();
        commandObjects.list->Reset(commandObjects.allocator.Get(), pipelineState.Get());
    }

    void ApplyBarrier(bool toRenderTarget) {
        D3D12_TEXTURE_BARRIER barrier = {};
        barrier.SyncBefore = toRenderTarget ? D3D12_BARRIER_SYNC_NONE : D3D12_BARRIER_SYNC_RENDER_TARGET;
        barrier.SyncAfter = toRenderTarget ? D3D12_BARRIER_SYNC_RENDER_TARGET : D3D12_BARRIER_SYNC_NONE;
        barrier.AccessBefore = toRenderTarget ? D3D12_BARRIER_ACCESS_NO_ACCESS : D3D12_BARRIER_ACCESS_RENDER_TARGET;
        barrier.AccessAfter = toRenderTarget ? D3D12_BARRIER_ACCESS_RENDER_TARGET : D3D12_BARRIER_ACCESS_NO_ACCESS;
        barrier.LayoutBefore = toRenderTarget ? D3D12_BARRIER_LAYOUT_PRESENT : D3D12_BARRIER_LAYOUT_RENDER_TARGET;
        barrier.LayoutAfter = toRenderTarget ? D3D12_BARRIER_LAYOUT_RENDER_TARGET : D3D12_BARRIER_LAYOUT_PRESENT;
        barrier.pResource = renderTargets[frameIndex].Get();

        D3D12_BARRIER_GROUP group = { D3D12_BARRIER_TYPE_TEXTURE, 1 };
        group.pTextureBarriers = &barrier;
        commandObjects.list->Barrier(1, &group);
    }

    void EndFrame() {
        commandObjects.list->Close();
        ID3D12CommandList* lists[] = { commandObjects.list.Get() };
        commandObjects.queue->ExecuteCommandLists(1, lists);
        swapChain->Present(1, 0);
        WaitForGPU();
    }

    void WaitForGPU() {
        const uint64_t waitValue = fenceValue++;
        commandObjects.queue->Signal(fence.Get(), waitValue);
        if (fence->GetCompletedValue() < waitValue) {
            fence->SetEventOnCompletion(waitValue, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }

private:
    std::vector<GPUInfo> QueryAvailableGPUs() {
        std::vector<GPUInfo> gpus;
        ComPtr<IDXGIAdapter1> adapter;
        for (uint32_t i = 0; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            gpus.push_back({ desc.Description, desc.DedicatedVideoMemory, adapter });
        }
        return gpus;
    }

    ComPtr<ID3D12Device> CreateDX12Device(const GPUInfo& gpu) {
        ComPtr<ID3D12Device> d;
#if defined(_DEBUG)
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer();
#endif
        D3D12CreateDevice(gpu.adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d));
        return d;
    }

    CommandObjects CreateCommandObjects(ID3D12Device* d) {
        CommandObjects c;
        D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
        d->CreateCommandQueue(&qd, IID_PPV_ARGS(&c.queue));
        d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&c.allocator));
        ComPtr<ID3D12GraphicsCommandList> bl;
        d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, c.allocator.Get(), nullptr, IID_PPV_ARGS(&bl));
        bl.As(&c.list);
        c.list->Close();
        return c;
    }

    ComPtr<IDXGISwapChain3> CreateSwapChain(ID3D12CommandQueue* q, HWND h, int w, int ht) {
        ComPtr<IDXGISwapChain1> s1;
        ComPtr<IDXGISwapChain3> s3;
        DXGI_SWAP_CHAIN_DESC1 d = { (UINT)w, (UINT)ht, DXGI_FORMAT_R8G8B8A8_UNORM, FALSE, {1,0}, DXGI_USAGE_RENDER_TARGET_OUTPUT, 3, DXGI_SCALING_STRETCH, DXGI_SWAP_EFFECT_FLIP_DISCARD };
        factory->CreateSwapChainForHwnd(q, h, &d, nullptr, nullptr, &s1);
        s1.As(&s3);
        return s3;
    }

    bool InitSync() {
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        return fenceEvent != nullptr;
    }

    bool CreateRTV() {
        D3D12_DESCRIPTOR_HEAP_DESC dhd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3 };
        device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&rtvHeap));
        rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        CD3DX12_CPU_DESCRIPTOR_HANDLE h(rtvHeap->GetCPUDescriptorHandleForHeapStart());
        for (uint32_t i = 0; i < 3; i++) {
            swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
            device->CreateRenderTargetView(renderTargets[i].Get(), nullptr, h);
            h.Offset(1, rtvDescriptorSize);
        }
        return true;
    }

    bool PipelineSetup() {
        CD3DX12_ROOT_SIGNATURE_DESC rd(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ComPtr<ID3DBlob> s, e;
        D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &s, &e);
        return SUCCEEDED(device->CreateRootSignature(0, s->GetBufferPointer(), s->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
    }

    ComPtr<ID3D12Resource2> CreateUploadBuffer(size_t s) {
        ComPtr<ID3D12Resource2> r;
        auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto rd = CD3DX12_RESOURCE_DESC::Buffer(s);
        device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&r));
        return r;
    }

    void UploadToBuffer(ID3D12Resource2* r, const void* d, size_t s) {
        void* p;
        r->Map(0, nullptr, &p);
        memcpy(p, d, s);
        r->Unmap(0, nullptr);
    }

    bool CompileShaderModern(LPCWSTR f, LPCWSTR e, LPCWSTR t, ID3DBlob** b) {
        ComPtr<IDxcUtils> u; ComPtr<IDxcCompiler3> c;
        DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&u));
        DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&c));
        ComPtr<IDxcBlobEncoding> src;
        if (FAILED(u->LoadFile(f, nullptr, &src))) return false;
        DxcBuffer buf = { src->GetBufferPointer(), src->GetBufferSize(), DXC_CP_UTF8 };
        std::vector<LPCWSTR> args = { f, L"-E", e, L"-T", t, L"-Zi" };
        ComPtr<IDxcResult> res;
        c->Compile(&buf, args.data(), (uint32_t)args.size(), nullptr, IID_PPV_ARGS(&res));
        ComPtr<IDxcBlobUtf8> err;
        res->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&err), nullptr);
        if (err && err->GetStringLength() > 0) OutputDebugStringA(err->GetStringPointer());
        res->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS((IDxcBlob**)b), nullptr);
        return true;
    }
};

// --- WINMAIN ---
int WINAPI WinMain(HINSTANCE, HINSTANCE, PSTR, int) {
    Radian::Platform::Window window;
    Radian::Platform::WindowConfig config{ 1280, 720, L"THE Radian" };
    if (!window.Create(config)) return -1;

    DX12Renderer renderer(&window, config);
    if (!renderer.DevicePrepare() || !renderer.ViewSetup() || !renderer.GraphicLoaderSetup() || !renderer.CreatePSO()) return -1;

    while (!window.ShouldClose()) {
        window.ProcessMessages();

        renderer.BeginFrame();
        renderer.ApplyBarrier(true);

        auto list = renderer.commandObjects.list.Get();
        list->RSSetViewports(1, &renderer.viewport);
        list->RSSetScissorRects(1, &renderer.scissorRect);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += (renderer.frameIndex * renderer.rtvDescriptorSize);
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

        const float clearCol[] = { 0.05f, 0.05f, 0.15f, 1.0f };
        list->ClearRenderTargetView(rtv, clearCol, 0, nullptr);

        list->SetGraphicsRootSignature(renderer.rootSignature.Get());
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->IASetVertexBuffers(0, 1, &renderer.vbView);

        list->DrawInstanced(3, 1, 0, 0);

        renderer.ApplyBarrier(false);
        renderer.EndFrame();
    }
    return 0;
}