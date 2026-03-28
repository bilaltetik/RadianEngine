#pragma once 

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <vector>
#include <d3dx12_root_signature.h>

using Microsoft::WRL::ComPtr;

class DescriptorHeapAllocator {
public:
    void Init(ID3D12Device* device, UINT maxDescriptors) {
        m_maxDescriptors = maxDescriptors;
        m_current = 0;

        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = maxDescriptors;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));

        m_incrementSize = device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
    }

    UINT Allocate() {
        assert(m_current < m_maxDescriptors && "Heap doldu!");
        return m_current++;
    }

    void Free(UINT index) {
        m_freeList.push_back(index);
    }

    UINT AllocateOrReuse() {
        if (!m_freeList.empty()) {
            UINT idx = m_freeList.back();
            m_freeList.pop_back();
            return idx;
        }
        return Allocate();
    }

    ID3D12DescriptorHeap* GetHeap()  const { return m_heap.Get(); }
    UINT GetIncrementSize()          const { return m_incrementSize; }

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetCPU(UINT index) const {
        return CD3DX12_CPU_DESCRIPTOR_HANDLE(
            m_heap->GetCPUDescriptorHandleForHeapStart(),
            index, m_incrementSize
        );
    }
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPU(UINT index) const {
        return CD3DX12_GPU_DESCRIPTOR_HANDLE(
            m_heap->GetGPUDescriptorHandleForHeapStart(),
            index, m_incrementSize
        );
    }

private:
    ComPtr<ID3D12DescriptorHeap> m_heap;
    UINT m_maxDescriptors = 0;
    UINT m_current = 0;
    UINT m_incrementSize = 0;
    std::vector<UINT> m_freeList;
};