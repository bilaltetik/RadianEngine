#pragma once
#include "Assert.h"

#include <dxcapi.h>
#include <array>

class ShaderCompiler
{
public:
	static ComPtr<ID3DBlob> Compile(LPCWSTR file, LPCWSTR entry, LPCWSTR target)
	{
        ComPtr<IDxcUtils>    utils;
        ComPtr<IDxcCompiler3> compiler;
        DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
        DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

        ComPtr<IDxcBlobEncoding> src;
        HR_ASSERT(utils->LoadFile(file, nullptr, &src));       

        const DxcBuffer buf{
            .Ptr = src->GetBufferPointer(),
            .Size = src->GetBufferSize(),
            .Encoding = DXC_CP_UTF8
        };

        std::array<LPCWSTR, 6> args{ file, L"-E", entry, L"-T", target, L"-Zi" };

        ComPtr<IDxcResult> result;
        HR_ASSERT(compiler->Compile(
            &buf,
            args.data(), static_cast<UINT32>(args.size()),
            nullptr,
            IID_PPV_ARGS(&result)
        ));

        ComPtr<IDxcBlobUtf8> errors;

        if(SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) && errors && errors->GetStringLength() > 0)            
        {
            OutputDebugStringA(errors->GetStringPointer());
        }

        ComPtr<ID3DBlob> blob;
        result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&blob), nullptr);
        return blob;
	}
};