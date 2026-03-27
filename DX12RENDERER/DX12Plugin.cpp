module;

#pragma comment(lib, "dxcompiler.lib")
#define NOMINMAX
#include <Windows.h>
#include "IRenderer.h"


module Radian.DX12;

extern "C" {
    __declspec(dllexport)
    Radian::Renderer::IRenderer* CreateRenderer() {
        return new DX12Renderer{};
    }

    __declspec(dllexport)
    void DestroyRenderer(Radian::Renderer::IRenderer* renderer) {
        delete renderer;
    }
}

BOOL WINAPI DllMain(HINSTANCE /*hInst*/, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(nullptr);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
