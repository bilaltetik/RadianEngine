#pragma once
#include "DirectXMath.h"

//Standard
struct Vertex {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT2 texcoord;

    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT3 tangent;
};

//Position + Texture
struct VertexPosTex {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT2 texcoord;
};

//Position + Color
struct VertexPosColor {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT4 color;
};

//Position + Color + Normal
struct VertexPosColorNormal {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT4 color;

    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT3 tangent;
};
