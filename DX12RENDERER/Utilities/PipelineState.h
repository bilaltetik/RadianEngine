#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <d3dx12.h>

#include "Assert.h"


using Microsoft::WRL::ComPtr;

typedef D3D12_INPUT_ELEMENT_DESC* PipelineElements;

static D3D12_INPUT_ELEMENT_DESC IE_POS_COLOR[] = {
           D3D12_INPUT_ELEMENT_DESC {
               "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
               0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
           },
           D3D12_INPUT_ELEMENT_DESC{
               "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
               0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
           }
};

static D3D12_INPUT_ELEMENT_DESC IE_POS_COLOR_NORMAL[] = {
           D3D12_INPUT_ELEMENT_DESC {
               "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
               0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
           },
           D3D12_INPUT_ELEMENT_DESC{
               "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
               0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
           },
           D3D12_INPUT_ELEMENT_DESC{
               "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,
               0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
           },
           D3D12_INPUT_ELEMENT_DESC{
               "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT,
               0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
           }
};

static D3D12_INPUT_ELEMENT_DESC IE_POS_TEX[] = {
           D3D12_INPUT_ELEMENT_DESC {
               "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
               0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
           },
           D3D12_INPUT_ELEMENT_DESC{
               "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
               0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
           }
};

static D3D12_INPUT_ELEMENT_DESC IE_POS_TEX_NORMAL[] = {
           D3D12_INPUT_ELEMENT_DESC {
               "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
               0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
           },
           D3D12_INPUT_ELEMENT_DESC{
               "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
               0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
           },
           D3D12_INPUT_ELEMENT_DESC{
               "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,
               0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
           },
           D3D12_INPUT_ELEMENT_DESC{
               "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT,
               0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
           }
};

class PipelineState
{
public:
	static ComPtr<ID3D12PipelineState> Create(ID3D12Device* device, ID3D12RootSignature* rootSignature, PipelineElements elements, UINT count, ID3DBlob* vs, ID3DBlob* ps, D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveType)
	{

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{
            .pRootSignature = rootSignature,
            .VS = { vs->GetBufferPointer(), vs->GetBufferSize() },
            .PS = { ps->GetBufferPointer(), ps->GetBufferSize() },
            .BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
            .SampleMask = UINT_MAX,
            .RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
            .DepthStencilState = {.DepthEnable = FALSE },
            .InputLayout = {
                elements,
                count
            },
            .PrimitiveTopologyType = primitiveType,
            .NumRenderTargets = 1,
            .RTVFormats = { DXGI_FORMAT_R8G8B8A8_UNORM },
            .SampleDesc = {.Count = 1 }
        };

		ComPtr<ID3D12PipelineState> pso;
        HR_ASSERT(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));
        return pso;
	}
};