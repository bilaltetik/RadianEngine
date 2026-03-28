#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <d3dx12.h>

#include "Assert.h"

using Microsoft::WRL::ComPtr;

class Buffer
{
public:
	static ComPtr<ID3D12Resource2> Create(ID3D12Device* device, size_t size)
	{
        ComPtr<ID3D12Resource2> r;
        const auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const auto rd = CD3DX12_RESOURCE_DESC::Buffer(size);
        HR_ASSERT(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&r)
        ));
        return r;
	}

    static void Upload(ID3D12Resource2* resource, void* data, size_t size) {
        void* ptr = nullptr;
        resource->Map(0, nullptr, &ptr);
        std::memcpy(ptr, data, size);
        resource->Unmap(0, nullptr);
    }
};
