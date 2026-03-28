#pragma once
#include "DescriptorHeapAllocator.h"

#include <d3dx12.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

export template<typename T>
class ConstantBuffer {
public:
    static constexpr UINT AlignedSize =
        (sizeof(T) + 255) & ~255;

    bool Init(ID3D12Device* device,
        DescriptorHeapAllocator* srvAllocator,
        UINT frameCount = 3)
    {
        m_frameCount = frameCount;
        m_allocator = srvAllocator;
        m_mappedData.resize(frameCount, nullptr);
        m_heapIndices.resize(frameCount);
        m_buffers.resize(frameCount);

        for (UINT i = 0; i < frameCount; ++i) {
            if (!CreateBuffer(device, i))
                return false;

            if (!CreateCBV(device, i))
                return false;

            CD3DX12_RANGE readRange(0, 0);
            m_buffers[i]->Map(0, &readRange,
                reinterpret_cast<void**>(&m_mappedData[i]));
        }
        return true;
    }

    void Update(UINT frameIndex, const T& data) {
        assert(frameIndex < m_frameCount);
        std::memcpy(m_mappedData[frameIndex], &data, sizeof(T));
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(UINT frameIndex) const {
        return m_allocator->GetGPU(m_heapIndices[frameIndex]);
    }

    void Shutdown() {
        for (UINT i = 0; i < m_frameCount; ++i) {
            if (m_buffers[i] && m_mappedData[i]) {
                m_buffers[i]->Unmap(0, nullptr);
                m_mappedData[i] = nullptr;
            }
        }
    }

    ~ConstantBuffer() { Shutdown(); }

private:
    bool CreateBuffer(ID3D12Device* device, UINT index) {
        const auto heapProps =
            CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const auto bufferDesc =
            CD3DX12_RESOURCE_DESC::Buffer(AlignedSize);

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_buffers[index])
        );
        return SUCCEEDED(hr);
    }

    bool CreateCBV(ID3D12Device* device, UINT index) {
        m_heapIndices[index] = m_allocator->AllocateOrReuse();

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
        cbvDesc.BufferLocation =
            m_buffers[index]->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = AlignedSize;

        device->CreateConstantBufferView(
            &cbvDesc,
            m_allocator->GetCPU(m_heapIndices[index])
        );
        return true;
    }

private:
    UINT                                   m_frameCount = 3;
    DescriptorHeapAllocator* m_allocator = nullptr;
    std::vector<ComPtr<ID3D12Resource>>    m_buffers;
    std::vector<T*>                        m_mappedData;
    std::vector<UINT>                      m_heapIndices;
};