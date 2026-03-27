#include <WICTextureLoader.h>
#include <ResourceUploadBatch.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class WICTextureLoaderDX12 {
public:
    struct Texture {
        ComPtr<ID3D12Resource> resource;
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle{};
    };

    void Init(
        ID3D12Device* device,
        ID3D12CommandQueue* queue,
        ID3D12DescriptorHeap* srvHeap,
        UINT descriptorSize
    ) {
        m_device = device;
        m_queue = queue;
        m_srvHeap = srvHeap;
        m_descriptorSize = descriptorSize;
    }

    Texture Load(const std::wstring& path, UINT index = 0) {
        Texture tex;

        DirectX::ResourceUploadBatch uploadBatch(m_device);
        uploadBatch.Begin();

        DirectX::CreateWICTextureFromFile(
            m_device,
            uploadBatch,
            path.c_str(),
            &tex.resource
        );

        auto finish = uploadBatch.End(m_queue);
        finish.wait();

        tex.srvHandle = CreateSRV(tex.resource.Get(), index);
        return tex;
    }

private:
    D3D12_GPU_DESCRIPTOR_HANDLE CreateSRV(ID3D12Resource* texture, UINT index) {
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(
            m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
            index,
            m_descriptorSize
            );

        CD3DX12_GPU_DESCRIPTOR_HANDLE gpu(
            m_srvHeap->GetGPUDescriptorHandleForHeapStart(),
            index,
            m_descriptorSize
        );

        D3D12_RESOURCE_DESC desc = texture->GetDesc();

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = desc.Format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = desc.MipLevels;

        m_device->CreateShaderResourceView(texture, &srv, cpu);

        return gpu;
    }

private:
    ID3D12Device* m_device = nullptr;
    ID3D12CommandQueue* m_queue = nullptr;

    ID3D12DescriptorHeap* m_srvHeap = nullptr;
    UINT m_descriptorSize = 0;
};