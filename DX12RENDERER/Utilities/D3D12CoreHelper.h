#pragma once

#include <string>
#include <dxgi1_6.h>
#include <wrl.h>
#include <d3d12.h>
#include "Assert.h"

using Microsoft::WRL::ComPtr;

struct GPUInfo {
    std::wstring          name;
    size_t                videoMemory = 0;
    ComPtr<IDXGIAdapter1> adapter;
};

struct DescriptorHeap
{
    ComPtr<ID3D12DescriptorHeap>        heap;
    UINT                                size = 0;
};

class D3D12CoreHelper
{
public:

    static ComPtr<IDXGIFactory6> CreateFactory()
    {
        ComPtr<IDXGIFactory6> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
            return nullptr;

        return factory;
    }

    static std::vector<GPUInfo> QueryGPUs(IDXGIFactory6* factory) {
        std::vector<GPUInfo> gpus;
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0;
            factory->EnumAdapterByGpuPreference(
                i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)
            ) != DXGI_ERROR_NOT_FOUND;
            ++i)
        {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            gpus.emplace_back(GPUInfo{
                .name = desc.Description,
                .videoMemory = desc.DedicatedVideoMemory,
                .adapter = adapter
                });
        }
        return gpus;
    }

    static ComPtr<ID3D12Device> CreateDevice(const GPUInfo& gpu) {
        ComPtr<ID3D12Device> d;

#if defined(_DEBUG)
        if (ComPtr<ID3D12Debug> dbg;
            SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
        {
            dbg->EnableDebugLayer();
        }
#endif
        D3D12CreateDevice(gpu.adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d));
        return d;
    }

    static ComPtr<ID3D12CommandQueue> CreateCommandQueue(ID3D12Device* device)
    {
        ComPtr<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC qd{ .Type = D3D12_COMMAND_LIST_TYPE_DIRECT };

        if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) return nullptr;

        return queue;

    }

    static ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ID3D12Device* device)
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))) return nullptr;
        return allocator;
    }

    static ComPtr<ID3D12GraphicsCommandList10> CreateGraphicsCommandList(ID3D12Device* device, ID3D12CommandAllocator* allocator)
    {
        ComPtr<ID3D12GraphicsCommandList10> list;
        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list)))) return nullptr;
        list->Close();
        return list;
    }

    static ComPtr<IDXGISwapChain3> CreateSwapChain(IDXGIFactory6* factory, ID3D12CommandQueue* q, HWND h, UINT w, UINT ht, UINT bufferCount)
    {
        const DXGI_SWAP_CHAIN_DESC1 desc{
            .Width = w,
            .Height = ht,
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .Stereo = FALSE,
            .SampleDesc = {.Count = 1, .Quality = 0 },
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = bufferCount,
            .Scaling = DXGI_SCALING_STRETCH,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
            .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
            .Flags = 0
        };

        ComPtr<IDXGISwapChain1> s1;
        HR_ASSERT(factory->CreateSwapChainForHwnd(q, h, &desc, nullptr, nullptr, &s1));

        ComPtr<IDXGISwapChain3> s3;
        HR_ASSERT(s1.As(&s3));

        return s3;
    }

    static DescriptorHeap CreateDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors)
    {
        DescriptorHeap dh;
        const D3D12_DESCRIPTOR_HEAP_DESC dhd{
            .Type = type,
            .NumDescriptors = numDescriptors
        };
        device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&dh.heap));
        dh.size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        return dh;
    }

};