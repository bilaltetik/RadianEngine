// DX12Plugin.cpp
// Radian.DX12 modülünün implementation unit'i.
// "module Radian.DX12;" bildirimi sayesinde DX12Renderer sınıfına
// (export edilmemiş olsa da) erişim hakkımız var.
// Bu dosya DLL'in giriş noktasını ve extern "C" fabrika fonksiyonlarını içerir.
module;

#pragma comment(lib, "dxcompiler.lib")
#define NOMINMAX
#include <Windows.h>


module Radian.DX12;

// -------------------------------------------------------------------
// Agility SDK sembolleri DLL içinde tanımlanır (exe'de değil artık).
// Loader bunları LoadLibraryW sonrası tarar.
// -------------------------------------------------------------------
/**
extern "C" {
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion = 619;
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}
*/
// -------------------------------------------------------------------
// Fabrika fonksiyonları — name-mangling yok, GetProcAddress güvenli.
//
// CreateRenderer  : yığında bir DX12Renderer oluşturur; sahiplik çağırana geçer.
// DestroyRenderer : sahipliği geri alıp siler — delete burada yapılmalı,
//                   çünkü new de aynı DLL'deydi (heap uyuşmazlığı önlenir).
// -------------------------------------------------------------------
extern "C" {
    __declspec(dllexport)
    Radian::Renderer::IRenderer* CreateRenderer() {
        return new DX12Renderer{};
    }

    __declspec(dllexport)
    void DestroyRenderer(Radian::Renderer::IRenderer* renderer) {
        // Shutdown → destructor zinciri burada başlar
        delete renderer;
    }
}

// -------------------------------------------------------------------
// DllMain — COM veya diğer DLL-scope kaynak yönetimi gerekirse burası.
// Şimdilik minimal; DX12 kaynakları IRenderer::Shutdown()'da serbest bırakılır.
// -------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE /*hInst*/, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            // DisableThreadLibraryCalls — thread attach/detach bildirimlerini devre dışı bırak
            // (bu DLL thread-local depolama kullanmıyor)
            DisableThreadLibraryCalls(nullptr);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
